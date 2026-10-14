# STM32H743 + FreeRTOS heap_4 example

This example adds LeakTrace to an **existing** CubeMX/CubeIDE project. It provides
integration files, not a complete board firmware, clock configuration or startup
project. Keep the application's normal STM32H743 CMSIS/NVIC settings.

The example uses a 64 KiB heap_4 arena, a 256-record tracker, and a cache-published
stats snapshot. An optional worker repeatedly allocates 64..512 bytes, holds each
block for two seconds, frees it, then waits one second. The monitor polls at 1 Hz.

For the optional H743-specific HardFault/MemManage/BusFault/UsageFault recorder,
see [fault integration, register-by-register flags and limitations](FAULTS.md).
It reserves RTC backup registers separately; it is not enabled by this heap demo.

## 1. Put the files in the project

For a project that already has `Core/Inc`, `Core/Src`, `main.c`, `freertos.c`,
FreeRTOS and the STM32H743 drivers, use this layout:

```text
YourProject/
  Core/Inc/
    FreeRTOSConfig.h                 existing generated config
    LeakTrace_H743.h                 copy from this example
    LeakTrace_H743_Config.h          copy from this example
  Core/Src/
    main.c                          existing application startup
    freertos.c                      existing generated RTOS setup, if used
    LeakTrace_H743.c                 copy from this example
  Middlewares/Third_Party/LeakTrace/
    LeakTrace.h                     copy from repository root
    leaktrace.c                     compile once
    LeakTrace_Config.h              copy from repository root
    LeakTrace_FreeRTOS.h             copy from repository root
  linker/
    leaktrace_sections.ld            copy from this example
  STM32H743xx_FLASH.ld               existing main linker script
```

Add `Core/Inc` and `Middlewares/Third_Party/LeakTrace` to the include paths.
Compile `leaktrace.c` and `LeakTrace_H743.c`; keep the generated HAL/RTOS files.
Do not compile the libc/C++ adapters for this heap_4-only example.

For **every** C/C++ translation unit, select the profile:

```text
-DLT_USER_CONFIG_HEADER="LeakTrace_H743_Config.h"
```

## 2. Configure FreeRTOS

Keep the project's other generated settings. Select heap_4, 32-bit ticks and a
1 kHz tick rate. Ensure delay, delay-until and task deletion APIs are enabled.
The last is used to clean up a partially created pair of example tasks.

Set the existing heap-size definition to:

```c
#define configTOTAL_HEAP_SIZE ((size_t)LT_H743_HEAP_BYTES)
```

Near the end of `FreeRTOSConfig.h`, inside its user section, add:

```c
#include "LeakTrace_FreeRTOS.h"
```

The selected profile defines `LT_H743_HEAP_BYTES` as 65536. It also makes all
FreeRTOS allocation timestamps use `LT_H743_Seconds()`. Do not define the same
trace macros a second time. The example's scheduler lock is suitable for the
single-core heap_4 hooks used here; no allocations/tracker calls from ISRs.

## 3. Reserve space in AXI SRAM

STM32H743 AXI SRAM occupies `0x24000000..0x2407FFFF` (512 KiB). This example
reserves its final 16 KiB for monitor data. Check the device memory map and your
existing linker layout before applying it.

In the main linker script, reduce the existing AXI RAM region to 496 KiB:

```ld
RAM_D1 (xrw) : ORIGIN = 0x24000000, LENGTH = 496K
```

Use your script's actual name for that region; keep other SRAM banks unchanged.
Then add `leaktrace_sections.ld` as an additional linker script **after** the main
script, for example `-TSTM32H743xx_FLASH.ld -Tlinker/leaktrace_sections.ld`.
Alternatively merge its memory/section definitions into the corresponding
blocks of the main script. Never leave a second 512 KiB region covering the
reserved tail: overlapping MEMORY definitions are not a reservation.

The `.lt_monitor` section is `NOLOAD`; it is initialized explicitly by
`LT_H743_Init()`. It must be writable Normal memory under any application MPU
configuration. Both objects start on 32-byte cache-line boundaries, with end
padding supplied by the linker. Polling cleans dirty cache lines into SRAM and
uses barriers so SWD sees the published snapshot. D-cache may remain enabled.

| Item | Size / location |
|---|---|
| Main AXI region after reservation | 496 KiB at `0x24000000` |
| Monitor reservation | 16 KiB at `0x2407C000..0x2407FFFF` |
| `g_lt`, this profile | 11,128 bytes |
| `g_lt_snapshot` | 148 bytes; 12-byte header plus 136-byte statistics |
| Occupied monitor section including alignment | 11,296 bytes |
| Clock bookkeeping in normal `.bss` | 12 bytes, excluding any linker padding |
| FreeRTOS heap | 65,536 bytes in the application's normal `.bss` placement |
| Monitor task stack | 512 words = 2,048 bytes, allocated **inside** that heap |
| Worker task stack | 256 words = 1,024 bytes, also inside that heap |

TCBs, other tasks, queues, application globals and MSP stack require additional
space. Do not add task stack allocations to the heap arena a second time when
counting physical SRAM. Check the **complete application's** map and stack
high-water marks; the figures above are not a complete firmware RAM total.

## 4. Connect the actual startup path

Add this include in the `main.c` user-includes section:

```c
#include "LeakTrace_H743.h"
```

After the application's HAL/clock/cache initialization and before any traced
allocations, in the user section before `osKernelInitialize()`, call:

```c
LT_H743_Init();
```

After `osKernelInitialize()` and before `osKernelStart()`, in the user task-creation
section, add:

```c
if (!LT_H743_StartExampleTasks()) {
    Error_Handler();
}
```

If CubeMX puts task creation in `MX_FREERTOS_Init()` instead, add the call there
once and verify that `main()` actually calls that function. Do not create a
second `main()` or duplicate task implementation. Existing tasks must block/yield
so the example tasks can run.

For production, omit `LT_H743_StartExampleTasks()` and call `LT_H743_Poll()` once
per second from an existing task. Your real heap_4 allocations are already
reported by the trace macros; no extra event calls are needed around them.

| Example function | Use |
|---|---|
| `LT_H743_Init()` | Initialize core, clock and snapshot once before traced allocations. |
| `LT_H743_StartExampleTasks()` | Create the optional monitor/worker pair before scheduler start; returns 1 on success, 0 on failure. |
| `LT_H743_Poll()` | Refresh real heap measurements and publish a snapshot from task context. |
| `LT_H743_Seconds()` | Extended tick-to-seconds callback used by hooks/poll under their shared serialization. |

## 5. Read the results over SWD

After programming and starting the firmware, connect CubeProgrammer with
ST-LINK/SWD in Hot Plug mode. Find symbol addresses in the **matching** ELF/map.

With the supplied profile and linker fragment, a linker probe produced:

| Symbol / range | Address | Size | Purpose |
|---|---|---|---|
| `g_lt` | `0x2407C000` | `0x2B78` / 11,128 B | Core statistics, live records, indexes, histories and suspect ranking. |
| `g_lt_snapshot` | `0x2407EB80` | `0x94` / 148 B | Published header and statistics. |
| `g_lt_snapshot.stats` | `0x2407EB8C` | `0x88` / 136 B | Statistics accepted by `decode_snapshot.py`. |
| `.lt_monitor` occupied range | `0x2407C000..0x2407EC1F` | `0x2C20` / 11,296 B | Both objects including cache-line padding. |

Recheck these addresses after changing capacities or section contents. For a
whole-snapshot read, set **Address = the map's `g_lt_snapshot` address**,
**Size = `0x94` bytes**, **Data width = 32-bit**, and click **Read**. Read again
after one second to observe changes. For the sequence checks below, read one
32-bit word at `g_lt_snapshot + 4`; the size for that read is `0x04` bytes.

| Read | What to expect |
|---|---|
| `g_lt_snapshot.magic` | `0x4C545331` after a successful poll |
| `g_lt_snapshot.sequence` | Increasing; odd during publication, even when complete |
| `g_lt_snapshot.uptime_seconds` | Increasing seconds |
| `g_lt_snapshot.stats` | Measured heap usage, counters and age-based suspect counts |

Read the sequence before and after a capture; accept only matching even values
with the expected magic. The raw `g_lt` live table is **not** protected by that
snapshot sequence: allocation hooks can change it between polls. Capture it
under application serialization or while the target is stopped if you need a
consistent full live set.

Export the entire 148-byte example snapshot to `capture.bin`, then decode its
statistics at byte offset 12:

```text
python tools/decode_snapshot.py capture.bin --offset 0x0C
```

Heap percentage uses the **64 KiB FreeRTOS heap**, not AXI SRAM or total device
RAM. Task/queue allocations may become age-based suspects after 20 minutes;
that alone does not indicate a leak. This example does not deliberately leak.

## Adjusting it

Edit `LeakTrace_H743_Config.h` to change live capacity, history and stack sizes.
Change `LT_H743_HEAP_BYTES` to resize the heap and check the map again. If the
tracker outgrows 16 KiB, the linker rejects it; enlarge the reservation and reduce
the application's AXI region correspondingly. Keep cache alignment at 32 bytes.

See [all settings and RAM formulas](../../docs/CONFIGURATION.md) and the
[function reference](../../docs/API.md).

## Validation status

The port and core compile with ARM GCC 14.3, CMSIS 5.9.0,
FreeRTOS-Kernel V10.4.6 and ST H7 device headers at commit
`81db1ec63cdc191fae1565b772da3ea5aa29a683`, with warnings treated as errors.
A minimal linker probe using the supplied profile/fragment confirms the sizes
and addresses above. That probe retains initialization and monitor storage;
it does not link a complete HAL/RTOS application. No H743 hardware execution,
SWD capture or cache behavior has been verified on a physical board.
