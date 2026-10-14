#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define LT_PACKED 1
#define LT_MAX_LIVE 2100u
#define LT_BUCKETS 256u
#define LT_FAST_SLOTS 6u
#define LT_SLOW_SLOTS 8u
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
#define PTR(i) ((void *)(uintptr_t)(0x24000000u + 16u * (i)))
#define PC(i) ((uintptr_t)(0x08020001u + 2u * (i)))

static void test_live_churn(void)
{
    uint32_t i, cursor, count;
    lt_stats_t s;
    lt_record_t r;
    lt_init();
    for (i = 1; i <= 2000; ++i)
        lt_on_alloc_at(PTR(i), i, PC(i), 0u);
    assert(lt_get_stats(&s) && s.live_count == 2000u);
    assert(s.record_drops == 0u && s.live_traced_bytes == 2001000u);
    lt_poll_at(7200u, 256000u, 180000u, 160000u);
    assert(lt_get_stats(&s) && s.suspect_count == 2000u);
    assert(s.oldest_live_seconds == 7200u);
    cursor = 0; count = 0;
    while (lt_next_suspect(&cursor, &r)) {
        uint32_t n = ((uint32_t)r.address - 0x24000000u) / 16u;
        assert(n >= 1u && n <= 2000u && r.caller_pc == PC(n) && r.bytes == n);
        assert(r.born_seconds == 0u);
        ++count;
    }
    assert(count == 2000u);
    for (i = 1; i <= 2000; i += 2)
        lt_on_free(PTR(i));
    assert(lt_get_stats(&s) && s.live_count == 1000u && s.live_traced_bytes == 1001000u);
    for (i = 1; i <= 2000; i += 2)
        lt_on_alloc_at(PTR(i), 32u, PC(i), 7300u);
    assert(lt_get_stats(&s) && s.live_count == 2000u && s.live_traced_bytes == 1033000u);
    cursor = 0; count = 0;
    while (lt_next_live(&cursor, &r)) {
        uint32_t n = ((uint32_t)r.address - 0x24000000u) / 16u;
        assert(n >= 1u && n <= 2000u && r.caller_pc == PC(n));
        assert(r.bytes == ((n & 1u) ? 32u : n));
        ++count;
    }
    assert(count == 2000u);
    for (i = 2000; i >= 1; --i) lt_on_free(PTR(i));
    assert(lt_get_stats(&s) && s.live_count == 0 && s.live_traced_bytes == 0);
    puts("PASS: 2000 simultaneous live, collision chains, free/reuse, exact tracked identity");
}

static void test_capacity_flags(void)
{
    uint32_t i;
    lt_stats_t s;
    lt_record_t r;
    lt_init();
    for (i = 1; i <= LT_MAX_LIVE; ++i) lt_on_alloc_at(PTR(i), 8u, PC(i), 20u);
    lt_on_alloc_at(PTR(LT_MAX_LIVE + 1u), 8u, PC(2), 20u);
    lt_on_alloc_at(PTR(1), 64u, PC(4), 20u);
    lt_on_alloc_at(NULL, 4u, PC(5), 20u);
    assert(lt_get_stats(&s) && s.record_drops == 1 && s.duplicates == 1 && s.alloc_failures == 1);
    assert((s.flags & (LT_FLAG_CAPACITY | LT_FLAG_DUPLICATE | LT_FLAG_ALLOC_FAIL)) ==
           (LT_FLAG_CAPACITY | LT_FLAG_DUPLICATE | LT_FLAG_ALLOC_FAIL));
    lt_on_free(PTR(20)); lt_on_free(PTR(20));
    assert(lt_get_stats(&s) && s.unmatched_frees == 1);
    lt_on_alloc_at(PTR(9u), 70000u, PC(9), 21u); /* duplicate; preserve first */
    lt_on_free(PTR(9u));
    lt_on_alloc_at(PTR(9u), 70000u, 0x1u, 25u);
    assert(lt_get_stats(&s) && (s.flags & (LT_FLAG_SIZE_CLIPPED | LT_FLAG_PC_RANGE)));
    lt_poll_at(4000, 512000, 511000, 500000);
    { uint32_t cursor = 0u, seen = 0u;
      while (lt_next_live(&cursor, &r)) {
          if (r.address == (uintptr_t)PTR(9u)) {
              assert(r.bytes == 65535u && r.caller_pc == 0u);
              ++seen;
          }
      }
      assert(seen == 1u);
    }
    puts("PASS: overflow/duplicate/unmatched/large allocation/PC-range flags");
}

static void test_history(void)
{
    lt_stats_t s;
    lt_history_sample_t h;
    uint32_t i;
    lt_init();
    /* Synthetic 48-hour timed polling, no random allocation traffic. */
    for (i = 0; i <= 48u * 60u; ++i)
        lt_poll_at(i * 60u, 256000u, 256000u - 32u * (i % 1000u), 220000u);
    assert(lt_get_stats(&s) && s.slow_count == LT_SLOW_SLOTS);
    assert(lt_get_history(0, 7u, &h) && h.seconds == 48u * 3600u);
    assert(h.valid && h.used_bytes == (48u * 60u % 1000u) * 32u);
    lt_poll_at(48u * 3600u + 10u, 256000u, 100000u, 80000u);
    assert(lt_get_history(1, 0u, &h) && !h.valid); /* gap between samples */
    assert(lt_get_history(1, LT_FAST_SLOTS - 1u, &h) && h.valid);
    assert(h.seconds == 48u * 3600u + 10u);
    lt_poll_at(48u * 3600u + 60u, 256000u, 90000u, 80000u);
    assert(lt_get_stats(&s) && s.heap_used_bytes == 166000u);
    assert(!lt_get_history(0, LT_SLOW_SLOTS, &h));
    puts("PASS: two-resolution circular trend, missing samples, 48-hour progression");
}

static void test_corrupt_chain_bounded(void)
{
    lt_stats_t s;
    lt_init();
    g_lt.bucket[lt_hash((uintptr_t)PTR(1))] = 100u;
    lt_on_alloc_at(PTR(1), 40u, PC(1), 30u);
    assert(lt_get_stats(&s) && s.live_count == 0u);
    assert(s.flags & LT_FLAG_CORRUPT_INDEX);
    puts("PASS: corrupt hash index fails closed without unbounded traversal");
}

static void test_wrap(void)
{
    lt_stats_t s;
    lt_init();
    lt_on_alloc_at(PTR(1), 33u, PC(1), 167772140u); /* ~5-year packed 10s rollover */
    lt_poll_at(167772200u, 256000u, 128000u, 120000u);
    assert(lt_get_stats(&s) && s.oldest_live_seconds >= 60u);
    lt_on_free(PTR(1));
    puts("PASS: packed timestamp rollover within supported retention horizon");
}

int main(void) { test_live_churn(); test_capacity_flags(); test_history(); test_wrap(); test_corrupt_chain_bounded(); }
