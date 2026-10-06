#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int monkey_flex_cflex_init(void **scanner);
int monkey_flex_cflex_destroy(void *scanner);
int monkey_flex_cflex(void *scanner);

char *monkey_flex_cfget_text(void *scanner);
int monkey_flex_cfget_leng(void *scanner);

int monkey_flex_cfget_lineno(void *scanner);
void monkey_flex_cfset_lineno(int line, void *scanner);

void *monkey_flex_cf_scan_buffer(char *base, size_t size, void *scanner);
void monkey_flex_cf_delete_buffer(void *buffer, void *scanner);

#ifdef __cplusplus
}
#endif
