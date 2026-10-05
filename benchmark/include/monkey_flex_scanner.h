#pragma once

#include <stddef.h>

#ifndef YY_TYPEDEF_YY_BUFFER_STATE
#define YY_TYPEDEF_YY_BUFFER_STATE
typedef struct yy_buffer_state *YY_BUFFER_STATE;
#endif

typedef void *yyscan_t;

#ifdef __cplusplus
extern "C" {
#endif

int yylex_init(yyscan_t *scanner);
int yylex_destroy(yyscan_t scanner);
int yylex(yyscan_t scanner);

char *yyget_text(yyscan_t scanner);
int yyget_leng(yyscan_t scanner);

int yyget_lineno(yyscan_t scanner);
void yyset_lineno(int line, yyscan_t scanner);

YY_BUFFER_STATE yy_scan_buffer(char *base, size_t size, yyscan_t scanner);
void yy_delete_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);

#ifdef __cplusplus
}
#endif

