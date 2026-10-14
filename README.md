# LeakTrace v7

Fixed-storage heap diagnostics for embedded C projects, with optional FreeRTOS, libc and C++ adapters.

LeakTrace records allocation/free events, keeps a live-allocation table, and reports heap measurements supplied by your application. Its core is independent of STM32 HAL, a particular board and an RTOS.

**Start here:** [STM32H743 example](examples/stm32h743/README.md) · [All functions](docs/API.md) · [Settings and RAM sizing](docs/CONFIGURATION.md)

**H743 fault diagnostics:** [Optional crash recorder and register/flag guide](examples/stm32h743/FAULTS.md)
captures HardFault/MemManage/BusFault/UsageFault in RTC backup registers, with a
dedicated emergency stack and an offline CRC-validating decoder.

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
| [`tools/decode_fault.py`](tools/decode_fault.py) | Validates/decodes the optional H743 128-byte crash record, fault flags and flash-ECC caveats. |
| [`.gitignore`](.gitignore) | Excludes generated binaries, memory dumps and Python caches. |
| [`LICENSE`](LICENSE) | MIT license for this project's code and examples. |
| [`docs/API.md`](docs/API.md) | Core/adapter functions, return values and interpretation of results. |
| [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) | Every configuration option, platform hooks and RAM formulas. |
| [`examples/stm32h743/`](examples/stm32h743/README.md) | H743 profile, platform C/header, linker fragment and startup/file-placement guide. Each example file is explained in that guide. |
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
| [`tests/test_h743_fault.c`](tests/test_h743_fault.c) | Fault-record CRC corruption/partial commit and internal-SRAM frame boundaries. |
| [`tests/test_tools.py`](tests/test_tools.py) | Python decoder formats, rollover, signed values and input validation. |
| [`tests/arm_compile.c`](tests/arm_compile.c) | Standalone ARM compilation probe for the packed core. |

</details>

## Getting started

### Which files go into which project?

| Project | Target sources to compile | Headers and placement |
|---|---|---|
| Single-threaded C with libc | `leaktrace.c`, `LeakTrace_Stdlib.c` | Keep core/config/stdlib headers in one library include directory. Call the `lt_*` wrappers. |
| FreeRTOS heap_4/heap_5 | `leaktrace.c` plus your platform poll/clock code | Core/config headers in the library include directory; include `LeakTrace_FreeRTOS.h` at the end of the existing RTOS config. |
| C++ / standard containers | C core plus `LeakTrace_Cpp.cpp` | Core/config headers accessible to both C and C++; initialize before traced allocations. Custom container allocators need their own hooks. |
| Custom allocator / pool | `leaktrace.c` plus your allocator adapter | Put event/time/lock integration in an application/platform source file. |
| ThreadX or another RTOS | Core plus an application-written RTOS/allocator port | A ready-made ThreadX adapter is not supplied. Provide serialization, seconds, allocation events and optional real heap metrics. |

`tools/` runs on the computer, not on the MCU. Exclude `tests/`, unused adapters
and unused examples from the firmware build; do not compile every `.c` file in
this repository automatically. The [H743 walkthrough](examples/stm32h743/README.md)
shows a concrete `Core/Inc`, `Core/Src`, middleware and linker layout.

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

The decoder name does not imply a built-in snapshot task: it reads `lt_stats_t`.
The core does not define `g_lt_snapshot`; the optional H743 example supplies a
cache-published, sequence-checked stats snapshot with that name.

For live records, export from `g_lt.live[0]` and use the count/time from the same consistent capture:

```text
python tools/decode_live.py live.bin --format unpacked --pointer-bits 32 --count 5 --now 120
python tools/resolve_pc.py firmware.elf 0x08001235 --thumb
```

The count, time and PC above are examples; replace them with your capture values. Use `--help` for all options. Decoders require Python 3 only; source-line resolution additionally requires a matching ELF and the toolchain's `addr2line`.

Raw state reads are not atomic. The application must provide a coherent snapshot/capture. Memory placement, SWD access and cache coherence are platform responsibilities; `volatile` alone does not make cached data visible to a probe.

## Configuration: what you can change

Choose values from your application's maximum live allocation count, heap size,
normal allocation lifetime, required history duration and available SRAM.
Configuration is compiled into the firmware; changing a memory value through
SWD does not resize the tracker's arrays or update its configuration.

| Where | What to change |
|---|---|
| [`LeakTrace_Config.h`](LeakTrace_Config.h) | Generic core capacities and project-wide platform hooks. |
| [`LeakTrace_H743_Config.h`](examples/stm32h743/LeakTrace_H743_Config.h) | Selected H743 core profile, heap bytes and example task stacks. |
| [`LeakTrace_H743_FaultConfig.h`](examples/stm32h743/LeakTrace_H743_FaultConfig.h) | Optional fault stack, reset policy and vector ownership. |
| Application / FreeRTOS config / linker | Poll timing, allocator measurements, demo behavior and actual SRAM placement. |

Select the H743 profile for **every C/C++ translation unit** with
`LT_USER_CONFIG_HEADER="LeakTrace_H743_Config.h"`, preserving the quotes in the
actual compiler argument and adding its include directory. With no selected
profile, the shared core defaults apply. The two profiles have different defaults.

Core defaults guarded by `#ifndef` can also be overridden by consistent compiler
definitions. The H743 heap/monitor/worker stack definitions are currently
unconditional: **edit them in the H743 profile**, rather than adding conflicting
`-D` definitions. Fault overrides belong in `FaultConfig.h` or in definitions
shared by the fault C and preprocessed `.S` builds; the assembly does not load
`LT_USER_CONFIG_HEADER`.

<details>
<summary>All core parameters: defaults, limits and selection criteria</summary>

The defaults below are from the shipped shared configuration and H743 profile.
All RAM increments are for a 32-bit target, before alignment padding.

| Parameter | Shared / H743 default | Unit / valid values | How to choose it |
|---|---|---|---|
| `LT_MAX_LIVE` | `64 / 256` | Records; `1..65534` | Cover the maximum simultaneous allocations, including traced RTOS objects, with workload headroom. Each record adds 18 B unpacked or 14 B packed, including its index. |
| `LT_BUCKETS` | `32 / 128` | Positive power of two | More buckets can shorten hash chains; each adds 2 B. Start near half the live capacity, rounded to a power of two, then measure lookup cost. This is a tuning starting point, not a required ratio. |
| `LT_PACKED` | `0 / 0` | `0` unpacked, `1` packed | Use 0 for full addresses, second-resolution time and 32-bit block sizes. Use 1 only when its size/PC/time limits fit; it saves 4 B per live slot on a 32-bit target. |
| `LT_TEXT_BASE` | `0x08000000 / same` | Halfword-aligned address in packed mode | Base of the caller-PC window. Adapt for relocated/external code; saved PCs must fit the encoded approximately 32 MiB window. The live decoder must use the same base. No effect in unpacked mode. |
| `LT_FAST_SLOTS` | `60 / 120` | Samples; `1..65535` | Recent-history capacity; each adds 2 B. Nominal horizon is slots multiplied by interval. |
| `LT_FAST_SECONDS` | `10 / 10` | Positive integer seconds | Recent sampling interval. Lower values show shorter changes but shorten the horizon at the same slot count. |
| `LT_SLOW_SLOTS` | `180 / 2880` | Samples; `1..65535` | Long-history capacity; each adds 2 B. Choose from the required hours/days and sampling interval. |
| `LT_SLOW_SECONDS` | `60 / 60` | Positive integer seconds | Long sampling interval. Larger values extend the horizon without adding slots, at lower time resolution. |
| `LT_HISTORY_QUANTUM` | `32 / 32` | Bytes per unit; positive power of two | History rounds down to this unit. Conservatively choose `65534 * quantum >= maximum heap usage`; larger units cover larger heaps with less precision and no additional RAM. |
| `LT_SUSPECT_SECONDS` | `1200 / 1200` | Positive integer seconds | Age at which a tracked allocation becomes a suspect. Choose above ordinary temporary-buffer lifetimes. Permanent tasks/buffers can still qualify. This is not a proof-of-leak threshold. |
| `LT_TOP_SUSPECTS` | `4 / 8` | Entries; `1..32` | Number of ranked suspect entries retained; each adds 16 B. It changes ranking storage, not the total suspect count or age threshold. |
| `LT_STATE_ATTRIBUTE` | Empty / aligned `.lt_monitor.core` section | Compiler attribute | Controls placement/alignment of `g_lt`; coordinate with the linker, cache policy and initialization. Edit the H743 profile's existing definition when changing it. |
| `LT_USER_CONFIG_HEADER` | Unset / explicitly selected H743 header | Quoted header name | Select one consistent project profile. Different configurations in different translation units can corrupt the shared state. |

Packed mode limits one recorded block to 65,535 bytes and quantizes birth times
to 10 seconds. Its 24-bit time encoding has a finite unambiguous age horizon
of about 2.66 years. It also requires 32-bit allocation addresses. Prefer
unpacked records if large blocks, wide address ranges or precise ages matter.

History arrays cannot be disabled with zero slots in the current implementation;
the minimum is one slot for each ring. A skipped sample is marked missing.
Changing the quantum affects history only, not current heap byte counters.

</details>

<details>
<summary>H743 heap, task timing, demo behavior and linker settings</summary>

| Setting | Current value | Where / selection rule |
|---|---|---|
| `LT_H743_HEAP_BYTES` | `65536` bytes | Edit the H743 profile and keep `configTOTAL_HEAP_SIZE = ((size_t)LT_H743_HEAP_BYTES)`. Size for actual task/queue/buffer demand and allocator overhead. |
| `LT_H743_MONITOR_STACK_WORDS` | `512` words = 2,048 B | H743 profile; this is the monitor task's stack inside the FreeRTOS heap. Check stack high-water marks under the real workload before reducing it. |
| `LT_H743_WORKER_STACK_WORDS` | `256` words = 1,024 B | H743 profile; demo task stack, also inside that heap. Omit the demo task in production when unnecessary. |
| Monitor polling period | `1000` ms | Edit `pdMS_TO_TICKS(1000u)` in `monitor_task()` in `LeakTrace_H743.c`, or call `LT_H743_Poll()` from your own task. No poll-period macro currently exists. |
| Demo block sizes | `64..512` bytes | Edit the `bytes` expression in `worker_task()`. These are workload settings, not tracker capacities. |
| Demo hold / idle time | `2000 / 1000` ms | Edit the worker's delay calls. Longer holds keep real allocations live longer. |
| Monitor / worker priority | Idle priority `+2 / +1` | Edit `LT_H743_StartExampleTasks()`. Both must be below `configMAX_PRIORITIES`; other tasks must allow them to run. |
| Tick rate / tick width | `1000 Hz / 32-bit` in the walkthrough | Application `FreeRTOSConfig.h`. Delays use `pdMS_TO_TICKS`; retain 32-bit ticks and provide monotonic seconds to LeakTrace. |
| Monitor region | 16 KiB at `0x2407C000` | Edit the linker fragment and the main AXI region together. A relocated region must be real, accessible SRAM with no overlap. |
| Main AXI region | 496 KiB at `0x24000000` | Leaves the final 16 KiB reserved. Growing the monitor reservation requires shrinking/repartitioning this region consistently. |
| Cache-line alignment | 32 bytes | Required by the current H743 publication code. Keep object starts and reserved padding aligned; do not shrink it as a RAM optimization. |

Poll no slower than the shortest history interval if you want every bucket
sampled. Faster polling refreshes the snapshot more frequently, but history still
uses its configured intervals. The H743 clock extension must be updated at
least once per tick wrap, about 49.7 days at 1 kHz. Its callers require the same
serialization as allocator events.

The core scans live records while polling. More live slots/records, a larger
suspect ranking and more frequent polls can increase CPU cost. Task stacks,
TCBs and buffers allocated from the heap are already part of the heap arena;
do not add their allocated bytes to that arena again when totaling physical RAM.

</details>

<details>
<summary>All H743 fault-recorder settings and fixed requirements</summary>

| Parameter / choice | Default | What changes |
|---|---|---|
| `LT_H743_FAULT_STACK_BYTES` | `512` bytes | Dedicated ordinary-SRAM emergency stack. Minimum 512; increase in multiples of 32. Apply the identical value to C and assembly and check the actual compiler's stack use. It is separate from both RTOS task stacks. |
| `LT_H743_FAULT_AUTO_RESET` | `1` | `1`: request system reset after capture. `0`: wait for inspection or an already-configured watchdog. This does not configure a watchdog or resume the failed code. |
| `LT_H743_FAULT_OWN_HANDLERS` | `0` | Set to `1` in the assembly build only after resolving the four conflicting generated handler definitions. The actual vectors are checked by `LT_H743_FaultEnable()`. |
| `LT_H743_FaultEnable(traps)` | Application call; use `0u` for no extra traps | Enables routed MemManage/BusFault/UsageFault. Optionally OR `SCB_CCR_DIV_0_TRP_Msk` and/or `SCB_CCR_UNALIGN_TRP_Msk`; unsupported bits are rejected. Alignment trapping can change behavior of existing application code. |
| Record retention / clearing | Preserve until overwritten or explicitly cleared | Call `LT_H743_FaultRead()` to validate/read and `LT_H743_FaultClear()` after consumption. It stores one latest crash, not a growing log. |

The fault record is fixed at **128 bytes in all 32 RTC backup registers**.
There is no record-length, backup-register-offset or multi-record-ring setting.
Changing that format requires coordinated recorder/decoder changes and a format
version change. These registers must be reserved exclusively for this module;
other RTC/boot code must not overwrite them or reset the backup domain.

The two SRAM objects total `LT_H743_FAULT_STACK_BYTES + 32` bytes before
alignment padding. Keep them in normal initialized `.bss`. Increasing the fault
stack does not enlarge retained storage. The 4 KiB Backup SRAM is not used by
this backend. Reset/VBAT retention and physical fault injection remain unverified
on hardware; follow the [fault integration requirements](examples/stm32h743/FAULTS.md).

</details>

<details>
<summary>All adapter hooks and application measurements</summary>

Define hooks in the selected shared profile, with any required function
declarations. Avoid recursively including RTOS headers while its config is
being parsed. Platform code implements the clock and synchronization behavior.

| Hook / input | Default or source | Required contract |
|---|---|---|
| `LT_FREERTOS_PC()` | GCC/Clang caller capture | Return the allocation caller PC as `uintptr_t`. Adapt if the compiler/calling convention requires it. |
| `LT_FREERTOS_SECONDS()` | Ticks divided by tick rate; H743 uses `LT_H743_Seconds()` | Monotonic seconds, not raw ticks. Handle rollover and power modes for your application. |
| `LT_STDLIB_TICK()` | Last poll time | Optional current-seconds callback for accurate libc allocation birth times. |
| `LT_CPP_TICK()` | Last poll time | Same seconds contract for C++ events. |
| `LT_STDLIB_ENTER()` / `LT_STDLIB_EXIT()` | No-ops | Serialize the entire libc allocation/free plus tracker event against polling/reads. |
| `LT_CPP_ENTER()` / `LT_CPP_EXIT()` | No-ops | Same requirement for the C++ adapter; underlying libc must also be safe for the application. |
| Poll `seconds` | Application clock | Monotonic `uint32_t` seconds. A backwards poll is rejected and flagged. |
| Poll `heap_total` | Allocator configuration/API | Actual selected heap capacity in bytes, at most `UINT32_MAX`. |
| Poll `heap_free` | Allocator measurement | Actual free bytes, no greater than total. |
| Poll `heap_min_free` | Allocator low-water measurement | Actual minimum-ever-free bytes, no greater than current free. |

`LT_STDLIB_PC`, `LT_CPP_PC` and their no-inline definitions are implementation
details, not override hooks in this release. Trace each allocation at one layer.
There is no generic allocator-ID/region-selection parameter yet; the core tracks
one live set and one heap measurement set.

</details>

### Example: change the selected H743 profile

Replace these definitions in `LeakTrace_H743_Config.h`, keeping its other
contents, section attribute and clock callback:

```c
#define LT_MAX_LIVE        128u
#define LT_BUCKETS          64u
#define LT_PACKED            0
#define LT_FAST_SLOTS       60u
#define LT_FAST_SECONDS      1u
#define LT_SLOW_SLOTS      240u
#define LT_SLOW_SECONDS     60u
#define LT_HISTORY_QUANTUM  32u
#define LT_SUSPECT_SECONDS 300u
#define LT_TOP_SUSPECTS      8u
```

This gives 128 live slots, a nominal 60-second recent window, a four-hour long
window and a five-minute suspect threshold. Core storage is **3,296 bytes** on
a 32-bit target, compared with **11,128 bytes** for the shipped H743 profile.
The unchanged 148-byte H743 snapshot plus cache-line padding brings the example
monitor section to **3,456 bytes**. The heap arena, clock state, fault module and
runtime stacks are separate costs. These are sizing examples, not a guarantee
that 128 slots or these stack/heap sizes fit every workload.

For other values, the 32-bit core storage formula before alignment is:

```text
136 + LT_MAX_LIVE * (record_bytes + 2) + LT_BUCKETS * 2
    + (LT_FAST_SLOTS + LT_SLOW_SLOTS) * 2 + LT_TOP_SUSPECTS * 16

record_bytes = 16 unpacked, 12 packed
history slots = ceiling(required_duration_seconds / sampling_interval_seconds)
```

The shipped shared defaults use **1,896 bytes**; their packed variant uses
**1,640 bytes**. Native 64-bit builds have different layouts. Always confirm
the actual map/`sizeof` result and keep the decoder format consistent.

<details>
<summary>Which measurements should drive your choices?</summary>

| Observation / criterion | Meaning and action |
|---|---|
| `max_live_count`, `record_drops`, `LT_FLAG_CAPACITY` | Size the live table from peak workload, not average allocation rate. If drops occurred, the recorded peak may understate the true requirement; enlarge and measure again. |
| `peak_probe_steps` | Evidence of long hash lookup chains; evaluate more buckets or a lower table load while measuring RAM/CPU cost. |
| `alloc_failures`, `LT_FLAG_ALLOC_FAIL` | The allocator failed an allocation. Increasing tracker slots does not increase the application's heap. |
| `heap_low_water_free_bytes`, `heap_peak_used_bytes` | Allocator headroom evidence. Test realistic bursts and account for metadata/fragmentation; free-byte totals alone do not prove that a large contiguous allocation can succeed. |
| `suspect_count`, `oldest_live_seconds`, ranked suspects | Compare age with normal object lifetimes and intentional permanent allocations. Increasing the threshold delays classification; it does not repair a leak. |
| `LT_FLAG_HISTORY_RANGE` | Increase history quantum if the heap range does not fit, accepting reduced history precision. |
| `LT_FLAG_SIZE_CLIPPED`, `LT_FLAG_PC_RANGE`, `LT_FLAG_ADDRESS_RANGE` | Check packed limits and decoder/code base; prefer unpacked records when needed. |
| `LT_FLAG_TIME_INVALID`, `LT_FLAG_HEAP_INVALID` | Correct clocks or measurement inputs; do not replace them with plausible constants. |
| `LT_FLAG_DUPLICATE`, `LT_FLAG_UNMATCHED_FREE` | Investigate duplicate instrumentation, allocations made before init, dropped records or mismatched allocation/free paths. |
| `LT_FLAG_CORRUPT_INDEX` | Investigate state corruption/configuration mismatch. More capacity alone is not a repair. |
| Task stack high-water marks | Application/RTOS measurement, not an automatic core feature. Check the actual stack units before converting to bytes. |

Heap percentage is `heap_used_percent_x100 / 100.0`, relative to the selected
heap. It is not total device SRAM usage. `live_traced_bytes` sums recorded block
sizes and may differ from actual heap occupancy. Counters/flags are measured
outputs, not configuration values to edit through SWD. Diagnostic flags are
sticky; `lt_init()` discards the tracking session rather than just clearing a
warning. `lt_set_heap_baseline()` changes the growth reference after a valid
poll; it does not exempt existing allocations from suspect classification.

There are no built-in heap-percentage alarm, minimum-free alarm, task-CPU alarm
or event-log-size macros in this release. Define those application policies
around real measurements if needed; the current core history is sampled heap
usage, and the optional fault backend keeps one latest crash.

</details>

<details>
<summary>Computer-tool options that must match your capture</summary>

| Tool | User-selectable inputs/options |
|---|---|
| `decode_snapshot.py` | Dump file, `--offset`; point at the 136-byte `lt_stats_t` (H743 snapshot stats start at offset `0x0C`). |
| `decode_live.py` | Dump file, `--count`, `--now`, `--format`, `--pointer-bits`, `--text-base`, `--elf`, `--tool`, `--limit`, `--all`. Count/time/layout must match a consistent capture; display limits do not change MCU tracking capacity. |
| `resolve_pc.py` | Matching ELF, caller PCs, `--tool`, `--thumb`. The Thumb return-PC adjustment is for allocation call sites, not a saved exception PC. |
| `decode_fault.py` | Fault dump file, `--offset`, `--json`; the record size/version/CRC format is fixed. |

Use each tool's `--help` for syntax. A map address, binary offset, record count,
PC base or display limit does not change firmware configuration. Keep magic,
version, record fields, `LT_IMPLEMENTATION` ownership and fixed hardware
addresses unchanged during ordinary tuning. Clean-rebuild and recheck the map
after layout changes; reprogram the matching firmware and use its matching ELF.

</details>

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

The eight native C/C++ regression executables and all thirteen Python tool tests pass. The core has also been cross-compiled without warnings for Cortex-M0+, M3, M4, M7 and M33. This is compilation coverage, not hardware execution across those targets. The 48-hour tests advance simulated timestamps; they are not 48-hour board tests.

The H743 integration source compiles with ARM GCC 14.3 (`-Wall -Wextra -Werror`),
CMSIS 5.9.0, ST H7 device headers and FreeRTOS-Kernel V10.4.6. A limited linker
probe confirms its section layout and sizes. A complete Cube firmware build and
execution on an H743 board have not been verified.
The optional fault recorder also passes an ARM vector/layout link probe; actual
fault injection, reset retention and VBAT operation have not been tested.

<details>
<summary>Sources, acknowledgements and licensing</summary>

<p><sub>LeakTrace code, documentation and example integration files: <a href="LICENSE">MIT</a>, copyright 2026 TheLouisMahdi. Existing SPDX notices are retained. External integration dependencies and technical references are listed below with their separate licenses; their implementations are not bundled.</sub></p>

<p><sub><a href="https://github.com/FreeRTOS/FreeRTOS-Kernel">FreeRTOS-Kernel</a>: <a href="https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/LICENSE.md">MIT</a>. Allocation hooks, heap_4/heap_5 behavior and RTOS APIs; see the <a href="https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/01-Memory-management">official heap documentation</a>.</sub></p>

<p><sub><a href="https://github.com/ARM-software/CMSIS_5">Arm CMSIS_5</a>: <a href="https://github.com/ARM-software/CMSIS_5/blob/5.9.0/LICENSE.txt">Apache-2.0</a>. Cortex-M definitions, cache-maintenance interfaces and barriers. <a href="https://github.com/STMicroelectronics/cmsis-device-h7">ST CMSIS device H7</a>: <a href="https://github.com/STMicroelectronics/cmsis-device-h7/blob/master/LICENSE.md">Apache-2.0</a>. <a href="https://github.com/STMicroelectronics/stm32h7xx-hal-driver">ST H7 HAL</a>: <a href="https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/LICENSE.md">BSD-3-Clause</a>, an external Cube-project dependency/reference.</sub></p>

<p><sub>ST technical references: <a href="https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf">RM0433</a> for the H743 memory map, <a href="https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf">AN4839</a> for cache coherence, and <a href="https://www.st.com/resource/en/user_manual/um2237-stm32cubeprogrammer-software-description-stmicroelectronics.pdf">UM2237</a> for CubeProgrammer. These documents remain subject to their publishers' copyright/terms; they are linked, not redistributed under this project's MIT license.</sub></p>

<p><sub>H743 fault references: <a href="https://www.st.com/resource/en/programming_manual/pm0253-stm32f7-series-and-stm32h7-series-cortexm7-processor-programming-manual-stmicroelectronics.pdf">ST PM0253</a>, <a href="https://www.st.com/resource/en/errata_sheet/es0392-stm32h742x743xig-st32h750xb-and-stm32h753xi-device-errata-stmicroelectronics.pdf">ST ES0392</a>, <a href="https://www.st.com/content/st_com/en/technical-documents/an5342--how-to-use-error-correction-code-ecc-management-for-internal-memories-protection-on-stm32-mcus-stmicroelectronics_asset/chunk-001-topic-an5342-ecc-overview.html">ST AN5342</a>, and the <a href="https://documentation-service.arm.com/static/61efd6602dd99944d051417b">Arm Cortex-M7 user guide</a>. Publisher terms apply. The recorder is original integration code; no third-party fault-handler implementation is included.</sub></p>

<p><sub><a href="https://sourceware.org/newlib/libc.html">newlib</a>: libc allocation/reentrancy reference; <a href="https://sourceware.org/newlib/COPYING.NEWLIB">mixed per-file licenses</a>. <a href="https://www.gnu.org/software/binutils/">GNU Binutils</a>: external addr2line/linker tools, GPL-3.0-or-later; the <a href="https://sourceware.org/binutils/docs/ld.html">ld manual</a> states GNU FDL 1.3. No Binutils or newlib implementation is bundled.</sub></p>

<p><sub><a href="https://github.com/cplusplus/draft">C++ working draft</a> / <a href="https://eel.is/c++draft/vector.capacity">vector capacity clauses</a>: language/container behavior references only; no repository-wide open-source license is asserted for the standards text. <a href="https://docs.python.org/3/license.html">Python</a>: external tool runtime, PSF license terms.</sub></p>

</details>
