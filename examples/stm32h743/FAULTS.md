# STM32H743 fault recorder

This optional port records **HardFault, MemManage, BusFault and UsageFault**
without a debug session. It uses 128 bytes of RTC backup registers and 544
bytes of ordinary SRAM before linker padding: a 512-byte emergency stack and
32-byte status object.
It does not allocate from the heap or call HAL/FreeRTOS in a fault.

The portable LeakTrace heap core is unchanged. Fault diagnostics are a separate
module; they identify processor exceptions, not proven memory leaks.

## Files and integration

| File | Add to | Purpose |
|---|---|---|
| `LeakTrace_H743_Fault.h` | `Core/Inc` | Startup/read/clear API and status type. |
| `LeakTrace_H743_FaultConfig.h` | `Core/Inc` | Handler ownership, stack size and reset policy. |
| `LeakTrace_H743_FaultFormat.h` | `Core/Inc` | Versioned wire format, CRC and frame bounds. |
| `LeakTrace_H743_Fault.c` | `Core/Src` | Register capture and RTC storage. |
| `LeakTrace_H743_FaultEntry.S` | `Core/Src` | Exception entry and optional vector symbols. Uppercase `.S` is required for preprocessing. |
| `tools/decode_fault.py` | Computer only | CRC validation and fault interpretation. |

Compile the fault C file with the project's normal CPU/float ABI options plus:

```text
-mgeneral-regs-only -fno-stack-protector -fno-lto -ffreestanding -fno-builtin
```

These options prevent floating-point/lazy-stacking side effects and runtime
library instrumentation on the crash path. Keep this file out of LTO. Apply
configuration overrides consistently to C and assembly. GNU toolchains may
need `-Wl,-z,noexecstack` when mixing this assembly with vendor objects lacking
GNU-stack metadata; this is ELF metadata, not an MCU MPU configuration.

Both SRAM objects use normal `.bss` and 32-byte alignment. Keep them covered by
the application's C runtime zero-initialization, before enabling D-cache.
Do not move the emergency stack into uninitialized `NOLOAD` SRAM: a first stack
access to uninitialized H7 ECC memory can itself fault. No additional linker
script is required for this fault module.

**Reserve all `RTC_BKP0R..RTC_BKP31R` for this module.** Existing RTC setup,
bootloaders and other libraries must not use them. No RTC calendar clock or
LSE setup is performed here. Backup SRAM's `BREN` is not required for these
backup *registers*. The separate 4 KiB Backup SRAM remains available to the app.

To route the exceptions, define `LT_H743_FAULT_OWN_HANDLERS=1` for the assembly
build, and remove/exclude the four existing definitions of `HardFault_Handler`,
`MemManage_Handler`, `BusFault_Handler` and `UsageFault_Handler` from the
generated interrupt source. Keep all other interrupt handlers unchanged.
Review this after CubeMX regeneration. Duplicate definitions cause a link error;
the module does not silently override existing handlers. The default is **0**.

The assembly captures original MSP, PSP, EXC_RETURN and PRIMASK before a C
prologue, disables maskable interrupts, switches MSP to its emergency stack,
then enters C. A C call from inside an ordinary generated handler is unsafe:
its prologue may already have changed MSP/LR. NMI is not replaced automatically.

After normal C runtime initialization, in privileged startup, after the last
application backup-domain reset and before any reset-status clear:

```c
#include "stm32h7xx.h"
#include "LeakTrace_H743_Fault.h"

/* In the existing main() startup USER CODE section: */
if (!LT_H743_FaultInit() || !LT_H743_FaultEnable(0u)) {
    Error_Handler();
}

/* Optionally read the previous crash; it is NOT erased automatically. */
lt_h743_fault_record_t previous;
if (LT_H743_FaultRead(&previous)) {
    /* Consume/export previous.words[] using the LTF_* field indexes. */
}
```

`FaultEnable` checks the actual `SCB->VTOR` entries before enabling configurable
faults. A missing vector route returns 0. It enables `MEMFAULTENA`, `BUSFAULTENA`
and `USGFAULTENA`; it does not enable divide-by-zero or alignment traps unless
requested. For example, `LT_H743_FaultEnable(SCB_CCR_DIV_0_TRP_Msk)` additionally
enables divide-by-zero faults. `UNALIGN_TRP` is an explicit application choice
because existing code may intentionally use supported unaligned accesses.

`FaultInit` sets `PWR_CR1.DBP` with a bounded readiness check and leaves it set.
It reads RCC reset flags without clearing them. It initializes the normal SRAM
status, not the retained record. Call it again after any backup-domain reset.
Use `LT_H743_FaultClear()` only after consuming a record. Read/clear APIs are
for serialized startup/task context, not ISR or fault context.

| API | Result |
|---|---|
| `LT_H743_FaultInit()` | 1 when backup write access is ready; 0 on timeout. It does not prove that vectors are installed. |
| `LT_H743_FaultEnable(traps)` | 1 after enabling the routed exceptions; 0 for missing routes/storage or unsupported trap bits. |
| `LT_H743_FaultRead(&record)` | 1 for a magic/version/CRC-valid record; 0 otherwise, including null output. |
| `LT_H743_FaultClear()` | Invalidates the saved signature explicitly. |

`g_lt_fault_status.handler_mask` bits 0/1/2/3 describe HardFault/MemManage/
BusFault/UsageFault routes; all installed is `0x0F`. `write_verified` is an
in-boot readback result and is cleared by initialization after reset.

## Capture and retention behavior

The handler commits a register-only record first. It then considers the saved
stack frame only if EXC_RETURN is recognized, the SP is 8-byte aligned, the
complete basic/FP frame fits an internal SRAM bank, and no stacking/unstacking/
lazy-preservation error is flagged. External RAM and ITCM stacks are not read.
The eight core registers start at SP for **both** frame formats; no erroneous
18-word offset is added for an FP frame. FP register contents are not captured.

If xPSR.T is valid, a second commit adds R0..R3, R12, LR, PC and xPSR. A failed
frame check leaves the first record with `FRAME_REJECTED`; zero register fields
must not be interpreted as a real PC/LR. `IMPRECISERR` means a valid stacked PC
can still be unrelated to the offending instruction.

Each write invalidates the signature, writes the payload/CRC, then commits the
signature last with barriers. The handler checks register readback. The CRC is
CRC-32/ISO-HDLC over bytes 0..123. This is **one latest record**, not a journal:
an interrupted overwrite can lose the old record; a partial record is rejected.

Default `LT_H743_FAULT_AUTO_RESET=1` requests a system reset after capture.
Set it to 0 to wait for inspection/watchdog reset. No `BKPT`, semihosting,
printf, heap scan, RTOS lock, source lookup or flash programming occurs in the
fault handler. There is no automatic retry/resume of a faulting instruction.

`LT_H743_FAULT_STACK_BYTES` defaults to 512 and must be at least 512, in multiples
of 32. Increasing it raises ordinary SRAM use; it does not enlarge the retained
128-byte record. These settings are independent of the heap/history profile.

Backup registers retain data across system reset and, with valid VBAT supply,
loss of VDD. Backup-domain reset, tamper behavior or loss of backup supply can
erase it. `.noinit` alone would not supply that guarantee. The recorder does
not change tamper settings, watchdogs, RTC clock selection or power policy.

## Read Memory and wire format

After reboot, connect ST-LINK/SWD in **Hot Plug** mode without resetting:

| Setting | Value |
|---|---|
| Address | `0x58004050` (`RTC_BKP0R`) |
| Size | `0x80` bytes |
| Data width | `32-bit` |
| Export | Raw binary, little-endian |

```text
python tools/decode_fault.py crash.bin
python tools/decode_fault.py crash.bin --json
```

Accept a record only if magic, version and CRC pass. Another crash can overwrite
it while reading; recapture on CRC failure. The RTC record is Device memory,
so D-cache cleaning of a normal SRAM snapshot is not needed for this export.
The normal `.bss` status object is not a cache-published SWD snapshot.

Resolve a trusted saved PC with `arm-none-eabi-addr2line -a -f -C -e firmware.elf
0x08001234`, substituting the actual PC and matching firmware ELF. Do not
subtract two from the exception PC as if it were a function return address.
There is no build ID in this compact record; retain the matching ELF externally.

| Offset | Field | Saved address |
|---|---|---|
| `0x00` | Magic `0x4C544631` | `0x58004050` |
| `0x04/08/0C` | Version / sequence / flags | `0x58004054/58/5C` |
| `0x10` | Reset flags entering the crashed boot | `0x58004060` |
| `0x14/18/1C` | EXC_RETURN / MSP / PSP | `0x58004064/68/6C` |
| `0x20/24` | PRIMASK / CONTROL | `0x58004070/74` |
| `0x28/2C/30/34` | CFSR / HFSR / DFSR / ABFSR | `0x58004078/7C/80/84` |
| `0x38/3C` | MMFAR / BFAR | `0x58004088/8C` |
| `0x40/44/48` | SHCSR / ICSR / device ID | `0x58004090/94/98` |
| `0x4C/50/54/58` | FLASH SR1 / SR2 / ECC_FA1 / ECC_FA2 | `0x5800409C/A0/A4/A8` |
| `0x5C..0x6C` | R0 / R1 / R2 / R3 / R12 | `0x580040AC..BC` |
| `0x70/74/78` | LR / PC / xPSR | `0x580040C0/C4/C8` |
| `0x7C` | CRC | `0x580040CC` |

Flags bits 0..7: `FRAME_VALID`, `FRAME_REJECTED`, `FP_EXTENDED`, `BFAR_VALID`,
`MMFAR_VALID`, `FLASH_ECC` (double error), `STACK_ERROR`, `PC_IMPRECISE`.
Bits 24:16 hold the active exception number (3/4/5/6 for the installed vectors).
The sequence counts committed crash attempts since the last valid record; it
is not a timestamp, boot counter or unique firmware/build identifier.

## Register/flag research catalog

Addresses here are **live H743 registers**, distinct from the saved addresses.
The recorder observes status; it does not clear peripheral error flags.

| Register | Address | Relevant fields | Current use |
|---|---|---|---|
| SHCSR | `0xE000ED24` | `MEMFAULTENA[16]`, `BUSFAULTENA[17]`, `USGFAULTENA[18]` | Explicit enable; raw capture. |
| CCR | `0xE000ED14` | `UNALIGN_TRP[3]`, `DIV_0_TRP[4]` | Opt-in traps. |
| CFSR | `0xE000ED28` | MMFSR / BFSR / UFSR below | Raw capture + decoder. |
| HFSR | `0xE000ED2C` | `VECTTBL[1]`, `FORCED[30]`, `DEBUGEVT[31]` | Raw capture + decoder. |
| DFSR | `0xE000ED30` | `HALTED/BKPT/DWTTRAP/VCATCH/EXTERNAL[0..4]` | Raw capture + decoder. |
| MMFAR / BFAR | `0xE000ED34/38` | Address, gated by validity flags | Conditional interpretation. |
| ABFSR | `0xE000EFA8` | `ITCM/DTCM/AHBP/AXIM/EPPB[0..4]`, `AXIMTYPE[9:8]` | M7 asynchronous bus diagnostics. |
| RCC_RSR | `0x580244D0` | Reset flags below; `RMVF[16]` clears | Boot capture; no automatic clear. |
| DBGMCU_IDCODE | `0x5C001000` | `DEV_ID[11:0]`, `REV_ID[31:16]` | Capture for errata review. |
| FLASH_SR1 / SR2 | `0x52002010/0x52002110` | `SNECCERR[25]`, `DBECCERR[26]` | Raw capture; double-ECC warning. |
| FLASH_ECC_FA1 / FA2 | `0x52002060/0x52002160` | Encoded failing address | Raw capture; bank/revision interpretation required. |

| CFSR bit | Flag | Meaning |
|---|---|---|
| 0 | IACCVIOL | Instruction access violation |
| 1 | DACCVIOL | Data access violation |
| 3 | MUNSTKERR | MemManage unstacking error |
| 4 | MSTKERR | MemManage stacking error |
| 5 | MLSPERR | MemManage lazy FP error |
| 7 | MMARVALID | MMFAR valid |
| 8 | IBUSERR | Instruction bus error |
| 9 | PRECISERR | Precise data bus error |
| 10 | IMPRECISERR | Asynchronous data bus error |
| 11 | UNSTKERR | Bus unstacking error |
| 12 | STKERR | Bus stacking error |
| 13 | LSPERR | Bus lazy FP error |
| 15 | BFARVALID | BFAR valid flag |
| 16 | UNDEFINSTR | Undefined instruction |
| 17 | INVSTATE | Invalid execution state |
| 18 | INVPC | Invalid exception return |
| 19 | NOCP | Coprocessor unavailable |
| 24 | UNALIGNED | Trapped unaligned access |
| 25 | DIVBYZERO | Trapped division by zero |

`HFSR.FORCED` reports escalation, not the original cause. For `ABFSR.AXIM=1`,
`AXIMTYPE` values 0/1/2/3 mean OKAY/EXOKAY/SLVERR/DECERR. Without AXIM set,
that field is not interpreted. A validity flag does not override silicon errata.

RCC_RSR reset bits: `CPURSTF[17]`, `D1RSTF[19]`, `D2RSTF[20]`, `BORRSTF[21]`,
`PINRSTF[22]`, `PORRSTF[23]`, `SFTRSTF[24]`, `IWDG1RSTF[26]`,
`WWDG1RSTF[28]`, `LPWRRSTF[30]`. Multiple sticky bits can be set together.
The saved flags describe reset *before* the recorded fault; the current boot's
flags are separately available in `g_lt_fault_status.reset_flags` to firmware.

### ECC and reset limitations that affect interpretation

- ES0392 section 2.2.6: on affected revisions, flash double ECC can invalidate
  the apparent BFAR address. Inspect FLASH ECC_FA1/2 and REV_ID instead. The
  decoder warns conservatively on any captured flash double ECC.
- Section 2.2.7: consecutive flash ECC errors can leave the failing-address
  register stale; a CRC-valid crash record cannot repair stale hardware data.
- Section 2.2.1: watchdog reset restricted to CPU domains can lock the AXI domain.
  Reset policy/option bytes belong to the application; this port does not change them.
- Section 2.2.23: affected WWDG voltage/operating conditions can prevent a proper
  reset/flag. A watchdog-reset bit is not a complete crash diagnosis.
- RAMECC has separate monitors and `ECC_IRQn = 145`: `ECCELEN`, `ECCSEIE`,
  `ECCDEIE`, `ECCDEBWIE`; status `SEDCF/DEDF/DEBWDF`; context `FAR/FDRL/FDRH`.
  Those need an application-owned ECC ISR and region-aware address decoding.
  They are researched extension points, **not implemented** by this fault module.
  Do not scan every monitor in a potentially failing domain from HardFault.
- CFSR/HFSR/DFSR have write-one-to-clear status; ABFSR has different clear
  semantics. Never apply one blanket read-modify-write clear procedure to all.

## Validation and boundaries

ARM GCC 14.3 compilation uses actual CMSIS 5.9.0 and pinned ST H743 headers.
A minimal link probe checks all four vector entries, symbols, sizes and the
absence of unresolved libc/RTOS calls or FP instructions in the capture object.
It links with the hard-float ABI as well as compiling with the default soft ABI.
This is not a full Cube/HAL/RTOS firmware build.

The `-Os` fault capture function uses 168 bytes of compiler-reported stack;
commit and CRC functions report 16 and 20 bytes. The 512-byte reservation also
needs room for possible NMI entry. Recheck call-chain stack use with the actual
compiler/options; these figures are not a proof of worst-case interrupt usage.
Native tests check CRC corruption/partial commit, frame bounds and FP/basic
span checks. Python tests cover CRC, invalid frames, flags and ECC warnings.

This is best-effort diagnostics. A fault before C runtime/FaultInit, exception
vector fetch failure, unreadable logger code/stack, a failed backup domain,
NMI interference or CPU lockup can prevent capture. A valid initial record can
survive a later stack-read fault, but an **already configured** watchdog is
needed to recover from lockup. No handler can promise to record every failure.
No physical H743 fault injection, reset/VBAT-retention test or SWD capture has
been performed. These require validation on the intended board/revision.
The handler executes from the application's normal code placement. To extend
coverage to failures affecting that flash code, code/constants would need a
separately initialized ITCM/RAM placement; this port does not relocate them.

## Primary references

- [ST PM0253](https://www.st.com/resource/en/programming_manual/pm0253-stm32f7-series-and-stm32h7-series-cortexm7-processor-programming-manual-stmicroelectronics.pdf): exception entry, SCB, MPU and M7 auxiliary faults.
- [Arm Cortex-M7 user guide, exception frame](https://documentation-service.arm.com/static/61efd6602dd99944d051417b): core/FP frame layout.
- [ST RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf): RTC backup, RCC, FLASH and RAMECC.
- [ST ES0392](https://www.st.com/resource/en/errata_sheet/es0392-stm32h742x743xig-st32h750xb-and-stm32h753xi-device-errata-stmicroelectronics.pdf): revision-dependent limitations.
- [Pinned ST H743 CMSIS header](https://github.com/STMicroelectronics/cmsis-device-h7/blob/81db1ec63cdc191fae1565b772da3ea5aa29a683/Include/stm32h743xx.h): register addresses and bit definitions, Apache-2.0.
- [Arm CMSIS 5.9 Cortex-M7 header](https://github.com/ARM-software/CMSIS_5/blob/5.9.0/CMSIS/Core/Include/core_cm7.h): core registers/intrinsics, Apache-2.0.

Original integration code is covered by the root [MIT license](../../LICENSE).
Linked manuals remain under their publishers' terms; they are not redistributed.
