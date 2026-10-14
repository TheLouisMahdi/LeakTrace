/* SPDX-License-Identifier: MIT
 * OPTIONAL global new/delete adapter, C++11+.
 * Compile EXACTLY ONCE; do not also trace its underlying malloc/free.
 * C++17 over-aligned new/delete are not intercepted in this adapter.
 * In multitask programs, define LT_CPP_ENTER/EXIT around libc and hook calls.
 */
#include "LeakTrace.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if !defined(__GNUC__) && !defined(__clang__)
#error "Define return address handling for this compiler"
#endif
#define LT_CPP_NOINLINE __attribute__((noinline))
#define LT_CPP_PC() ((uintptr_t)\
    __builtin_extract_return_addr(__builtin_return_address(0)))
/* Override with monotonic ticks if exact age is needed. */
#ifndef LT_CPP_TICK
#define LT_CPP_TICK() lt_last_poll_seconds()
#endif
#ifndef LT_CPP_ENTER
#define LT_CPP_ENTER() ((void)0)
#endif
#ifndef LT_CPP_EXIT
#define LT_CPP_EXIT() ((void)0)
#endif

LT_CPP_NOINLINE void *operator new(std::size_t n)
{
    void *p;
    LT_CPP_ENTER();
    p = std::malloc(n ? n : 1u);
    lt_on_alloc_at(p, n, LT_CPP_PC(), LT_CPP_TICK());
    LT_CPP_EXIT();
    if (!p) {
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return p;
}

LT_CPP_NOINLINE void *operator new[](std::size_t n)
{
    void *p;
    LT_CPP_ENTER();
    p = std::malloc(n ? n : 1u);
    lt_on_alloc_at(p, n, LT_CPP_PC(), LT_CPP_TICK());
    LT_CPP_EXIT();
    if (!p) {
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return p;
}

LT_CPP_NOINLINE void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    void *p;
    LT_CPP_ENTER();
    p = std::malloc(n ? n : 1u);
    lt_on_alloc_at(p, n, LT_CPP_PC(), LT_CPP_TICK());
    LT_CPP_EXIT();
    return p;
}

LT_CPP_NOINLINE void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
    void *p;
    LT_CPP_ENTER();
    p = std::malloc(n ? n : 1u);
    lt_on_alloc_at(p, n, LT_CPP_PC(), LT_CPP_TICK());
    LT_CPP_EXIT();
    return p;
}

void operator delete(void *p) noexcept
{
    LT_CPP_ENTER();
    lt_on_free(p);
    std::free(p);
    LT_CPP_EXIT();
}

void operator delete[](void *p) noexcept
{
    LT_CPP_ENTER();
    lt_on_free(p);
    std::free(p);
    LT_CPP_EXIT();
}

void operator delete(void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    ::operator delete[](p);
}
#if __cplusplus >= 201402L
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete[](p); }
#endif
