# LeakTrace v7

Fixed-storage heap diagnostics for embedded C projects, with optional FreeRTOS, libc and C++ adapters.

LeakTrace records allocation/free events, keeps a live-allocation table, and reports heap measurements supplied by your application. Its core is independent of STM32 HAL, a particular board and an RTOS.

## Features

- Allocation/free counters, allocation failures, live block count and tracked bytes.
- Allocation address, caller PC, size and age; configurable age-based suspect ranking.
- Current/peak heap usage, free bytes, low-water mark and heap percentage from real allocator measurements.
- Sampled short/long history, with explicit markers for missed samples.
- Diagnostic flags for capacity exhaustion, duplicate addresses, unmatched frees and invalid inputs.
- A global SRAM state (`g_lt`) and Python tools for inspecting binary memory exports.
- Fixed core storage: the tracker itself does not allocate from the monitored heap.

## File guide

| File | Purpose |
|---|---|
| [`LeakTrace.h`](LeakTrace.h) | Public API, record/statistics types and core implementation guarded by `LT_IMPLEMENTATION`. |
| [`leaktrace.c`](leaktrace.c) | Compiles the core implementation exactly once. |
| [`LeakTrace_Config.h`](LeakTrace_Config.h) | Shared record capacities, history sizes and optional platform hooks. |
| [`LeakTrace_FreeRTOS.h`](LeakTrace_FreeRTOS.h) | Optional `traceMALLOC`/`traceFREE` adapter for FreeRTOS heap_4/heap_5. |
| [`LeakTrace_Stdlib.h`](LeakTrace_Stdlib.h) | Declarations for explicit `lt_malloc`, `lt_calloc`, `lt_realloc` and `lt_free` wrappers. |
| [`LeakTrace_Stdlib.c`](LeakTrace_Stdlib.c) | Implements those wrappers around libc allocation functions. |
| [`LeakTrace_Cpp.cpp`](LeakTrace_Cpp.cpp) | Optional global ordinary, array and nothrow `new`/`delete` replacements. |
| [`tools/decode_snapshot.py`](tools/decode_snapshot.py) | Decodes a raw 136-byte `lt_stats_t` export. |
| [`tools/decode_live.py`](tools/decode_live.py) | Decodes live records and groups outstanding allocations by caller PC. |
| [`tools/resolve_pc.py`](tools/resolve_pc.py) | Resolves saved caller PCs to function names/source lines using a matching ELF and `addr2line`. |
| [`.gitignore`](.gitignore) | Excludes generated binaries, memory dumps and Python caches. |
| [`README.md`](README.md) | Integration, usage and current limitations. |

<details>
<summary>Test files</summary>

| File | Purpose |
|---|---|
| [`tests/run.py`](tests/run.py) | Cross-platform native C/C++ test runner; supports `--cc` and `--cxx`. |
| [`tests/run.ps1`](tests/run.ps1) | Alternative Windows PowerShell runner. |
| [`tests/run.sh`](tests/run.sh) | Alternative POSIX shell runner. |
| [`tests/test_core.c`](tests/test_core.c) | Live-set churn, address reuse, collisions, flags, history and corruption checks. |
| [`tests/test_portable.c`](tests/test_portable.c) | Full pointer/PC values and blocks larger than 64 KiB in unpacked mode. |
| [`tests/test_adapters.c`](tests/test_adapters.c) | FreeRTOS adapter event forwarding with controlled test inputs. |
| [`tests/test_stdlib.c`](tests/test_stdlib.c) | libc allocation, reallocation and free wrapper integration. |
| [`tests/test_cpp.cpp`](tests/test_cpp.cpp) | Global C++ allocation/deallocation adapter integration. |
| [`tests/test_48h.c`](tests/test_48h.c) | History retention across 48 hours of simulated timestamps. |
| [`tests/test_time_future.c`](tests/test_time_future.c) | Allocation age when an event is newer than the last poll. |
| [`tests/test_tools.py`](tests/test_tools.py) | Python decoder formats, rollover, signed values and input validation. |
| [`tests/arm_compile.c`](tests/arm_compile.c) | Standalone ARM compilation probe for the packed core. |

</details>

## Getting started

### 1. Add the core

Copy `LeakTrace.h`, `leaktrace.c` and `LeakTrace_Config.h` into your project and add their directory to the include path. Compile `leaktrace.c` once. All translation units must use the same configuration.

Call `lt_init()` once, before the first traced allocation. Calling it again discards tracking state.

### 2. Connect your allocator

| Your allocator | Integration |
|---|---|
| FreeRTOS heap_4/heap_5 | Include `LeakTrace_FreeRTOS.h` near the end of `FreeRTOSConfig.h`. Initialize LeakTrace before creating dynamically allocated RTOS objects. Existing trace macros must be chained explicitly. |
| libc | Compile `LeakTrace_Stdlib.c`, include `LeakTrace_Stdlib.h`, and use the `lt_*` wrappers. Ordinary `malloc` calls are not intercepted automatically. |
| C++ | Compile `LeakTrace_Cpp.cpp` once and link the C core. Allocations must occur after LeakTrace initialization. |
| Custom allocator | Report successful/failed allocation attempts through `lt_on_alloc_at(ptr, bytes, caller_pc, seconds)` and non-null frees through `lt_on_free(ptr)`. A null allocation pointer records a failure. |

Minimal **single-threaded** libc usage:

```c
#include "LeakTrace.h"
#include "LeakTrace_Stdlib.h"

void app_init(void)
{
    lt_init();
}

void app_work_once(void)
{
    void *buffer = lt_malloc(64u);
    if (buffer != NULL) {
        /* Use the buffer. */
        lt_free(buffer);
    }
}
```

**Synchronization is required in multitask programs.** Serialize allocator operations, tracker events, polling and reads. The core has no internal lock. Configure `LT_STDLIB_ENTER/EXIT` or `LT_CPP_ENTER/EXIT` in the shared config; their defaults are no-ops. Underlying libc thread safety is also the platform's responsibility.

Do not call the monitor from an ISR. Trace each physical allocation at one layer only; tracing both a wrapper and its already-instrumented backend double-counts it.

### 3. Poll with real measurements

Call this periodically, for example once per second, under the same serialization as the allocation hooks:

```c
lt_poll_at(seconds, heap_total, heap_free, heap_min_free);
```

| Argument | Required value |
|---|---|
| `seconds` | Monotonic seconds since startup, not raw RTOS ticks. |
| `heap_total` | Total size of the selected heap, in bytes. |
| `heap_free` | Current free bytes reported by that allocator. |
| `heap_min_free` | Minimum free bytes observed since startup. |

For heap_4, these measurements normally come from `configTOTAL_HEAP_SIZE`, `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()`. For other allocators, use their own measurement APIs.

If heap measurements are unavailable, do not substitute invented numbers. You can still read event counters with `lt_get_stats()` and enumerate live blocks with `lt_next_live()`. Polling is needed to refresh suspects, ages and history.

All adapter time hooks return **seconds**, including `LT_STDLIB_TICK()` and `LT_CPP_TICK()`. Their defaults use the last poll time; configure a current monotonic-seconds source for accurate event timestamps.

## Inspecting memory

Find `g_lt` in the exact firmware's ELF/map file. It has no fixed address. `g_lt.stats` starts at that same address; export **136 bytes** as raw little-endian binary:

```text
python tools/decode_snapshot.py stats.bin
```

The decoder name does not imply a built-in snapshot task: it reads `lt_stats_t`. This standalone package does not define an application-specific `g_lt_snapshot`.

For live records, export from `g_lt.live[0]` and use the count/time from the same consistent capture:

```text
python tools/decode_live.py live.bin --format unpacked --pointer-bits 32 --count 5 --now 120
python tools/resolve_pc.py firmware.elf 0x08001235 --thumb
```

The count, time and PC above are examples; replace them with your capture values. Use `--help` for all options. Decoders require Python 3 only; source-line resolution additionally requires a matching ELF and the toolchain's `addr2line`.

Raw state reads are not atomic. The application must provide a coherent snapshot/capture. Memory placement, SWD access and cache coherence are platform responsibilities; `volatile` alone does not make cached data visible to a probe.

## Default configuration

| Setting | Default |
|---|---|
| Simultaneously tracked live blocks | 64 |
| Record format | Unpacked: full addresses, 32-bit sizes, second-resolution timestamps |
| Core state on a 32-bit target | 1,896 bytes; excludes adapter/runtime stack overhead |
| Suspect age threshold | 1,200 seconds / 20 minutes |
| Ranked suspect entries | 4 |
| Recent history | 60 samples, one every 10 seconds |
| Long history | 180 samples, one every 60 seconds |

Edit `LeakTrace_Config.h` for your RAM budget. History stores heap usage in 32-byte units. Optional `LT_PACKED=1` reduces record storage but limits a block size to 65,535 bytes, uses a caller-PC window relative to `LT_TEXT_BASE`, and quantizes timestamps to 10 seconds. Check `flags` for clipping/range errors.

## Current limitations

- **One global live set and one heap measurement set.** Independent allocator/region statistics and allocator IDs are not implemented. Nested pools need careful instrumentation to avoid double-counting backing storage.
- **Bounded tracking.** When the live table fills, `record_drops` and `flags` report incomplete coverage. History is sampled, not a complete allocation/free event log.
- **Suspects are not proven leaks.** Intentional long-lived buffers and permanent RTOS objects may be ranked. This is not a buffer-overflow or use-after-free detector.
- **Heap percentage is not total SRAM percentage.** Tracked block bytes may differ from heap usage because of allocator metadata/alignment and hook semantics. Task activity, stack usage and total physical SRAM accounting require application instrumentation.
- **Optional adapters need a port.** Caller capture currently uses GCC/Clang facilities. The C++ adapter does not intercept C++17 over-aligned allocations or implement `new_handler` retry behavior; throwing allocation failure throws `std::bad_alloc` when exceptions are enabled, otherwise aborts.
- **Clock limits apply.** The default FreeRTOS time source wraps with 32-bit ticks (about 49.7 days at 1 kHz). Supply an extended monotonic-seconds source for longer runs.
- **Platform support requires validation.** The package does not automatically discover arbitrary allocators, memory regions, cache policies or synchronization mechanisms.

## Tests and validation

With native GCC/G++ and Python 3 installed:

```text
python tests/run.py
python -m unittest discover -s tests -p test_tools.py -v
```

The native C/C++ regression suite and all seven Python tool tests pass. The core has also been cross-compiled without warnings for Cortex-M0+, M3, M4, M7 and M33. This is compilation coverage, not hardware execution across those targets. The 48-hour tests advance simulated timestamps; they are not 48-hour board tests.

Core and adapter source files retain their existing `SPDX-License-Identifier: MIT` notices.
