/* SPDX-License-Identifier: MIT */
#ifndef LEAKTRACE_H743_FAULT_H
#define LEAKTRACE_H743_FAULT_H
#include "LeakTrace_H743_FaultFormat.h"
#include "LeakTrace_H743_FaultConfig.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t magic, reset_flags, device_id, storage_ready;
    uint32_t record_sequence, handler_mask, in_progress, write_verified;
} lt_h743_fault_status_t;
extern volatile lt_h743_fault_status_t g_lt_fault_status;
/* Owns ALL RTC_BKP0R..BKP31R. Call early in privileged startup, before reset
 * flags are cleared, and after any application backup-domain reset.
 * Does not clear a previous record, change RTC clocks or enable exceptions.
 */
int LT_H743_FaultInit(void);
/* Enable MemManage/BusFault/UsageFault only if all four vectors are installed.
 * optional_traps: 0, SCB_CCR_DIV_0_TRP_Msk and/or SCB_CCR_UNALIGN_TRP_Msk.
 */
int LT_H743_FaultEnable(uint32_t optional_traps);
/* Startup/task context, not from a fault or ISR. No heap allocation. */
int LT_H743_FaultRead(lt_h743_fault_record_t *out);
void LT_H743_FaultClear(void);
/* Exception-entry target; implemented in FaultEntry.S. Never call as C code. */
void LT_H743_FaultEntry(void);
#ifdef __cplusplus
}
#endif
#endif
