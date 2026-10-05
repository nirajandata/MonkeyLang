#include "monkey_flex_lexer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "monkey_flex_scanner.h"

extern "C" int monkey_flex_saw_error;

namespace {

std::string &error_slot() {
  static std::string error;
  return error;
}

struct UniqueFd {
  int fd{-1};
  explicit UniqueFd(int f) : fd(f) {}
  ~UniqueFd() {
    if (fd >= 0) ::close(fd);
  }
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  operator int() const { return fd; }
};

} // namespace

struct monkey_flex_lexer {
  void *base{nullptr};
  size_t map_size{0};
  size_t size{0};
  yyscan_t scanner{nullptr};
  int had_error{0};
  bool open_ok{false};
};

const char *monkey_flex_open_error(void) { return error_slot().c_str(); }

size_t monkey_flex_size(const monkey_flex_lexer *lexer) {
  return lexer != nullptr ? lexer->size : 0;
}

monkey_flex_lexer *monkey_flex_open(const char *path) {
  error_slot().clear();

  UniqueFd fd{::open(path, O_RDONLY)};
  if (fd < 0) {
    error_slot() = "cannot open '" + std::string(path) + "': " +
                   std::strerror(errno);
    return nullptr;
  }

  struct stat st {};
  if (::fstat(fd, &st) != 0 || st.st_size < 0) {
    error_slot() = "cannot stat '" + std::string(path) + "'";
    return nullptr;
  }

  const size_t size = static_cast<size_t>(st.st_size);
  const long page_size_l = ::sysconf(_SC_PAGESIZE);
  const size_t page_size =
      page_size_l > 0 ? static_cast<size_t>(page_size_l) : size_t{4096};

  const size_t file_pages = (size + page_size - 1) / page_size;
  const size_t map_size = (file_pages + 1) * page_size;

  void *base = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) {
    error_slot() = "mmap failed: " + std::string(std::strerror(errno));
    return nullptr;
  }

  if (size > 0) {
    void *filemap = ::mmap(base, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_FIXED | MAP_POPULATE, fd, 0);
    if (filemap == MAP_FAILED) {
      ::munmap(base, map_size);
      error_slot() = "file mmap failed: " + std::string(std::strerror(errno));
      return nullptr;
    }
  }

  ::madvise(base, size, MADV_SEQUENTIAL | MADV_WILLNEED);

  auto *lexer = new monkey_flex_lexer();
  lexer->base = base;
  lexer->map_size = map_size;
  lexer->size = size;

  if (::yylex_init(&lexer->scanner) != 0) {
    ::munmap(base, map_size);
    delete lexer;
    error_slot() = "yylex_init failed";
    return nullptr;
  }
  lexer->open_ok = true;
  return lexer;
}

int monkey_flex_lex(monkey_flex_lexer *lexer, monkey_flex_token *out, size_t cap,
                    size_t *count, int *had_error) {
  if (lexer == nullptr || !lexer->open_ok) {
    error_slot() = "lexer is not open";
    return -2;
  }

  if (lexer->size + 2 > lexer->map_size) {
    error_slot() = "mapping is too small for the flex EOB pair";
    return -2;
  }

  YY_BUFFER_STATE buffer = ::yy_scan_buffer(static_cast<char *>(lexer->base),
                                           lexer->size + 2, lexer->scanner);
  if (buffer == nullptr) {
    error_slot() = "yy_scan_buffer rejected the mapping";
    return -2;
  }
  ::yyset_lineno(1, lexer->scanner);

  monkey_flex_saw_error = 0;

  size_t n = 0;
  int status = 0;
  for (;;) {
    const int type = ::yylex(lexer->scanner);
    if (type == 0)
      break;

    if (n + 1 > cap) {
      status = -1;
      break;
    }

    out[n].type = type;
    out[n].text = ::yyget_text(lexer->scanner);
    out[n].len = static_cast<size_t>(::yyget_leng(lexer->scanner));
    out[n].line = static_cast<uint32_t>(::yyget_lineno(lexer->scanner));
    ++n;
  }

  if (status == 0) {
    if (n + 1 > cap) {
      status = -1;
    } else {
      out[n].type = MONKEY_TT_Eof;
      out[n].text = nullptr;
      out[n].len = 0;
      out[n].line = static_cast<uint32_t>(::yyget_lineno(lexer->scanner));
      ++n;
    }
  }

  lexer->had_error = monkey_flex_saw_error;
  ::yy_delete_buffer(buffer, lexer->scanner);

  *count = n;
  *had_error = lexer->had_error;
  return status;
}

void monkey_flex_close(monkey_flex_lexer *lexer) {
  if (lexer == nullptr)
    return;
  if (lexer->scanner != nullptr)
    ::yylex_destroy(lexer->scanner);
  if (lexer->base != nullptr)
    ::munmap(lexer->base, lexer->map_size);
  delete lexer;
}
