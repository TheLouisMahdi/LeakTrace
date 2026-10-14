# Configuration and RAM sizing

All translation units must see identical settings. Either edit the shared
`LeakTrace_Config.h`, or select a project profile for **all C/C++ source files**:

```text
-DLT_USER_CONFIG_HEADER="LeakTrace_H743_Config.h"
```

Add that profile's directory to the project's include paths. A compiler argument
must preserve the quotes so the macro expands to a header name. In a CubeIDE
preprocessor-symbol field, enter `LT_USER_CONFIG_HEADER=\"LeakTrace_H743_Config.h\"`
if that field requires escaping quotes; inspect the generated compiler command.
Do not try to select a profile only by changing include-path order: the core
header includes its adjacent shared config first.

## Settings

| Setting | Default | Effect |
|---|---|---|
| `LT_MAX_LIVE` | 64 | Maximum simultaneous live records, 1..65534. More records use more RAM. |
| `LT_BUCKETS` | 32 | Hash bucket count; must be a power of two. More buckets can reduce lookup chains. |
| `LT_PACKED` | 0 | Full records by default; 1 selects compact records with explicit range limits. |
| `LT_TEXT_BASE` | `0x08000000` | Caller-PC reference base in packed mode only. Adapt for relocated/external code. |
| `LT_FAST_SLOTS` | 60 | Recent-history sample capacity, 1..65535. |
| `LT_FAST_SECONDS` | 10 | Recent sampling interval in seconds; nonzero. |
| `LT_SLOW_SLOTS` | 180 | Long-history sample capacity, 1..65535. |
| `LT_SLOW_SECONDS` | 60 | Long sampling interval in seconds; nonzero. |
| `LT_HISTORY_QUANTUM` | 32 | Bytes per stored history unit; must be a power of two. Larger values cover more memory with less precision. |
| `LT_SUSPECT_SECONDS` | 1200 | Age threshold for suspect ranking; nonzero. |
| `LT_TOP_SUSPECTS` | 4 | Ranked suspect capacity, 1..32. |
| `LT_STATE_ATTRIBUTE` | empty | Optional section/alignment attribute for `g_lt`; the linker and initialization must match. |
| `LT_USER_CONFIG_HEADER` | unset | Optional project-wide configuration header. |

Poll at least as often as the desired sampling interval. A one-second poll can
refresh counters/ages each second while the two histories still sample every
10 and 60 seconds. Missed buckets get explicit missing markers.

History uses 16-bit units with `65535` reserved for missing readings. Its maximum
decoded valid sample is `65534 * LT_HISTORY_QUANTUM` bytes. For large heaps,
increase the quantum or history will report range errors/missing samples. This
does not change the unquantized current heap counters.

## RAM formula on a 32-bit target

```text
sizeof(g_lt) = 136
             + LT_MAX_LIVE * (record_bytes + 2)
             + LT_BUCKETS * 2
             + (LT_FAST_SLOTS + LT_SLOW_SLOTS) * 2
             + LT_TOP_SUSPECTS * 16

record_bytes = 16 when LT_PACKED=0; 12 when LT_PACKED=1
```

These sizes apply to the current v7 ABI on 32-bit targets; confirm with
`sizeof(lt_state_t)` and your map. Native 64-bit builds have different record
sizes. The formula excludes alignment padding, task stacks and any extra
application snapshot.

| Profile | Live / buckets | Fast / slow slots | Top | Packed | Core RAM |
|---|---|---|---|---|---|
| Default | 64 / 32 | 60 / 180 | 4 | 0 | 1,896 B |
| Compact default | 64 / 32 | 60 / 180 | 4 | 1 | 1,640 B |
| H743 example | 256 / 128 | 120 / 2880 | 8 | 0 | 11,128 B |

The H743 profile retains 20 minutes of recent history and 48 hours of long
history. These are configured ring horizons, not claims of hardware endurance
testing. Its separate 148-byte stats snapshot and 32-byte section alignment
bring occupied monitor storage to 11,296 bytes inside a 16 KiB reservation.

Packed records limit block size to 65535 bytes, quantize birth time to 10 seconds,
and encode caller PCs in a 32 MiB window relative to `LT_TEXT_BASE`. Check range
flags; choose unpacked records when these limits do not fit the application.

## Platform hooks

| Hook | Purpose |
|---|---|
| `LT_FREERTOS_PC()` | Capture the allocator caller PC; GCC/Clang implementation supplied. |
| `LT_FREERTOS_SECONDS()` | FreeRTOS event clock; default is ticks divided by tick frequency. |
| `LT_STDLIB_ENTER()` / `LT_STDLIB_EXIT()` | Serialize libc allocation plus tracking; defaults are no-ops. |
| `LT_STDLIB_TICK()` | libc event clock, in **seconds** despite the name. |
| `LT_CPP_ENTER()` / `LT_CPP_EXIT()` | Serialize C++ allocation plus tracking; defaults are no-ops. |
| `LT_CPP_TICK()` | C++ event clock, also in seconds. |

Introduce any function declarations needed by a hook in the shared profile.
Avoid including an RTOS header recursively from a profile loaded while
`FreeRTOSConfig.h` itself is being parsed. The H743 profile declares a clock
callback and implements it in the platform `.c` file for this reason.

The H743 example extends tick differences using a 64-bit accumulator; updates
must occur at least once per 32-bit tick wrap. Its callbacks are serialized by
the same scheduler lock as heap_4. This does not supply a generic multicore or
ISR synchronization strategy.
