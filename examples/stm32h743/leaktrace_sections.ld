/* SPDX-License-Identifier: MIT
 * Additional GNU ld script, used AFTER the application's main linker script.
 * FIRST reduce that script's 512 KiB RAM_D1/AXI region to 496 KiB, starting at
 * 0x24000000. Otherwise the two linker regions would overlap.
 */
MEMORY
{
  LT_MONITOR (rw) : ORIGIN = 0x2407C000, LENGTH = 16K
}

SECTIONS
{
  .lt_monitor (NOLOAD) :
  {
    . = ALIGN(32);
    __lt_monitor_start__ = .;
    KEEP(*(.lt_monitor.core))
    . = ALIGN(32);
    KEEP(*(.lt_monitor.snapshot))
    . = ALIGN(32);
    __lt_monitor_end__ = .;
  } > LT_MONITOR
}
ASSERT(SIZEOF(.lt_monitor) <= LENGTH(LT_MONITOR), "LeakTrace monitor region overflow")
