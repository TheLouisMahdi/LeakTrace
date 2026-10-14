/* SPDX-License-Identifier: MIT
 * H743 integration example. Requires the application to supply STM32 CMSIS,
 * FreeRTOS, heap_4, and the documented linker/profile configuration.
 */
#include "LeakTrace_H743.h"
#include "stm32h7xx.h"
#include "FreeRTOS.h"
#include "task.h"

volatile lt_h743_snapshot_t g_lt_snapshot
    __attribute__((section(".lt_monitor.snapshot"), aligned(32)));

static uint32_t previous_ticks;
static uint64_t elapsed_ticks;

_Static_assert(sizeof(lt_stats_t) == 136u, "Unexpected statistics ABI");
_Static_assert(sizeof(lt_h743_snapshot_t) == 148u, "Unexpected snapshot ABI");
_Static_assert(sizeof(TickType_t) == 4u, "This example requires 32-bit ticks");
_Static_assert(configTOTAL_HEAP_SIZE == LT_H743_HEAP_BYTES,
               "Set configTOTAL_HEAP_SIZE to LT_H743_HEAP_BYTES");

/* Both exported objects start on their own cache line; the linker reserves
 * padding at the end as well. Never invalidate CPU-owned dirty data here.
 */
static void publish_to_sram(const volatile void *address, size_t bytes)
{
    __DMB();
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        const size_t rounded = (bytes + 31u) & ~(size_t)31u;
        SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)address, (int32_t)rounded);
    }
    __DSB();
}

/* Callers are heap_4 hooks (already scheduler-suspended) or the poll function
 * below. Update at least once per tick wrap; the demo polls every second.
 */
uint32_t LT_H743_Seconds(void)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    elapsed_ticks += (uint32_t)(now - previous_ticks);
    previous_ticks = now;
    return (uint32_t)(elapsed_ticks / configTICK_RATE_HZ);
}

void LT_H743_Init(void)
{
    previous_ticks = 0u;
    elapsed_ticks = 0u;
    lt_init(); /* Explicitly initializes g_lt in the NOLOAD section. */
    volatile uint8_t *p = (volatile uint8_t *)&g_lt_snapshot;
    for (size_t i = 0u; i < sizeof(g_lt_snapshot); ++i) p[i] = 0u;
    publish_to_sram(&g_lt, sizeof(g_lt));
    publish_to_sram(&g_lt_snapshot, sizeof(g_lt_snapshot));
}

void LT_H743_Poll(void)
{
    lt_stats_t stats;
    vTaskSuspendAll(); /* Same serialization as this heap_4's trace hooks. */
    ++g_lt_snapshot.sequence;
    publish_to_sram(&g_lt_snapshot, sizeof(g_lt_snapshot)); /* Publish odd first. */

    const uint32_t seconds = LT_H743_Seconds();
    lt_poll_at(seconds, configTOTAL_HEAP_SIZE, xPortGetFreeHeapSize(),
               xPortGetMinimumEverFreeHeapSize());
    if (lt_get_stats(&stats)) {
        g_lt_snapshot.magic = LT_H743_SNAPSHOT_MAGIC;
        g_lt_snapshot.uptime_seconds = seconds;
        g_lt_snapshot.stats = stats;
    } else {
        g_lt_snapshot.magic = 0u; /* Invalid capture; do not present fake data. */
    }

    publish_to_sram(&g_lt, sizeof(g_lt));
    publish_to_sram(&g_lt_snapshot, sizeof(g_lt_snapshot)); /* Payload, still odd. */
    ++g_lt_snapshot.sequence;
    publish_to_sram(&g_lt_snapshot, sizeof(g_lt_snapshot)); /* Completed capture. */
    (void)xTaskResumeAll();
}

static void monitor_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        LT_H743_Poll();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000u));
    }
}

static void worker_task(void *argument)
{
    (void)argument;
    uint32_t cycle = 0u;
    for (;;) {
        const size_t bytes = 64u * (1u + (cycle & 7u));
        volatile uint8_t *buffer = (volatile uint8_t *)pvPortMalloc(bytes);
        if (buffer != NULL) {
            for (size_t i = 0u; i < bytes; ++i) buffer[i] = (uint8_t)(i + cycle);
            vTaskDelay(pdMS_TO_TICKS(2000u));
            vPortFree((void *)buffer);
        } /* A failed allocation is recorded by the existing trace hook. */
        ++cycle;
        vTaskDelay(pdMS_TO_TICKS(1000u));
    }
}

int LT_H743_StartExampleTasks(void)
{
    TaskHandle_t monitor;
    if (xTaskCreate(monitor_task, "lt_monitor", LT_H743_MONITOR_STACK_WORDS,
                    NULL, tskIDLE_PRIORITY + 2u, &monitor) != pdPASS) return 0;
    if (xTaskCreate(worker_task, "lt_worker", LT_H743_WORKER_STACK_WORDS,
                    NULL, tskIDLE_PRIORITY + 1u, NULL) != pdPASS) {
        vTaskDelete(monitor);
        return 0;
    }
    return 1;
}
