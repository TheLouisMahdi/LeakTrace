/* SPDX-License-Identifier: MIT
 * Example profile for an existing STM32H743 + FreeRTOS heap_4 application.
 * Select through LT_USER_CONFIG_HEADER in every C/C++ compiler invocation.
 */
#ifndef LEAKTRACE_H743_CONFIG_H
#define LEAKTRACE_H743_CONFIG_H

#ifndef LT_MAX_LIVE
#define LT_MAX_LIVE 256u
#endif
#ifndef LT_BUCKETS
#define LT_BUCKETS 128u
#endif
#ifndef LT_PACKED
#define LT_PACKED 0
#endif
#ifndef LT_FAST_SLOTS
#define LT_FAST_SLOTS 120u
#endif
#ifndef LT_FAST_SECONDS
#define LT_FAST_SECONDS 10u
#endif
#ifndef LT_SLOW_SLOTS
#define LT_SLOW_SLOTS 2880u
#endif
#ifndef LT_SLOW_SECONDS
#define LT_SLOW_SECONDS 60u
#endif
#ifndef LT_HISTORY_QUANTUM
#define LT_HISTORY_QUANTUM 32u
#endif
#ifndef LT_TOP_SUSPECTS
#define LT_TOP_SUSPECTS 8u
#endif
#ifndef LT_SUSPECT_SECONDS
#define LT_SUSPECT_SECONDS 1200u
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#error "The H743 example uses GCC/Clang section attributes"
#endif
#define LT_STATE_ATTRIBUTE \
    __attribute__((section(".lt_monitor.core"), aligned(32)))

/* Change these together with the application's FreeRTOSConfig.h. */
#define LT_H743_HEAP_BYTES 65536u
#define LT_H743_MONITOR_STACK_WORDS 512u
#define LT_H743_WORKER_STACK_WORDS 256u

#ifdef __cplusplus
extern "C" {
#endif
uint32_t LT_H743_Seconds(void);
#ifdef __cplusplus
}
#endif
#define LT_FREERTOS_SECONDS() LT_H743_Seconds()

#endif
