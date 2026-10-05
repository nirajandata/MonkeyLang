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

typedef struct monkey_flex_lexer monkey_flex_lexer;

monkey_flex_lexer *monkey_flex_open(const char *path);
const char *monkey_flex_open_error(void);

size_t monkey_flex_size(const monkey_flex_lexer *lexer);

int monkey_flex_lex(monkey_flex_lexer *lexer, monkey_flex_token *out, size_t cap,
                    size_t *count, int *had_error);

void monkey_flex_close(monkey_flex_lexer *lexer);

#ifdef __cplusplus
}
#endif

