#include <assert.h>
#include <stdint.h>
#define LT_PACKED 1
#define LT_MAX_LIVE 8u
#define LT_BUCKETS 4u
#define LT_FAST_SLOTS 4u
#define LT_SLOW_SLOTS 4u
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
int main(void) {
    lt_record_t r;
    uint32_t cursor = 0u;
    lt_init();
    lt_poll_at(100u, 10000u, 8000u, 7000u);
    lt_on_alloc_at((void *)(uintptr_t)0x24000800u, 40u, 0x08012001u, 130u);
    assert(!lt_next_suspect(&cursor, &r));
    lt_poll_at(135u, 10000u, 7900u, 7000u);
    assert(g_lt.stats.oldest_live_seconds == 5u);
    return 0;
}
