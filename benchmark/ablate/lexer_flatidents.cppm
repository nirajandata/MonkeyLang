module;

#include <bit>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <immintrin.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

export module lexer_flatidents;

import token;
import ascii;

namespace detail {

constexpr size_t max_keyword_len = 8;

template <size_t N> struct KeywordMap {
  static_assert(N <= 64, "keyword hit mask must fit in a single word");

  static constexpr size_t VEC_SIZE = 8;
  static constexpr size_t NUM_VECS = (N + VEC_SIZE - 1) / VEC_SIZE;
  static constexpr size_t PADDED_N = NUM_VECS * VEC_SIZE;

  alignas(64) uint64_t keys[PADDED_N]{};
  TokenType types[PADDED_N]{};

  constexpr KeywordMap(
      const std::pair<std::string_view, TokenType> (&entries)[N]) noexcept {
    for (size_t i = 0; i < N; ++i) {
      const std::string_view keyword = entries[i].first;
      uint64_t key = 0;
      for (size_t j = 0; j < keyword.size(); ++j) {
        key |= static_cast<uint64_t>(static_cast<uint8_t>(keyword[j]))
               << (j * 8);
      }
      keys[i] = key;
      types[i] = entries[i].second;
    }
  }

  [[nodiscard]] inline TokenType lookup(const char *data, size_t len) const noexcept {
    if (len == 0 || len > max_keyword_len)
      return TokenType::Identifier;

    uint64_t raw_word;
    std::memcpy(&raw_word, data, 8);

    uint64_t word = _bzhi_u64(raw_word, len * 8);

    __m512i target = _mm512_set1_epi64(word);
    uint64_t hits = 0;

    for (size_t i = 0; i < NUM_VECS; ++i) {
      __m512i vkeys = _mm512_load_si512(reinterpret_cast<const __m512i*>(&keys[i * VEC_SIZE]));

      __mmask8 mask = _mm512_cmpeq_epi64_mask(target, vkeys);

      hits |= static_cast<uint64_t>(mask) << (i * 8);
    }

    if (hits == 0)
      return TokenType::Identifier;

    return types[std::countr_zero(hits)];
  }
};

inline constexpr std::pair<std::string_view, TokenType> keyword_entries_[] = {
    {"if", TokenType::If}, {"else", TokenType::Else}, {"int", TokenType::Int},
    {"void", TokenType::Void}, {"return", TokenType::Return}, {"goto", TokenType::Goto},
    {"do", TokenType::Do}, {"for", TokenType::For}, {"while", TokenType::While},
    {"break", TokenType::Break}, {"continue", TokenType::Continue}, {"switch", TokenType::Switch},
    {"case", TokenType::Case}, {"default", TokenType::Default}, {"static", TokenType::Static},
    {"extern", TokenType::Extern},
};

inline constexpr size_t keyword_count =
    sizeof(keyword_entries_) / sizeof(keyword_entries_[0]);

constexpr bool all_keywords_fit() noexcept {
  for (const auto &keyword_entrie : keyword_entries_) {
    if (keyword_entrie.first.size() > max_keyword_len) return false;
  }
  return true;
}

static_assert(all_keywords_fit(), "a keyword does not fit in max_keyword_len bytes");

inline constexpr auto keyword_map_ = KeywordMap<keyword_count>(keyword_entries_);

}

namespace monkey::ablate::flatidents {

export class Lexer {
private:
  struct MMapHandle {
    void *map_base{nullptr};
    size_t map_size{0};

    MMapHandle() = default;
    MMapHandle(void *base, size_t size) : map_base(base), map_size(size) {}
    ~MMapHandle() { if (map_base) munmap(map_base, map_size); }

    MMapHandle(const MMapHandle &) = delete;
    MMapHandle &operator=(const MMapHandle &) = delete;

    MMapHandle(MMapHandle &&other) noexcept
        : map_base(std::exchange(other.map_base, nullptr)),
          map_size(std::exchange(other.map_size, 0)) {}

    MMapHandle &operator=(MMapHandle &&other) noexcept {
      if (this != &other) {
        if (map_base) munmap(map_base, map_size);
        map_base = std::exchange(other.map_base, nullptr);
        map_size = std::exchange(other.map_size, 0);
      }
      return *this;
    }
  };

  MMapHandle mmap_handle_{};
  const char *cursor_{nullptr};
  const char *limit_{nullptr};
  const char *window_{nullptr};
  uint64_t alnum_{0};
  uint64_t digit_{0};
  std::vector<Token> tokens_{};
  uint32_t current_line_{1};
  bool had_error_{false};
  bool open_ok_{false};

  static constexpr unsigned avail_at(size_t off) noexcept {
    return static_cast<unsigned>(64 - off);
  }

  static constexpr unsigned run_at(uint64_t mask, size_t off) noexcept {
    const unsigned avail = avail_at(off);
    const unsigned run = static_cast<unsigned>(std::countr_zero(~(mask >> off)));
    return run < avail ? run : avail;
  }

  bool in_window() const noexcept {
    return cursor_ >= window_ && cursor_ < window_ + 64;
  }

  inline void refresh() noexcept {
    window_ = cursor_;
    const __m512i chars = _mm512_loadu_si512(cursor_);
    const uint64_t under =
        _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('_'));
    alnum_ = ascii::is_alnum_512(chars) | under;
    digit_ = ascii::is_digit_512(chars);
  }

  inline void skip_whitespace() noexcept {
    while (cursor_ < limit_ && ascii::is_space(*cursor_)) {
      if (*cursor_ == '\n') current_line_++;
      cursor_++;
    }
  }

  inline void skip_line_comment() noexcept {
    while (cursor_ < limit_ && *cursor_ != '\n') cursor_++;
  }

  inline void skip_block_comment() noexcept {
    while (cursor_ < limit_) {
      if (*cursor_ == '\n') {
        current_line_++;
        cursor_++;
      } else if (*cursor_ == '*' && cursor_[1] == '/') {
        cursor_ += 2;
        return;
      } else {
        cursor_++;
      }
    }
    had_error_ = true;
  }

  inline void read_identifier_or_keyword() noexcept {
    const char *start = cursor_;
    while (cursor_ < limit_) {
      if (!in_window())
        refresh();
      const size_t off = static_cast<size_t>(cursor_ - window_);
      cursor_ += run_at(alnum_, off);
      if (cursor_ < window_ + 64)
        break;
    }
    if (cursor_ > limit_)
      cursor_ = limit_;

    const size_t len = cursor_ - start;
    tokens_.emplace_back(detail::keyword_map_.lookup(start, len),
                         std::string_view(start, len), current_line_);
  }

  inline void read_constant() noexcept {
    const char *start = cursor_;
    while (cursor_ < limit_) {
      if (!in_window())
        refresh();
      const size_t off = static_cast<size_t>(cursor_ - window_);
      cursor_ += run_at(digit_, off);
      if (cursor_ < window_ + 64)
        break;
    }
    if (cursor_ > limit_)
      cursor_ = limit_;

    if (cursor_ < limit_ && (ascii::is_alpha(*cursor_) || *cursor_ == '_')) {
      had_error_ = true;
      while (cursor_ < limit_ && (ascii::is_alnum(*cursor_) || *cursor_ == '_'))
        ++cursor_;
      tokens_.emplace_back(TokenType::Error,
                           std::string_view(start, cursor_ - start),
                           current_line_);
      return;
    }

    tokens_.emplace_back(TokenType::Constant,
                         std::string_view(start, cursor_ - start),
                         current_line_);
  }

  inline void emit(TokenType type, size_t len) noexcept {
    tokens_.emplace_back(type, std::string_view(cursor_, len), current_line_);
    cursor_ += len;
  }

public:
  explicit Lexer(const std::filesystem::path &path) {
    struct UniqueFd {
      int fd{-1};
      explicit UniqueFd(int f) : fd(f) {}
      ~UniqueFd() { if (fd >= 0) close(fd); }
      operator int() const { return fd; }
    };

    UniqueFd fd{open(path.c_str(), O_RDONLY)};
    if (fd < 0) return;

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < 0) return;

    const size_t size = static_cast<size_t>(st.st_size);
    const long page_size_l = sysconf(_SC_PAGESIZE);
    const size_t page_size = page_size_l > 0 ? static_cast<size_t>(page_size_l) : 4096;

    const size_t file_pages = (size + page_size - 1) / page_size;
    const size_t map_size = (file_pages + 1) * page_size;

    void *base = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return;

    mmap_handle_ = MMapHandle{base, map_size};

    if (size > 0) {
      void *filemap = mmap(base, size, PROT_READ, MAP_PRIVATE | MAP_FIXED | MAP_POPULATE, fd, 0);
      if (filemap == MAP_FAILED) {
        mmap_handle_ = MMapHandle{};
        return;
      }
    }

    madvise(base, size, MADV_SEQUENTIAL | MADV_WILLNEED);

    cursor_ = static_cast<const char *>(base);
    limit_ = cursor_ + size;
    open_ok_ = true;
  }

  Lexer(const Lexer &) = delete;
  Lexer &operator=(const Lexer &) = delete;

  Lexer(Lexer &&other) noexcept
      : mmap_handle_(std::move(other.mmap_handle_)),
        cursor_(std::exchange(other.cursor_, nullptr)),
        limit_(std::exchange(other.limit_, nullptr)),
        window_(std::exchange(other.window_, nullptr)),
        alnum_(std::exchange(other.alnum_, 0)),
        digit_(std::exchange(other.digit_, 0)),
        tokens_(std::move(other.tokens_)),
        current_line_(std::exchange(other.current_line_, 1)),
        had_error_(std::exchange(other.had_error_, false)),
        open_ok_(std::exchange(other.open_ok_, false)) {}

  Lexer &operator=(Lexer &&other) noexcept {
    if (this != &other) {
      mmap_handle_ = std::move(other.mmap_handle_);
      cursor_ = std::exchange(other.cursor_, nullptr);
      limit_ = std::exchange(other.limit_, nullptr);
      window_ = std::exchange(other.window_, nullptr);
      alnum_ = std::exchange(other.alnum_, 0);
      digit_ = std::exchange(other.digit_, 0);
      tokens_ = std::move(other.tokens_);
      current_line_ = std::exchange(other.current_line_, 1);
      had_error_ = std::exchange(other.had_error_, false);
      open_ok_ = std::exchange(other.open_ok_, false);
    }
    return *this;
  }

  void lex() {
    tokens_.clear();
    tokens_.reserve(static_cast<size_t>(limit_ - cursor_) / 3 + 16);

    had_error_ = false;
    current_line_ = 1;
    cursor_ = static_cast<const char *>(mmap_handle_.map_base);
    refresh();

    while (true) {
      skip_whitespace();
      if (cursor_ >= limit_)
        break;

      const char c = *cursor_;

      if (ascii::is_alpha(c) || c == '_') {
        read_identifier_or_keyword();
      } else if (ascii::is_digit(c)) {
        read_constant();
      } else {
        switch (c) {
        case '#':
          skip_line_comment();
          break;
        case '/':
          if (cursor_[1] == '/') {
            cursor_ += 2;
            skip_line_comment();
          } else if (cursor_[1] == '*') {
            cursor_ += 2;
            skip_block_comment();
          } else if (cursor_[1] == '=') {
            emit(TokenType::DivideAssign, 2);
          } else {
            emit(TokenType::Divide, 1);
          }
          break;
        case '-':
          if (cursor_[1] == '-')
            emit(TokenType::Decrement, 2);
          else if (cursor_[1] == '=')
            emit(TokenType::SubtractAssign, 2);
          else
            emit(TokenType::Subtract, 1);
          break;
        case '!':
          if (cursor_[1] == '=')
            emit(TokenType::NotEqual, 2);
          else
            emit(TokenType::Not, 1);
          break;
        case '&':
          if (cursor_[1] == '&')
            emit(TokenType::And, 2);
          else if (cursor_[1] == '=')
            emit(TokenType::BitwiseAndAssign, 2);
          else
            emit(TokenType::BitwiseAnd, 1);
          break;
        case '|':
          if (cursor_[1] == '|')
            emit(TokenType::Or, 2);
          else if (cursor_[1] == '=')
            emit(TokenType::BitwiseOrAssign, 2);
          else
            emit(TokenType::BitwiseOr, 1);
          break;
        case '=':
          if (cursor_[1] == '=')
            emit(TokenType::Equal, 2);
          else
            emit(TokenType::Assign, 1);
          break;
        case '<':
          if (cursor_[1] == '<' && cursor_[2] == '=')
            emit(TokenType::ShiftLeftAssign, 3);
          else if (cursor_[1] == '=')
            emit(TokenType::LessOrEqual, 2);
          else if (cursor_[1] == '<')
            emit(TokenType::ShiftLeft, 2);
          else
            emit(TokenType::LessThan, 1);
          break;
        case '>':
          if (cursor_[1] == '>' && cursor_[2] == '=')
            emit(TokenType::ShiftRightAssign, 3);
          else if (cursor_[1] == '=')
            emit(TokenType::GreaterOrEqual, 2);
          else if (cursor_[1] == '>')
            emit(TokenType::ShiftRight, 2);
          else
            emit(TokenType::GreaterThan, 1);
          break;
        case '^':
          if (cursor_[1] == '=')
            emit(TokenType::BitwiseXorAssign, 2);
          else
            emit(TokenType::BitwiseXor, 1);
          break;
        case '(':
          emit(TokenType::LParen, 1);
          break;
        case ')':
          emit(TokenType::RParen, 1);
          break;
        case '{':
          emit(TokenType::LBrace, 1);
          break;
        case '}':
          emit(TokenType::RBrace, 1);
          break;
        case ';':
          emit(TokenType::Semicolon, 1);
          break;
        case '?':
          emit(TokenType::QuestionMark, 1);
          break;
        case ':':
          emit(TokenType::Colon, 1);
          break;
        case '~':
          emit(TokenType::Complement, 1);
          break;
        case ',':
          emit(TokenType::Comma, 1);
          break;
        case '+':
          if (cursor_[1] == '+')
            emit(TokenType::Increment, 2);
          else if (cursor_[1] == '=')
            emit(TokenType::AddAssign, 2);
          else
            emit(TokenType::Add, 1);
          break;
        case '*':
          if (cursor_[1] == '=')
            emit(TokenType::MultiplyAssign, 2);
          else
            emit(TokenType::Multiply, 1);
          break;
        case '%':
          if (cursor_[1] == '=')
            emit(TokenType::RemainderAssign, 2);
          else
            emit(TokenType::Remainder, 1);
          break;
        default:
          had_error_ = true;
          emit(TokenType::Error, 1);
          break;
        }
      }
    }

    tokens_.emplace_back(TokenType::Eof, std::string_view{}, current_line_);
  }

  [[nodiscard]] bool ok() const noexcept { return !had_error_ && open_ok_; }
  [[nodiscard]] const std::vector<Token> &get_tokens() const noexcept {
    return tokens_;
  }
};

}
