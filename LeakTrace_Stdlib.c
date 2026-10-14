/* SPDX-License-Identifier: MIT
 * Compile as a separate source so the captured return address points at the
 * call site in application code. Lock must cover the allocator AND the hook.
 * Single-task code needs no lock; multi-task code MUST define these macros.
 * Do not call these from an ISR.
 */
#include "LeakTrace_Stdlib.h"
#include "LeakTrace.h"
#include <stdint.h>
#include <stdlib.h>

/* Override with monotonic FreeRTOS ticks when using this adapter. */
#ifndef LT_STDLIB_TICK
#define LT_STDLIB_TICK() lt_last_poll_seconds()
#endif
#ifndef LT_STDLIB_ENTER
#define LT_STDLIB_ENTER() ((void)0)
#endif
#ifndef LT_STDLIB_EXIT
#define LT_STDLIB_EXIT() ((void)0)
#endif
#if defined(__GNUC__) || defined(__clang__)
#define LT_STDLIB_NOINLINE __attribute__((noinline))
#define LT_STDLIB_PC() ((uintptr_t)\
    __builtin_extract_return_addr(__builtin_return_address(0)))
#else
#error "Define a supported compiler-specific return-PC capture for stdlib adapter"
#endif

LT_STDLIB_NOINLINE void *lt_malloc(size_t bytes)
{
    void *p;
    LT_STDLIB_ENTER();
    p = malloc(bytes);
    lt_on_alloc_at(p, bytes, LT_STDLIB_PC(), LT_STDLIB_TICK());
    LT_STDLIB_EXIT();
    return p;
}

LT_STDLIB_NOINLINE void *lt_calloc(size_t count, size_t bytes)
{
    void *p;
    const size_t total = count && bytes > SIZE_MAX / count ? SIZE_MAX : count * bytes;
    LT_STDLIB_ENTER();
    p = calloc(count, bytes);
    lt_on_alloc_at(p, total, LT_STDLIB_PC(), LT_STDLIB_TICK());
    LT_STDLIB_EXIT();
    return p;
}

LT_STDLIB_NOINLINE void *lt_realloc(void *ptr, size_t bytes)
{
    void *p;
    volatile uintptr_t old_address;
    if (bytes == 0u) {
        lt_free(ptr); /* Stable, documented alternative to realloc(p, 0). */
        return NULL;
    }
    old_address = (uintptr_t)ptr;
    LT_STDLIB_ENTER();
    p = realloc(ptr, bytes);
    if (p) {
        if (old_address) lt_on_free((void *)old_address);
        lt_on_alloc_at(p, bytes, LT_STDLIB_PC(), LT_STDLIB_TICK());
    } else {
        lt_on_alloc_at(NULL, bytes, LT_STDLIB_PC(), LT_STDLIB_TICK());
    }
    LT_STDLIB_EXIT();
    return p;
}

LT_STDLIB_NOINLINE void lt_free(void *ptr)
{
    LT_STDLIB_ENTER();
    lt_on_free(ptr);
    free(ptr);
    LT_STDLIB_EXIT();
}
