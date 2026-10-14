/* SPDX-License-Identifier: MIT
 * LeakTrace v7 - continuous embedded heap diagnostics, fixed RAM, no malloc.
 * All calls must be serialized with allocator hooks. Never call from an ISR.
 * Input timestamps are MONOTONIC SECONDS since boot (not RTOS ticks).
 * The live set is NEVER circular; ONLY sampled history is circular.
 */
#ifndef LEAKTRACE_H
#define LEAKTRACE_H
#include <stddef.h>
#include <stdint.h>
#include "LeakTrace_Config.h"

#ifndef LT_MAX_LIVE
#define LT_MAX_LIVE 512u
#endif
#ifndef LT_BUCKETS
#define LT_BUCKETS 256u
#endif
#ifndef LT_PACKED
#define LT_PACKED (UINTPTR_MAX == UINT32_MAX)
#endif
#ifndef LT_TEXT_BASE
#define LT_TEXT_BASE 0x08000000u
#endif
#ifndef LT_FAST_SLOTS
#define LT_FAST_SLOTS 120u /* 20 minutes at 10s intervals */
#endif
#ifndef LT_FAST_SECONDS
#define LT_FAST_SECONDS 10u
#endif
#ifndef LT_SLOW_SLOTS
#define LT_SLOW_SLOTS 2880u /* 48 hours at 60s intervals */
#endif
#ifndef LT_SLOW_SECONDS
#define LT_SLOW_SECONDS 60u
#endif
#ifndef LT_HISTORY_QUANTUM
#define LT_HISTORY_QUANTUM 32u /* 16-bit sample: up to ~2MiB */
#endif
#ifndef LT_SUSPECT_SECONDS
#define LT_SUSPECT_SECONDS 1200u
#endif
#ifndef LT_TOP_SUSPECTS
#define LT_TOP_SUSPECTS 8u
#endif
#ifndef LT_STATE_ATTRIBUTE
#define LT_STATE_ATTRIBUTE
#endif

#if LT_MAX_LIVE < 1u || LT_MAX_LIVE > 65534u
#error "LT_MAX_LIVE must be in [1,65534]"
#endif
#if LT_BUCKETS < 1u || ((LT_BUCKETS & (LT_BUCKETS - 1u)) != 0u)
#error "LT_BUCKETS must be a power of two"
#endif
#if LT_FAST_SLOTS < 1u || LT_FAST_SLOTS > 65535u || LT_SLOW_SLOTS < 1u || LT_SLOW_SLOTS > 65535u
#error "Invalid history capacities"
#endif
#if LT_FAST_SECONDS == 0u || LT_SLOW_SECONDS == 0u || LT_SUSPECT_SECONDS == 0u
#error "Intervals must be nonzero"
#endif
#if LT_HISTORY_QUANTUM < 1u || ((LT_HISTORY_QUANTUM & (LT_HISTORY_QUANTUM - 1u)) != 0u)
#error "LT_HISTORY_QUANTUM must be a power of two"
#endif
#if LT_TOP_SUSPECTS < 1u || LT_TOP_SUSPECTS > 32u
#error "Invalid LT_TOP_SUSPECTS"
#endif
#if LT_PACKED && (LT_TEXT_BASE & 1u)
#error "LT_TEXT_BASE must be halfword aligned"
#endif

#define LT_MAGIC 0x4C543037u
#define LT_VERSION 0x00070000u
#define LT_FLAG_CAPACITY       0x0001u
#define LT_FLAG_ALLOC_FAIL     0x0002u
#define LT_FLAG_DUPLICATE      0x0004u
#define LT_FLAG_UNMATCHED_FREE 0x0008u
#define LT_FLAG_TIME_INVALID   0x0010u
#define LT_FLAG_HEAP_INVALID   0x0020u
#define LT_FLAG_SIZE_CLIPPED   0x0040u
#define LT_FLAG_PC_RANGE       0x0080u
#define LT_FLAG_HISTORY_RANGE  0x0100u
#define LT_FLAG_ADDRESS_RANGE  0x0200u
#define LT_FLAG_CORRUPT_INDEX  0x0400u
#define LT_HISTORY_MISSING 65535u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t address;
    uintptr_t caller_pc;
    uint32_t bytes;
    uint32_t born_seconds;
} lt_record_t;

typedef struct {
    uint32_t seconds;
    uint32_t used_bytes; /* LT_HISTORY_MISSING means no valid reading. */
    uint8_t valid;
} lt_history_sample_t;

typedef struct {
    uintptr_t address;
    uintptr_t caller_pc;
    uint32_t bytes;
    uint32_t age_seconds;
} lt_top_t;

typedef struct {
    uint32_t magic, version, flags;
    uint32_t alloc_calls, free_calls, alloc_failures, record_drops;
    uint32_t duplicates, unmatched_frees, peak_probe_steps;
    uint32_t max_live_count, live_count;
    uint32_t live_traced_bytes, peak_live_traced_bytes;
    uint32_t suspect_count, suspect_traced_bytes, oldest_live_seconds;
    uint32_t top_count, poll_count, last_poll_seconds;
    uint32_t heap_total_bytes, heap_free_bytes, heap_low_water_free_bytes;
    uint32_t heap_used_bytes, heap_peak_used_bytes, heap_used_percent_x100;
    uint32_t heap_baseline_used_bytes;
    int32_t heap_growth_bytes;
    uint32_t fast_last_bucket, slow_last_bucket;
    uint32_t fast_writes, slow_writes;
    uint16_t fast_head, fast_count, slow_head, slow_count;
} lt_stats_t;

#if LT_PACKED
/* 12 bytes on ARM: 32-bit address, 24-bit Thumb PC offset (32 MiB window),
 * 16-bit block bytes, 24-bit timestamp in 10-second units (half-range retention ~2.66 years).
 * Out-of-window PCs and oversized allocations SET EXPLICIT FLAGS.
 */
typedef struct { uint32_t address, pc_size, size_time; } lt_stored_t;
#else
typedef lt_record_t lt_stored_t;
#endif

typedef struct {
    lt_stats_t stats;
    lt_stored_t live[LT_MAX_LIVE];
    uint16_t next[LT_MAX_LIVE]; /* compact chained hash index */
    uint16_t bucket[LT_BUCKETS];
    uint16_t fast[LT_FAST_SLOTS]; /* 10-second resolution */
    uint16_t slow[LT_SLOW_SLOTS]; /* one-minute resolution */
    lt_top_t top[LT_TOP_SUSPECTS];
} lt_state_t;

extern volatile lt_state_t g_lt;
void lt_init(void);
void lt_on_alloc_at(void *ptr, size_t bytes, uintptr_t caller_pc, uint32_t seconds);
void lt_on_alloc(void *ptr, size_t bytes, uintptr_t caller_pc);
void lt_on_free(void *ptr);
void lt_poll_at(uint32_t seconds, size_t heap_total, size_t heap_free,
                size_t heap_low_water_free);
void lt_set_heap_baseline(void);
uint32_t lt_last_poll_seconds(void);
int lt_get_stats(lt_stats_t *out);
int lt_next_live(uint32_t *cursor, lt_record_t *out);
int lt_next_suspect(uint32_t *cursor, lt_record_t *out);
/* fast=0: 1-minute history; fast=1: 10-second recent history. */
int lt_get_history(int fast, uint32_t oldest_index, lt_history_sample_t *out);
#ifdef __cplusplus
}
#endif
#endif /* LEAKTRACE_H */

#ifdef LT_IMPLEMENTATION
#ifndef LT_IMPLEMENTATION_ONCE
#define LT_IMPLEMENTATION_ONCE

LT_STATE_ATTRIBUTE volatile lt_state_t g_lt;

static uint32_t lt_hash(uintptr_t address)
{
    uint32_t x = (uint32_t)address;
#if UINTPTR_MAX > UINT32_MAX
    x ^= (uint32_t)(address >> 32u);
#endif
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    return x & (LT_BUCKETS - 1u);
}

static uint32_t lt_saturate_size(size_t value)
{
#if SIZE_MAX > UINT32_MAX
    if (value > UINT32_MAX) { g_lt.stats.flags |= LT_FLAG_SIZE_CLIPPED; return UINT32_MAX; }
#endif
    return (uint32_t)value;
}

static uint32_t lt_saturating_add(uint32_t a, uint32_t b)
{
    return UINT32_MAX - a < b ? UINT32_MAX : a + b;
}

static void lt_save(uint32_t i, uintptr_t address, uint32_t bytes,
                    uintptr_t pc, uint32_t seconds)
{
#if LT_PACKED
    uint32_t offset = 0xFFFFFFu;
    uint32_t size = bytes;
    if (address > UINT32_MAX) g_lt.stats.flags |= LT_FLAG_ADDRESS_RANGE;
    if ((pc & ~(uintptr_t)1u) >= LT_TEXT_BASE &&
        (((pc & ~(uintptr_t)1u) - LT_TEXT_BASE) >> 1) < 0xFFFFFFu)
        offset = (uint32_t)(((pc & ~(uintptr_t)1u) - LT_TEXT_BASE) >> 1);
    else g_lt.stats.flags |= LT_FLAG_PC_RANGE;
    if (size > UINT16_MAX) {
        g_lt.stats.flags |= LT_FLAG_SIZE_CLIPPED;
        size = UINT16_MAX;
    }
    g_lt.live[i].address = (uint32_t)address;
    g_lt.live[i].pc_size = offset | ((size & 0xFFu) << 24);
    g_lt.live[i].size_time = (size >> 8) | (((seconds / 10u) & 0xFFFFFFu) << 8);
#else
    g_lt.live[i].address = address;
    g_lt.live[i].caller_pc = pc;
    g_lt.live[i].bytes = bytes;
    g_lt.live[i].born_seconds = seconds;
#endif
}

static uintptr_t lt_address(uint32_t i)
{
    return (uintptr_t)g_lt.live[i].address;
}

static uint32_t lt_size(uint32_t i)
{
#if LT_PACKED
    return (g_lt.live[i].pc_size >> 24) | ((g_lt.live[i].size_time & 0xFFu) << 8);
#else
    return g_lt.live[i].bytes;
#endif
}

static uintptr_t lt_pc(uint32_t i)
{
#if LT_PACKED
    uint32_t offset = g_lt.live[i].pc_size & 0xFFFFFFu;
    return offset == 0xFFFFFFu ? 0u : (uintptr_t)(LT_TEXT_BASE + (offset << 1) + 1u);
#else
    return g_lt.live[i].caller_pc;
#endif
}

static uint32_t lt_age(uint32_t i, uint32_t now)
{
#if LT_PACKED
    uint32_t born = (g_lt.live[i].size_time >> 8) & 0xFFFFFFu;
    uint32_t elapsed_units = ((now / 10u) - born) & 0xFFFFFFu;
    /* Half-range rule disambiguates a future alloc from an old record.
     * Supported ages are less than 2^23 ten-second intervals (~2.66 years).
     */
    if (elapsed_units >= 0x800000u) return 0u;
    return elapsed_units * 10u + now % 10u;
#else
    uint32_t born = g_lt.live[i].born_seconds;
    if ((int32_t)(now - born) < 0) return 0u;
    return now - born;
#endif
}

static uint32_t lt_born(uint32_t i, uint32_t now)
{
#if LT_PACKED
    return now - lt_age(i, now); /* Quantized to 10s. */
#else
    (void)now;
    return g_lt.live[i].born_seconds;
#endif
}

static void lt_copy_record(uint32_t i, uint32_t now, lt_record_t *out)
{
    out->address = lt_address(i);
    out->caller_pc = lt_pc(i);
    out->bytes = lt_size(i);
    out->born_seconds = lt_born(i, now);
}

static uint16_t lt_find(uintptr_t addr, uint16_t *previous, uint32_t *steps)
{
    uint16_t ref = g_lt.bucket[lt_hash(addr)];
    *previous = 0u;
    *steps = 0u;
    while (ref != 0u) {
        if (*steps >= g_lt.stats.live_count || ref > g_lt.stats.live_count) {
            g_lt.stats.flags |= LT_FLAG_CORRUPT_INDEX;
            return 0u; /* Corrupt chain must never spin forever. */
        }
        ++*steps;
        if (lt_address(ref - 1u) == addr) return ref;
        *previous = ref;
        ref = g_lt.next[ref - 1u];
    }
    return 0u;
}

static void lt_probe_steps(uint32_t steps)
{
    if (steps > g_lt.stats.peak_probe_steps) g_lt.stats.peak_probe_steps = steps;
}

void lt_init(void)
{
    volatile uint8_t *p = (volatile uint8_t *)&g_lt;
    size_t i;
    for (i = 0; i < sizeof(g_lt); ++i) p[i] = 0u;
    g_lt.stats.magic = LT_MAGIC;
    g_lt.stats.version = LT_VERSION;
}

void lt_on_alloc_at(void *ptr, size_t bytes, uintptr_t caller_pc, uint32_t seconds)
{
    uint16_t previous;
    uint32_t steps, n, size, bucket;
    volatile lt_stats_t *s = &g_lt.stats;
    if (s->magic != LT_MAGIC) return;
    ++s->alloc_calls;
    if (!ptr) { ++s->alloc_failures; s->flags |= LT_FLAG_ALLOC_FAIL; return; }
#if LT_PACKED
    if ((uintptr_t)ptr > UINT32_MAX) {
        ++s->record_drops;
        s->flags |= LT_FLAG_ADDRESS_RANGE | LT_FLAG_CAPACITY;
        return;
    }
#endif
    if (lt_find((uintptr_t)ptr, &previous, &steps)) {
        ++s->duplicates;
        s->flags |= LT_FLAG_DUPLICATE;
        return; /* Preserve original allocation. */
    }
    if (s->flags & LT_FLAG_CORRUPT_INDEX) return;
    lt_probe_steps(steps);
    n = s->live_count;
    if (n >= LT_MAX_LIVE) { ++s->record_drops; s->flags |= LT_FLAG_CAPACITY; return; }
    size = lt_saturate_size(bytes);
    lt_save(n, (uintptr_t)ptr, size, caller_pc, seconds);
    size = lt_size(n);
    bucket = lt_hash((uintptr_t)ptr);
    g_lt.next[n] = g_lt.bucket[bucket];
    g_lt.bucket[bucket] = (uint16_t)(n + 1u);
    s->live_count = n + 1u;
    s->live_traced_bytes = lt_saturating_add(s->live_traced_bytes, size);
    if (s->live_count > s->max_live_count) s->max_live_count = s->live_count;
    if (s->live_traced_bytes > s->peak_live_traced_bytes)
        s->peak_live_traced_bytes = s->live_traced_bytes;
}

void lt_on_alloc(void *ptr, size_t bytes, uintptr_t caller_pc)
{
    lt_on_alloc_at(ptr, bytes, caller_pc, lt_last_poll_seconds());
}

void lt_on_free(void *ptr)
{
    uint16_t previous, ref, move_prev, move_ref;
    uint32_t steps, index, last, bucket, moved_bucket;
    uintptr_t moved_address;
    volatile lt_stats_t *s = &g_lt.stats;
    if (!ptr || s->magic != LT_MAGIC) return;
    ++s->free_calls;
    ref = lt_find((uintptr_t)ptr, &previous, &steps);
    if (s->flags & LT_FLAG_CORRUPT_INDEX) return;
    lt_probe_steps(steps);
    if (!ref) { ++s->unmatched_frees; s->flags |= LT_FLAG_UNMATCHED_FREE; return; }
    index = ref - 1u;
    last = s->live_count - 1u;
    bucket = lt_hash((uintptr_t)ptr);
    if (previous) g_lt.next[previous - 1u] = g_lt.next[index];
    else g_lt.bucket[bucket] = g_lt.next[index];
    s->live_traced_bytes -= lt_size(index);
    if (index != last) {
        moved_address = lt_address(last);
        moved_bucket = lt_hash(moved_address);
        move_ref = lt_find(moved_address, &move_prev, &steps);
        if (move_ref != (uint16_t)(last + 1u)) {
            s->flags |= LT_FLAG_CORRUPT_INDEX;
            return;
        }
        if (move_prev) g_lt.next[move_prev - 1u] = (uint16_t)(index + 1u);
        else g_lt.bucket[moved_bucket] = (uint16_t)(index + 1u);
#if LT_PACKED
        g_lt.live[index].address = g_lt.live[last].address;
        g_lt.live[index].pc_size = g_lt.live[last].pc_size;
        g_lt.live[index].size_time = g_lt.live[last].size_time;
#else
        g_lt.live[index].address = g_lt.live[last].address;
        g_lt.live[index].caller_pc = g_lt.live[last].caller_pc;
        g_lt.live[index].bytes = g_lt.live[last].bytes;
        g_lt.live[index].born_seconds = g_lt.live[last].born_seconds;
#endif
        g_lt.next[index] = g_lt.next[last];
    }
    g_lt.next[last] = 0u;
    g_lt.live[last].address = 0u;
    s->live_count = last;
}

static int32_t lt_growth(uint32_t used, uint32_t baseline)
{
    int64_t v = (int64_t)used - (int64_t)baseline;
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

static uint16_t lt_quantize(uint32_t used)
{
    uint32_t value = used / LT_HISTORY_QUANTUM;
    if (value >= LT_HISTORY_MISSING) {
        g_lt.stats.flags |= LT_FLAG_HISTORY_RANGE;
        return LT_HISTORY_MISSING;
    }
    return (uint16_t)value;
}

/* Fill missed buckets with a sentinel, never fake a continuous trend. */
static void lt_write_history(volatile uint16_t *ring, uint32_t capacity,
                             volatile uint16_t *head, volatile uint16_t *count,
                             volatile uint32_t *last_bucket,
                             volatile uint32_t *writes,
                             uint32_t seconds, uint32_t interval, uint16_t sample)
{
    uint32_t unit = seconds / interval;
    uint32_t distance, j;
    if (*writes == 0u) distance = 1u;
    else {
        distance = unit - *last_bucket;
        if (distance == 0u) return;
        if (distance > capacity) {
            *head = 0u;
            *count = 0u;
            distance = capacity;
        }
    }
    for (j = 0u; j < distance; ++j) {
        ring[*head] = (j + 1u == distance) ? sample : LT_HISTORY_MISSING;
        *head = (uint16_t)((*head + 1u == capacity) ? 0u : *head + 1u);
        if (*count < capacity) ++*count;
    }
    *last_bucket = unit;
    ++*writes;
}

void lt_poll_at(uint32_t seconds, size_t heap_total, size_t heap_free,
                size_t heap_low_water_free)
{
    uint32_t i, suspects = 0u, bytes = 0u, oldest = 0u, top_count = 0u;
    volatile lt_stats_t *s = &g_lt.stats;
    if (s->magic != LT_MAGIC) return;
    if (s->poll_count && seconds < s->last_poll_seconds) {
        s->flags |= LT_FLAG_TIME_INVALID;
        return; /* Fail closed on a backwards clock: no fabricated ages/history. */
    }
    ++s->poll_count;
    s->last_poll_seconds = seconds;
    if (heap_free > heap_total || heap_low_water_free > heap_free ||
        heap_total > UINT32_MAX) {
        s->flags |= LT_FLAG_HEAP_INVALID;
        return;
    }
    s->heap_total_bytes = (uint32_t)heap_total;
    s->heap_free_bytes = (uint32_t)heap_free;
    s->heap_low_water_free_bytes = (uint32_t)heap_low_water_free;
    s->heap_used_bytes = (uint32_t)(heap_total - heap_free);
    s->heap_peak_used_bytes = (uint32_t)(heap_total - heap_low_water_free);
    s->heap_used_percent_x100 = heap_total
        ? (uint32_t)(((uint64_t)(heap_total - heap_free) * 10000u) / heap_total) : 0u;
    if (s->poll_count == 1u) s->heap_baseline_used_bytes = s->heap_used_bytes;
    s->heap_growth_bytes = lt_growth(s->heap_used_bytes, s->heap_baseline_used_bytes);
    for (i = 0u; i < s->live_count; ++i) {
        uint32_t age = lt_age(i, seconds);
        if (age > oldest) oldest = age;
        if (age >= LT_SUSPECT_SECONDS) {
            uint32_t size = lt_size(i), position = top_count;
            ++suspects;
            bytes = lt_saturating_add(bytes, size);
            while (position > 0u && (size > g_lt.top[position - 1u].bytes ||
                   (size == g_lt.top[position - 1u].bytes &&
                    age > g_lt.top[position - 1u].age_seconds))) --position;
            if (position < LT_TOP_SUSPECTS) {
                uint32_t j = top_count < LT_TOP_SUSPECTS ? top_count++ : LT_TOP_SUSPECTS - 1u;
                while (j > position) {
                    g_lt.top[j].address = g_lt.top[j - 1u].address;
                    g_lt.top[j].caller_pc = g_lt.top[j - 1u].caller_pc;
                    g_lt.top[j].bytes = g_lt.top[j - 1u].bytes;
                    g_lt.top[j].age_seconds = g_lt.top[j - 1u].age_seconds;
                    --j;
                }
                g_lt.top[position].address = lt_address(i);
                g_lt.top[position].caller_pc = lt_pc(i);
                g_lt.top[position].bytes = size;
                g_lt.top[position].age_seconds = age;
            }
        }
    }
    for (i = top_count; i < LT_TOP_SUSPECTS; ++i) {
        g_lt.top[i].address = 0;
        g_lt.top[i].caller_pc = 0;
        g_lt.top[i].bytes = 0u;
        g_lt.top[i].age_seconds = 0u;
    }
    s->suspect_count = suspects;
    s->suspect_traced_bytes = bytes;
    s->oldest_live_seconds = oldest;
    s->top_count = top_count;
    {
        uint16_t sample = lt_quantize(s->heap_used_bytes);
        lt_write_history(g_lt.fast, LT_FAST_SLOTS, &s->fast_head, &s->fast_count,
                         &s->fast_last_bucket, &s->fast_writes, seconds,
                         LT_FAST_SECONDS, sample);
        lt_write_history(g_lt.slow, LT_SLOW_SLOTS, &s->slow_head, &s->slow_count,
                         &s->slow_last_bucket, &s->slow_writes, seconds,
                         LT_SLOW_SECONDS, sample);
    }
}

void lt_set_heap_baseline(void)
{
    if (g_lt.stats.magic != LT_MAGIC) return;
    g_lt.stats.heap_baseline_used_bytes = g_lt.stats.heap_used_bytes;
    g_lt.stats.heap_growth_bytes = 0;
}

uint32_t lt_last_poll_seconds(void)
{
    return g_lt.stats.magic == LT_MAGIC ? g_lt.stats.last_poll_seconds : 0u;
}

int lt_get_stats(lt_stats_t *out)
{
    const volatile uint8_t *src = (const volatile uint8_t *)&g_lt.stats;
    uint8_t *dst = (uint8_t *)out;
    size_t i;
    if (!out || g_lt.stats.magic != LT_MAGIC) return 0;
    for (i = 0; i < sizeof(*out); ++i) dst[i] = src[i];
    return 1;
}

int lt_next_live(uint32_t *cursor, lt_record_t *out)
{
    if (!cursor || !out || g_lt.stats.magic != LT_MAGIC ||
        *cursor >= g_lt.stats.live_count) return 0;
    lt_copy_record((*cursor)++, g_lt.stats.last_poll_seconds, out);
    return 1;
}

int lt_next_suspect(uint32_t *cursor, lt_record_t *out)
{
    uint32_t i, now;
    if (!cursor || !out || g_lt.stats.magic != LT_MAGIC) return 0;
    now = g_lt.stats.last_poll_seconds;
    for (i = *cursor; i < g_lt.stats.live_count; ++i) {
        if (lt_age(i, now) >= LT_SUSPECT_SECONDS) {
            lt_copy_record(i, now, out);
            *cursor = i + 1u;
            return 1;
        }
    }
    *cursor = g_lt.stats.live_count;
    return 0;
}

int lt_get_history(int fast, uint32_t oldest_index, lt_history_sample_t *out)
{
    uint32_t capacity, count, head, last_bucket, interval, index;
    uint16_t sample;
    if (!out || g_lt.stats.magic != LT_MAGIC) return 0;
    if (fast) {
        capacity = LT_FAST_SLOTS;
        count = g_lt.stats.fast_count;
        head = g_lt.stats.fast_head;
        last_bucket = g_lt.stats.fast_last_bucket;
        interval = LT_FAST_SECONDS;
    } else {
        capacity = LT_SLOW_SLOTS;
        count = g_lt.stats.slow_count;
        head = g_lt.stats.slow_head;
        last_bucket = g_lt.stats.slow_last_bucket;
        interval = LT_SLOW_SECONDS;
    }
    if (oldest_index >= count) return 0;
    index = (head + capacity - count + oldest_index) % capacity;
    sample = fast ? g_lt.fast[index] : g_lt.slow[index];
    out->seconds = (last_bucket - (count - 1u - oldest_index)) * interval;
    out->valid = sample != LT_HISTORY_MISSING;
    out->used_bytes = out->valid ? (uint32_t)sample * LT_HISTORY_QUANTUM : 0u;
    return 1;
}
#endif /* LT_IMPLEMENTATION_ONCE */
#endif /* LT_IMPLEMENTATION */
