/* SPDX-License-Identifier: MIT
 * FreeRTOS heap_4/heap_5 adapter. Include near END of FreeRTOSConfig.h.
 * Do NOT combine with a second hook on the same allocations.
 * Caller PC is the caller of pvPortMalloc when compiler preserves LR.
 * Never allocate from ISR; all monitor operations require serialization.
 */
#ifndef LEAKTRACE_FREERTOS_H
#define LEAKTRACE_FREERTOS_H
#include "LeakTrace.h"
#if defined(traceMALLOC) || defined(traceFREE)
#error "Chain existing traceMALLOC/traceFREE manually; do not silently override"
#endif
#if defined(configUSE_16_BIT_TICKS) && (configUSE_16_BIT_TICKS != 0)
#error "LeakTrace FreeRTOS adapter requires 32-bit ticks"
#endif
#ifndef LT_FREERTOS_PC
#if defined(__GNUC__) || defined(__clang__)
#define LT_FREERTOS_PC() ((uintptr_t) \
    __builtin_extract_return_addr(__builtin_return_address(0)))
#else
#error "Provide LT_FREERTOS_PC() for this compiler"
#endif
#endif
/* Clock in SECONDS. Passes through RTOS tick wrap after ~49.7 days at 1kHz.
 * For longer runs use an independent monotonic seconds counter instead.
 */
#ifndef LT_FREERTOS_SECONDS
#define LT_FREERTOS_SECONDS() ((uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ))
#endif
#define traceMALLOC(ptr, bytes) do { \
    lt_on_alloc_at((ptr), (size_t)(bytes), LT_FREERTOS_PC(), \
                   LT_FREERTOS_SECONDS()); \
} while (0)
#define traceFREE(ptr, bytes) do { \
    (void)(bytes); lt_on_free((ptr)); \
} while (0)
#endif
