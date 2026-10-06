#include "monkey_re2c_scanner.h"


#include <stdlib.h>
#include <string.h>

struct monkey_re2c_buffer {
  char *base;
  size_t size;
};

int monkey_re2c_lex_init(void **scanner) {
  struct monkey_re2c_scanner *sc =
      (struct monkey_re2c_scanner *)calloc(1, sizeof(*sc));
  if (sc == 0)
    return 1;
  sc->cursor = 0;
  sc->end = 0;
  sc->limit = 0;
  sc->token = 0;
  sc->len = 0;
  sc->lineno = 1;
  sc->ready = 0;
  *scanner = sc;
  return 0;
}

int monkey_re2c_lex_destroy(void *scanner) {
  free(scanner);
  return 0;
}

char *monkey_re2c_get_text(void *scanner) {
  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  return (char *)sc->token;
}

int monkey_re2c_get_leng(void *scanner) {
  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  return (int)sc->len;
}

int monkey_re2c_get_lineno(void *scanner) {
  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  return sc->lineno;
}

void monkey_re2c_set_lineno(int line, void *scanner) {
  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  sc->lineno = line;
}

void *monkey_re2c_scan_buffer(char *base, size_t size, void *scanner) {
  if (base == 0 || scanner == 0 || size < 2)
    return 0;

  const size_t data_size = size - 2;
  if (base[data_size] != 0 || base[data_size + 1] != 0)
    return 0;

  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  struct monkey_re2c_buffer *buf =
      (struct monkey_re2c_buffer *)malloc(sizeof(*buf));
  if (buf == 0)
    return 0;
  buf->base = base;
  buf->size = data_size;

  sc->cursor = base;
  sc->limit = base + data_size + 2;
  sc->end = base + data_size;
  sc->token = base;
  sc->len = 0;
  sc->lineno = 1;
  sc->ready = 1;
  return buf;
}

void monkey_re2c_delete_buffer(void *buffer, void *scanner) {
  (void)scanner;
  free(buffer);
}