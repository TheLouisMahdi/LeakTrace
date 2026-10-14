/* SPDX-License-Identifier: MIT
 * OPTIONAL adapter for libc allocations. Do not combine it with FreeRTOS
 * heap tracing when libc malloc/calloc/realloc/free delegate to pvPortMalloc.
 * Compile LeakTrace_Stdlib.c only if these wrappers are actually used.
 */
#ifndef LEAKTRACE_STDLIB_H
#define LEAKTRACE_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *lt_malloc(size_t bytes);
void *lt_calloc(size_t count, size_t bytes);
void *lt_realloc(void *ptr, size_t bytes);
void lt_free(void *ptr);
#ifdef __cplusplus
}
#endif
#endif
