#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct monkey_re2c_scanner {
  const char *cursor;
  const char *end;
  const char *limit;
  const char *token;
  size_t len;
  int lineno;
  int ready;
};

extern int monkey_re2c_saw_error;

int monkey_re2c_lex_init(void **scanner);
int monkey_re2c_lex_destroy(void *scanner);
int monkey_re2c_lex(void *scanner);

char *monkey_re2c_get_text(void *scanner);
int monkey_re2c_get_leng(void *scanner);
int monkey_re2c_get_lineno(void *scanner);
void monkey_re2c_set_lineno(int line, void *scanner);

void *monkey_re2c_scan_buffer(char *base, size_t size, void *scanner);
void monkey_re2c_delete_buffer(void *buffer, void *scanner);

#ifdef __cplusplus
}
#endif