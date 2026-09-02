module;

#include <bit>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <immintrin.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

export module lexer;

import token;
import ascii;

export class Lexer {
private:
  struct MMapHandle {
    void *map_base = nullptr;
    size_t map_size = 0;

    MMapHandle() = default;
    MMapHandle(void *base, size_t size) : map_base(base), map_size(size) {}
    ~MMapHandle() {
      if (map_base)
        munmap(map_base, map_size);
    }

    MMapHandle(const MMapHandle &) = delete;
    MMapHandle &operator=(const MMapHandle &) = delete;

    MMapHandle(MMapHandle &&other) noexcept
        : map_base(std::exchange(other.map_base, nullptr)),
          map_size(std::exchange(other.map_size, 0)) {}

    MMapHandle &operator=(MMapHandle &&other) noexcept {
      if (this != &other) {
        if (map_base)
          munmap(map_base, map_size);
        map_base = std::exchange(other.map_base, nullptr);
        map_size = std::exchange(other.map_size, 0);
      }
      return *this;
    }
  };

  MMapHandle mmap_handle_{};
  const char *cursor_ = nullptr;
  const char *limit_ = nullptr;
  std::vector<Token> tokens_{};
  uint32_t current_line_ = 1;
  bool had_error_ = false;
  bool open_ok_ = false;

  inline uint64_t tail_mask() const noexcept {
    uint64_t remaining = static_cast<uint64_t>(limit_ - cursor_);
    return remaining >= 64 ? ~0ULL : ((1ULL << remaining) - 1);
  }

  void skip_whitespace() {
    while (cursor_ < limit_) {
      __m512i chars = _mm512_loadu_si512(cursor_);
      uint64_t mask = tail_mask();

      uint64_t ws = ascii::is_space_512(chars) & mask;
      uint64_t nl =
          _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n')) & mask;
      uint64_t not_ws = (~ws) & mask;

      if (not_ws == 0) {
        current_line_ += std::popcount(nl);
        cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
      } else {
        int offset = std::countr_zero(not_ws);
        uint64_t nl_skipped = nl & ((1ULL << offset) - 1);
        current_line_ += std::popcount(nl_skipped);
        cursor_ += offset;
        return;
      }
    }
  }

  void skip_line_comment() {
    while (cursor_ < limit_) {
      __m512i chars = _mm512_loadu_si512(cursor_);
      uint64_t mask = tail_mask();
      uint64_t nl =
          _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n')) & mask;

      if (nl == 0) {
        cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
      } else {
        cursor_ += std::countr_zero(nl);
        return;
      }
    }
  }

  void skip_block_comment() {
    while (cursor_ + 64 <= limit_) {
      __m512i chars = _mm512_loadu_si512(cursor_);
      uint64_t star = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('*'));
      uint64_t nl = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'));

      if (star == 0) {
        current_line_ += std::popcount(nl);
        cursor_ += 64;
      } else
        break;
    }

    while (cursor_ < limit_) {
      if (*cursor_ == '\n')
        current_line_++;

      if (*cursor_ == '*') {
        cursor_++;
        if (cursor_ < limit_ && *cursor_ == '/') {
          cursor_++;
          return;
        }
      } else
        cursor_++;
    }

    had_error_ = true;
  }

  Token read_identifier_or_keyword() {
    const char *start = cursor_;

    while (cursor_ < limit_) {
      __m512i chars = _mm512_loadu_si512(cursor_);
      uint64_t mask = tail_mask();

      uint64_t is_under = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('_'));
      uint64_t valid = (ascii::is_alnum_512(chars) | is_under) & mask;

      if (valid == mask) {
        cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
      } else {
        cursor_ += std::countr_zero(~valid);
        break;
      }
    }

    std::string_view text(start, cursor_ - start);
    TokenType type = TokenType::Identifier;

    if (text.size() == 3 && text == "int")
      type = TokenType::Int;
    else if (text.size() == 4 && text == "void")
      type = TokenType::Void;
    else if (text.size() == 6 && text == "return")
      type = TokenType::Return;

    return {type, text, current_line_};
  }

  Token read_constant() {
    const char *start = cursor_;

    while (cursor_ < limit_) {
      __m512i chars = _mm512_loadu_si512(cursor_);
      uint64_t mask = tail_mask();
      uint64_t is_digit = ascii::is_digit_512(chars) & mask;

      if (is_digit == mask) {
        cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
      } else {
        cursor_ += std::countr_zero(~is_digit);
        break;
      }
    }

    if (cursor_ < limit_ && (ascii::is_alpha(*cursor_) || *cursor_ == '_')) {
      had_error_ = true;
      while (cursor_ < limit_ && (ascii::is_alnum(*cursor_) || *cursor_ == '_'))
        cursor_++;
      return {TokenType::Error, std::string_view(start, cursor_ - start),
              current_line_};
    }

    return {TokenType::Constant, std::string_view(start, cursor_ - start),
            current_line_};
  }

public:
  explicit Lexer(const std::filesystem::path &path) {
    struct UniqueFd {
      int fd;
      explicit UniqueFd(int f) : fd(f) {}
      ~UniqueFd() {
        if (fd >= 0)
          close(fd);
      }
      operator int() const { return fd; }
    };

    UniqueFd fd{open(path.c_str(), O_RDONLY)};
    if (fd < 0)
      return;

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < 0)
      return;

    size_t size = static_cast<size_t>(st.st_size);
    long page_size_l = sysconf(_SC_PAGESIZE);
    size_t page_size =
        page_size_l > 0 ? static_cast<size_t>(page_size_l) : 4096;

    size_t file_pages = (size + page_size - 1) / page_size;
    size_t map_size = (file_pages + 1) * page_size;

    void *base =
        mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
      return;

    mmap_handle_ = MMapHandle{base, map_size};

    if (size > 0) {
      void *filemap = mmap(base, size, PROT_READ,
                           MAP_PRIVATE | MAP_FIXED | MAP_POPULATE, fd, 0);
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
        tokens_(std::move(other.tokens_)),
        current_line_(std::exchange(other.current_line_, 1)),
        had_error_(std::exchange(other.had_error_, false)),
        open_ok_(std::exchange(other.open_ok_, false)) {}

  Lexer &operator=(Lexer &&other) noexcept {
    if (this != &other) {
      mmap_handle_ = std::move(other.mmap_handle_);
      cursor_ = std::exchange(other.cursor_, nullptr);
      limit_ = std::exchange(other.limit_, nullptr);
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

    auto emit = [&](TokenType type, size_t len) {
      tokens_.emplace_back(type, std::string_view(cursor_, len), current_line_);
      cursor_ += len;
    };

    auto match = [&](char expected) -> bool {
      return (cursor_ + 1 < limit_ && cursor_[1] == expected);
    };

    while (cursor_ < limit_) {
      skip_whitespace();
      if (cursor_ >= limit_)
        break;

      char c = *cursor_;

      if (ascii::is_alpha(c) || c == '_') {
        tokens_.emplace_back(read_identifier_or_keyword());
      } else if (ascii::is_digit(c)) {
        tokens_.emplace_back(read_constant());
      } else {
        switch (c) {
        case '#':
          skip_line_comment();
          break;
        case '/':
          if (match('/')) {
            cursor_ += 2;
            skip_line_comment();
          } else if (match('*')) {
            cursor_ += 2;
            skip_block_comment();
          } else
            emit(TokenType::Divide, 1);
          break;
        case '-':
          if (match('-'))
            emit(TokenType::Decrement, 2);
          else
            emit(TokenType::Subtract, 1);
          break;
        case '!':
          if (match('='))
            emit(TokenType::NotEqual, 2);
          else
            emit(TokenType::Not, 1);
          break;
        case '&':
          if (match('&'))
            emit(TokenType::And, 2);
          else
            emit(TokenType::BitwiseAnd, 1);
          break;
        case '|':
          if (match('|'))
            emit(TokenType::Or, 2);
          else
            emit(TokenType::BitwiseOr, 1);
          break;
        case '=':
          if (match('='))
            emit(TokenType::Equal, 2);
          else {
            emit(TokenType::Assign,1);
          }
          break;
        case '<':
          if (match('='))
            emit(TokenType::LessOrEqual, 2);
          else if (match('<'))
            emit(TokenType::ShiftLeft, 2);
          else
            emit(TokenType::LessThan, 1);
          break;
        case '>':
          if (match('='))
            emit(TokenType::GreaterOrEqual, 2);
          else if (match('>'))
            emit(TokenType::ShiftRight, 2);
          else
            emit(TokenType::GreaterThan, 1);
          break;
        case '^':
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
        case '~':
          emit(TokenType::Complement, 1);
          break;
        case '+':
          emit(TokenType::Add, 1);
          break;
        case '*':
          emit(TokenType::Multiply, 1);
          break;
        case '%':
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