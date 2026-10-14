#include <assert.h>
#include <stdint.h>
#define LT_PACKED 1
#define LT_MAX_LIVE 8u
#define LT_BUCKETS 4u
#define LT_FAST_SLOTS 4u
#define LT_SLOW_SLOTS 4u
#define LT_IMPLEMENTATION
#define LT_FREERTOS_PC() 0x08020101u
#define LT_FREERTOS_SECONDS() 12u
#include "../LeakTrace_FreeRTOS.h"
int main(void) {
    lt_stats_t s;
    lt_init();
    traceMALLOC((void *)(uintptr_t)0x24000400u, 20u);
    traceFREE((void *)(uintptr_t)0x24000400u, 20u);
    assert(lt_get_stats(&s) && s.alloc_calls == 1u && s.free_calls == 1u && s.live_count == 0u);
    return 0;
}
