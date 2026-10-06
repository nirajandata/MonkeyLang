#pragma once

#include <stddef.h>
#include <stdint.h>

#include "monkey_token_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct monkey_flex_token {
  int32_t type;
  const char *text;
  size_t len;
  uint32_t line;
} monkey_flex_token;

struct monkey_flex_api {
  int (*lex_init)(void **scanner);
  int (*lex_destroy)(void *scanner);
  int (*lex)(void *scanner);
  char *(*get_text)(void *scanner);
  int (*get_leng)(void *scanner);
  int (*get_lineno)(void *scanner);
  void (*set_lineno)(int line, void *scanner);
  void *(*scan_buffer)(char *base, size_t size, void *scanner);
  void (*delete_buffer)(void *buffer, void *scanner);
};

extern const struct monkey_flex_api monkey_flex_api_base;
extern const struct monkey_flex_api monkey_flex_api_avxkw;
extern const struct monkey_flex_api monkey_flex_api_cf;
extern const struct monkey_flex_api monkey_flex_api_re2c;

typedef struct monkey_flex_lexer monkey_flex_lexer;

monkey_flex_lexer *monkey_flex_open(const char *path);
const char *monkey_flex_open_error(void);

size_t monkey_flex_size(const struct monkey_flex_api *api,
                        const monkey_flex_lexer *lexer);

int monkey_flex_lex(const struct monkey_flex_api *api, monkey_flex_lexer *lexer,
                    monkey_flex_token *out, size_t cap, size_t *count,
                    int *had_error);

void monkey_flex_close(const struct monkey_flex_api *api,
                       monkey_flex_lexer *lexer);

#ifdef __cplusplus
}
#endif