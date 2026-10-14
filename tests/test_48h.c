#include <assert.h>
#include <stdint.h>
#define LT_PACKED 1
#define LT_MAX_LIVE 8u
#define LT_BUCKETS 4u
#define LT_SLOW_SLOTS 2880u /* This test explicitly retains all 48 hours. */
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
int main(void) {
    lt_stats_t s;
    lt_history_sample_t h;
    uint32_t n;
    lt_init();
    for (n = 0; n <= 48u*60u; n++)
        lt_poll_at(n*60u, 262144u, 262144u - (n%1000u)*32u, 220000u);
    assert(lt_get_stats(&s) && s.slow_count == 2880u);
    assert(lt_get_history(0, 0, &h) && h.seconds == 60u);
    assert(lt_get_history(0, 2879u, &h) && h.seconds == 48u*3600u);
    assert(h.valid);
    return 0;
}
