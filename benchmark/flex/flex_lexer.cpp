#include "monkey_flex_lexer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "monkey_flex_scanner.h"
#include "monkey_flex_scanner_avxkw.h"
#include "monkey_flex_scanner_cf.h"
#include "monkey_re2c_scanner.h"

extern "C" int monkey_flex_saw_error;
extern "C" int monkey_flex_avxkw_saw_error;
extern "C" int monkey_flex_cf_saw_error;
extern "C" int monkey_re2c_saw_error;

namespace {

std::string &error_slot() {
  static std::string error;
  return error;
}

void *base_scan_buffer(char *base, size_t size, void *scanner) {
  return yy_scan_buffer(base, size, scanner);
}

void base_delete_buffer(void *buffer, void *scanner) {
  yy_delete_buffer(static_cast<YY_BUFFER_STATE>(buffer), scanner);
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

}

extern "C" const struct monkey_flex_api monkey_flex_api_base = {
    .lex_init = yylex_init,
    .lex_destroy = yylex_destroy,
    .lex = yylex,
    .get_text = yyget_text,
    .get_leng = yyget_leng,
    .get_lineno = yyget_lineno,
    .set_lineno = yyset_lineno,
    .scan_buffer = base_scan_buffer,
    .delete_buffer = base_delete_buffer,
};

extern "C" const struct monkey_flex_api monkey_flex_api_avxkw = {
    .lex_init = monkey_flex_avxkwlex_init,
    .lex_destroy = monkey_flex_avxkwlex_destroy,
    .lex = monkey_flex_avxkwlex,
    .get_text = monkey_flex_avxkwget_text,
    .get_leng = monkey_flex_avxkwget_leng,
    .get_lineno = monkey_flex_avxkwget_lineno,
    .set_lineno = monkey_flex_avxkwset_lineno,
    .scan_buffer = monkey_flex_avxkw_scan_buffer,
    .delete_buffer = monkey_flex_avxkw_delete_buffer,
};

extern "C" const struct monkey_flex_api monkey_flex_api_cf = {
    .lex_init = monkey_flex_cflex_init,
    .lex_destroy = monkey_flex_cflex_destroy,
    .lex = monkey_flex_cflex,
    .get_text = monkey_flex_cfget_text,
    .get_leng = monkey_flex_cfget_leng,
    .get_lineno = monkey_flex_cfget_lineno,
    .set_lineno = monkey_flex_cfset_lineno,
    .scan_buffer = monkey_flex_cf_scan_buffer,
    .delete_buffer = monkey_flex_cf_delete_buffer,
};

extern "C" const struct monkey_flex_api monkey_flex_api_re2c = {
    .lex_init = monkey_re2c_lex_init,
    .lex_destroy = monkey_re2c_lex_destroy,
    .lex = monkey_re2c_lex,
    .get_text = monkey_re2c_get_text,
    .get_leng = monkey_re2c_get_leng,
    .get_lineno = monkey_re2c_get_lineno,
    .set_lineno = monkey_re2c_set_lineno,
    .scan_buffer = monkey_re2c_scan_buffer,
    .delete_buffer = monkey_re2c_delete_buffer,
};

struct monkey_flex_lexer {
  void *base{nullptr};
  size_t map_size{0};
  size_t size{0};
  void *scanner{nullptr};
  int *saw_error{nullptr};
  int had_error{0};
  bool open_ok{false};
};

extern "C" const char *monkey_flex_open_error(void) {
  return error_slot().c_str();
}

extern "C" size_t monkey_flex_size(const struct monkey_flex_api *,
                                   const monkey_flex_lexer *lexer) {
  return lexer != nullptr ? lexer->size : 0;
}

extern "C" monkey_flex_lexer *monkey_flex_open(const char *path) {
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
  lexer->open_ok = true;
  return lexer;
}

extern "C" int monkey_flex_lex(const struct monkey_flex_api *api,
                               monkey_flex_lexer *lexer, monkey_flex_token *out,
                               size_t cap, size_t *count, int *had_error) {
  if (api == nullptr || lexer == nullptr || !lexer->open_ok) {
    error_slot() = "lexer is not open";
    return -2;
  }

  if (lexer->size + 2 > lexer->map_size) {
    error_slot() = "mapping is too small for the flex EOB pair";
    return -2;
  }

  if (lexer->scanner == nullptr) {
    if (api->lex_init(&lexer->scanner) != 0) {
      error_slot() = "lex_init failed";
      return -2;
    }
    if (api == &monkey_flex_api_avxkw)
      lexer->saw_error = &monkey_flex_avxkw_saw_error;
    else if (api == &monkey_flex_api_cf)
      lexer->saw_error = &monkey_flex_cf_saw_error;
    else if (api == &monkey_flex_api_re2c)
      lexer->saw_error = &monkey_re2c_saw_error;
    else
      lexer->saw_error = &monkey_flex_saw_error;
  }

  void *buffer = api->scan_buffer(static_cast<char *>(lexer->base),
                                 lexer->size + 2, lexer->scanner);
  if (buffer == nullptr) {
    error_slot() = "scan_buffer rejected the mapping";
    return -2;
  }
  api->set_lineno(1, lexer->scanner);

  *lexer->saw_error = 0;

  size_t n = 0;
  int status = 0;
  for (;;) {
    const int type = api->lex(lexer->scanner);
    if (type == 0)
      break;

    if (n + 1 > cap) {
      status = -1;
      break;
    }

    out[n].type = type;
    out[n].text = api->get_text(lexer->scanner);
    out[n].len = static_cast<size_t>(api->get_leng(lexer->scanner));
    out[n].line = static_cast<uint32_t>(api->get_lineno(lexer->scanner));
    ++n;
  }

  if (status == 0) {
    if (n + 1 > cap) {
      status = -1;
    } else {
      out[n].type = MONKEY_TT_Eof;
      out[n].text = nullptr;
      out[n].len = 0;
      out[n].line = static_cast<uint32_t>(api->get_lineno(lexer->scanner));
      ++n;
    }
  }

  lexer->had_error = *lexer->saw_error;
  api->delete_buffer(buffer, lexer->scanner);

  *count = n;
  *had_error = lexer->had_error;
  return status;
}

extern "C" void monkey_flex_close(const struct monkey_flex_api *api,
                                  monkey_flex_lexer *lexer) {
  if (lexer == nullptr)
    return;
  if (lexer->scanner != nullptr && api != nullptr)
    api->lex_destroy(lexer->scanner);
  if (lexer->base != nullptr)
    ::munmap(lexer->base, lexer->map_size);
  delete lexer;
}