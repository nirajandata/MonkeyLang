#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int monkey_flex_avxkwlex_init(void **scanner);
int monkey_flex_avxkwlex_destroy(void *scanner);
int monkey_flex_avxkwlex(void *scanner);

char *monkey_flex_avxkwget_text(void *scanner);
int monkey_flex_avxkwget_leng(void *scanner);

int monkey_flex_avxkwget_lineno(void *scanner);
void monkey_flex_avxkwset_lineno(int line, void *scanner);

void *monkey_flex_avxkw_scan_buffer(char *base, size_t size, void *scanner);
void monkey_flex_avxkw_delete_buffer(void *buffer, void *scanner);

#ifdef __cplusplus
}
#endif
