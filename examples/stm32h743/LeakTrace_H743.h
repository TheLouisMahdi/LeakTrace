/* SPDX-License-Identifier: MIT */
#ifndef LEAKTRACE_H743_H
#define LEAKTRACE_H743_H
#include "LeakTrace.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LT_H743_SNAPSHOT_MAGIC 0x4C545331u
typedef struct {
    uint32_t magic;
    uint32_t sequence; /* Odd while publishing, even after completion. */
    uint32_t uptime_seconds;
    lt_stats_t stats;
} lt_h743_snapshot_t;

extern volatile lt_h743_snapshot_t g_lt_snapshot;

/* Call once after platform initialization, before any traced allocations. */
void LT_H743_Init(void);
/* Call from task context; also used by the optional example monitor task. */
void LT_H743_Poll(void);
/* Optional demo: call after osKernelInitialize, before osKernelStart.
 * Returns 1 on success, 0 on task creation failure.
 */
int LT_H743_StartExampleTasks(void);

#ifdef __cplusplus
}
#endif
#endif
