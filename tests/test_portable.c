#include <assert.h>
#include <stdint.h>
#define LT_PACKED 0
#define LT_MAX_LIVE 8u
#define LT_BUCKETS 2u
#define LT_FAST_SLOTS 4u
#define LT_SLOW_SLOTS 4u
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
int main(void) {
    lt_record_t r;
    uint32_t cursor = 0;
    uintptr_t big_addr = UINTPTR_MAX - 1000u;
    lt_init();
    lt_on_alloc_at((void *)big_addr, 80000u, UINTPTR_MAX-2u, 33u);
    lt_poll_at(3600u, 200000u, 120000u, 100000u);
    assert(lt_next_suspect(&cursor, &r));
    assert(r.address == big_addr && r.caller_pc == UINTPTR_MAX-2u);
    assert(r.bytes == 80000u && r.born_seconds == 33u);
    lt_on_free((void *)big_addr);
    return 0;
}
