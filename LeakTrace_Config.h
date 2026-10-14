/* SPDX-License-Identifier: MIT
 * Shared configuration. No device, HAL, RTOS or memory-map dependency.
 * Override these defaults here or consistently in every translation unit.
 */
#ifndef LEAKTRACE_CONFIG_H
#define LEAKTRACE_CONFIG_H

#ifndef LT_MAX_LIVE
#define LT_MAX_LIVE 64u
#endif
#ifndef LT_BUCKETS
#define LT_BUCKETS 32u
#endif
/* Full addresses, 32-bit block sizes and second-resolution timestamps.
 * Optional LT_PACKED=1 saves RAM but clips block sizes to 65535 bytes and
 * stores caller offsets relative to LT_TEXT_BASE with 10-second timestamps.
 */
#ifndef LT_PACKED
#define LT_PACKED 0
#endif
#ifndef LT_FAST_SLOTS
#define LT_FAST_SLOTS 60u
#endif
#ifndef LT_FAST_SECONDS
#define LT_FAST_SECONDS 10u
#endif
#ifndef LT_SLOW_SLOTS
#define LT_SLOW_SLOTS 180u
#endif
#ifndef LT_SLOW_SECONDS
#define LT_SLOW_SECONDS 60u
#endif
#ifndef LT_HISTORY_QUANTUM
#define LT_HISTORY_QUANTUM 32u
#endif
#ifndef LT_SUSPECT_SECONDS
#define LT_SUSPECT_SECONDS 1200u
#endif
#ifndef LT_TOP_SUSPECTS
#define LT_TOP_SUSPECTS 4u
#endif

/* Optional platform hooks belong here, not in the core:
 * LT_STATE_ATTRIBUTE: linker section/alignment for g_lt.
 * LT_FREERTOS_PC(), LT_FREERTOS_SECONDS(): FreeRTOS event call site/time.
 * LT_STDLIB_ENTER/EXIT(), LT_STDLIB_TICK(): libc serialization/seconds.
 * LT_CPP_ENTER/EXIT(), LT_CPP_TICK(): C++ serialization/seconds.
 * Default libc/C++ locks are no-ops: only suitable for single-thread use.
 */
#endif
