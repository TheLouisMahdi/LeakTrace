# API reference

Include `LeakTrace.h`. Initialize before traced allocations. Unless noted as
startup-only, every operation below must use the same serialization as allocator
hooks. Iterator cursors are indexes into a compacted live table: hold the lock
for the complete traversal, not just one iterator call.

| Function | Purpose | Result / notes |
|---|---|---|
| `lt_init()` | Clear all tracker state and initialize its signature. | Startup only; calling again loses existing records. |
| `lt_on_alloc_at(ptr, bytes, caller_pc, seconds)` | Record an allocator event. | `ptr == NULL` records a failed allocation; successful blocks get size, PC and birth time. |
| `lt_on_alloc(ptr, bytes, caller_pc)` | Allocation event using the last poll time. | Convenient when event-time precision is not required. |
| `lt_on_free(ptr)` | Remove the matching live record. | Null is ignored; an unknown non-null address increments unmatched frees. |
| `lt_poll_at(seconds, total, free, minimum_free)` | Refresh heap measurements, suspects and sampled history. | Requires real measurements; seconds must not go backwards. |
| `lt_set_heap_baseline()` | Make the current measured heap usage the growth baseline. | Call after a valid poll, at a meaningful application checkpoint. |
| `lt_last_poll_seconds()` | Read the last poll timestamp. | Returns zero before initialization/first poll. |
| `lt_get_stats(&stats)` | Copy the current statistics. | Returns 1 on success, 0 for a null output or uninitialized state. |
| `lt_next_live(&cursor, &record)` | Iterate outstanding allocations. | Start `cursor = 0`; returns 1 for a record, 0 at the end. |
| `lt_next_suspect(&cursor, &record)` | Iterate aged allocations. | Same cursor rules; uses the last poll timestamp and configured threshold. |
| `lt_get_history(fast, index, &sample)` | Read history from oldest to newest. | `fast = 1`: recent ring; `fast = 0`: long ring. Returns 1 if the index exists; also check `sample.valid`. |

For example, inside an application-defined lock:

```c
lt_stats_t stats;
lt_record_t record;
uint32_t cursor = 0u;

if (lt_get_stats(&stats)) {
    /* Consume stats.heap_used_bytes, stats.live_count, stats.flags, etc. */
}
while (lt_next_live(&cursor, &record)) {
    /* record.address, caller_pc, bytes, born_seconds */
}
```

The lock/capture mechanism is supplied by the application. On the single-core
heap_4 example, scheduler suspension serializes these operations with hooks.
That technique alone is not a multicore lock and does not make an ISR safe.

## Optional libc wrappers

Include `LeakTrace_Stdlib.h` and compile `LeakTrace_Stdlib.c`.

| Function | Behavior |
|---|---|
| `lt_malloc(bytes)` | Calls libc `malloc`, reports success/failure and returns its pointer. |
| `lt_calloc(count, bytes)` | Calls libc `calloc` and records the requested size or failure. |
| `lt_realloc(ptr, bytes)` | On success, replaces the old record even if the address stays the same. On failure, preserves the old live record and allocation. |
| `lt_free(ptr)` | Reports the free, then calls libc `free`. Match it with libc-family allocations. |

This wrapper defines `lt_realloc(ptr, 0)` as `lt_free(ptr)` followed by a null
return. Ordinary libc calls are not globally redirected. Define the adapter's
lock/time hooks for your application; locks are no-ops by default.

## Optional C++ adapter

Compile `LeakTrace_Cpp.cpp` once, using C++11 or newer, together with the C core.
It replaces scalar/array ordinary and nothrow `new`/`delete`, plus C++14 sized
deletes. Default-allocator containers can therefore generate tracked events
through these replacements. Actual allocator paths and allocation elision still
depend on the toolchain/application.

It does not intercept C++17 over-aligned allocation, invoke `new_handler` retries,
or cover a container's private allocator automatically. Initialize before any
allocations you intend to trace, including static constructors if relevant.
Do not also trace the same underlying malloc/free calls.

## Interpreting the results

- `heap_used_percent_x100 / 100.0` is percent of the **polled heap**.
- `live_traced_bytes` is the sum of recorded sizes. It is not necessarily actual
  occupied heap: an allocator can add metadata/alignment or retain larger blocks.
- `flags == 0` means no diagnostic flag has been raised; it is not proof that
  uninstrumented allocations or memory-safety bugs do not exist.
- `record_drops != 0` means the live-set view is incomplete.
- Suspects are aged allocations, not confirmed leaks.
- History can contain missing samples. Never treat an invalid sample as zero use.
