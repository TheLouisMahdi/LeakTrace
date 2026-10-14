#include <assert.h>
#define LT_MAX_LIVE 16u
#define LT_BUCKETS 8u
#define LT_FAST_SLOTS 4u
#define LT_SLOW_SLOTS 4u
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
#include "../LeakTrace_Stdlib.h"
int main(void) {
    lt_stats_t s;
    void *p;
    lt_init();
    p = lt_malloc(64u);
    assert(p && lt_get_stats(&s) && s.live_count == 1u);
    p = lt_realloc(p, 128u);
    assert(p && lt_get_stats(&s) && s.live_count == 1u);
    lt_free(p);
    assert(lt_get_stats(&s) && s.live_count == 0u);
    return 0;
}
