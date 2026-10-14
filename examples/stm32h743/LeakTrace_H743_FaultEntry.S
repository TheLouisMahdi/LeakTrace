/* SPDX-License-Identifier: MIT
 * Capture original SPs/LR before any C prologue; no push on the damaged stack.
 */
#include "LeakTrace_H743_FaultConfig.h"
.syntax unified
.cpu cortex-m7
.thumb
.section .text.LT_H743_FaultEntry,"ax",%progbits
.balign 4
.global LT_H743_FaultEntry
.type LT_H743_FaultEntry,%function
.thumb_func
LT_H743_FaultEntry:
    mrs r0, msp
    mrs r1, psp
    mov r2, lr
    mrs r3, primask
    cpsid i
    ldr r12, =g_lt_fault_stack + LT_H743_FAULT_STACK_BYTES
    msr msp, r12
    isb
    b LT_H743_FaultCapture
.size LT_H743_FaultEntry, .-LT_H743_FaultEntry

#if LT_H743_FAULT_OWN_HANDLERS
.global HardFault_Handler
.thumb_set HardFault_Handler, LT_H743_FaultEntry
.global MemManage_Handler
.thumb_set MemManage_Handler, LT_H743_FaultEntry
.global BusFault_Handler
.thumb_set BusFault_Handler, LT_H743_FaultEntry
.global UsageFault_Handler
.thumb_set UsageFault_Handler, LT_H743_FaultEntry
#endif
.section .note.GNU-stack,"",%progbits
