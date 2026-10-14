/* SPDX-License-Identifier: MIT
 * STM32H743 only, GCC/Clang. Compile this file without FP, LTO, builtins or
 * stack protectors; see FAULTS.md. No HAL, RTOS, malloc, formatting or BKPT.
 */
#include "stm32h7xx.h"
#include "LeakTrace_H743_Fault.h"
#include <stddef.h>
#if !defined(STM32H743xx)
#error "This fault port is specific to STM32H743xx"
#endif
#if LT_H743_FAULT_STACK_BYTES < 512 || (LT_H743_FAULT_STACK_BYTES % 32) != 0
#error "Reserve at least 512 bytes, in multiples of 32"
#endif
_Static_assert(sizeof(lt_h743_fault_record_t) == 128u, "Fault ABI");
_Static_assert(LTF_CRC == 31, "Fault ABI indices");
_Static_assert(offsetof(RTC_TypeDef, BKP31R) - offsetof(RTC_TypeDef, BKP0R) == 124u,
               "Expected contiguous H743 backup registers");
volatile lt_h743_fault_status_t g_lt_fault_status
    __attribute__((section(".bss.lt_fault_status"), aligned(32)));
uint8_t g_lt_fault_stack[LT_H743_FAULT_STACK_BYTES]
    __attribute__((section(".bss.lt_fault_stack"), aligned(32)));

static volatile uint32_t *backup_words(void)
{
    return (volatile uint32_t *)(RTC_BASE + offsetof(RTC_TypeDef, BKP0R));
}
static uint32_t installed_handlers(void)
{
    const volatile uint32_t *vectors = (const volatile uint32_t *)SCB->VTOR;
    const uint32_t target = (uint32_t)(uintptr_t)LT_H743_FaultEntry | 1u;
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < 4u; ++i)
        if (vectors[3u + i] == target) mask |= 1u << i;
    return mask;
}
int LT_H743_FaultRead(lt_h743_fault_record_t *out)
{
    if (out == NULL || g_lt_fault_status.storage_ready == 0u) return 0;
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    volatile uint32_t *source = backup_words();
    for (uint32_t i = 0u; i < LT_H743_FAULT_WORDS; ++i) out->words[i] = source[i];
    __set_PRIMASK(saved);
    return lt_h743_fault_valid(out);
}
int LT_H743_FaultInit(void)
{
    lt_h743_fault_record_t previous;
    /* Normal .bss initialization must have run before this startup API. */
    g_lt_fault_status.magic = LT_H743_FAULT_MAGIC;
    g_lt_fault_status.reset_flags = RCC->RSR; /* No RMVF write. */
    g_lt_fault_status.device_id = DBGMCU->IDCODE;
    g_lt_fault_status.storage_ready = 0u;
    g_lt_fault_status.record_sequence = 0u;
    g_lt_fault_status.handler_mask = installed_handlers();
    g_lt_fault_status.in_progress = 0u;
    g_lt_fault_status.write_verified = 0u;
    PWR->CR1 |= PWR_CR1_DBP;
    for (uint32_t remaining = 100000u; remaining != 0u; --remaining) {
        if ((PWR->CR1 & PWR_CR1_DBP) != 0u) {
            g_lt_fault_status.storage_ready = 1u;
            if (LT_H743_FaultRead(&previous))
                g_lt_fault_status.record_sequence = previous.words[LTF_SEQUENCE];
            return 1;
        }
    }
    return 0;
}
int LT_H743_FaultEnable(uint32_t optional_traps)
{
    const uint32_t allowed = SCB_CCR_DIV_0_TRP_Msk | SCB_CCR_UNALIGN_TRP_Msk;
    g_lt_fault_status.handler_mask = installed_handlers();
    if (g_lt_fault_status.storage_ready == 0u ||
        g_lt_fault_status.handler_mask != 0x0Fu || (optional_traps & ~allowed) != 0u)
        return 0;
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= optional_traps;
    __DSB(); __ISB();
    return 1;
}
void LT_H743_FaultClear(void)
{
    if (g_lt_fault_status.storage_ready != 0u) {
        backup_words()[LTF_MAGIC] = 0u;
        __DSB();
    }
}
static __attribute__((noreturn)) void finish_fault(void)
{
#if LT_H743_FAULT_AUTO_RESET
    NVIC_SystemReset();
#else
    for (;;) __WFI(); /* A configured watchdog can still reset the device. */
#endif
}
static void commit_record(lt_h743_fault_record_t *record)
{
    record->words[LTF_CRC] = lt_h743_fault_crc(record);
    volatile uint32_t *destination = backup_words();
    destination[LTF_MAGIC] = 0u;
    __DSB();
    for (uint32_t i = 1u; i < LT_H743_FAULT_WORDS; ++i)
        destination[i] = record->words[i];
    __DSB();
    destination[LTF_MAGIC] = LT_H743_FAULT_MAGIC; /* Commit last. */
    __DSB();
    uint32_t equal = 1u;
    for (uint32_t i = 0u; i < LT_H743_FAULT_WORDS; ++i)
        if (destination[i] != record->words[i]) equal = 0u;
    g_lt_fault_status.write_verified = equal;
}
/* Called only by the assembly entry, on the dedicated emergency stack. */
__attribute__((noreturn, used))
void LT_H743_FaultCapture(uint32_t msp, uint32_t psp, uint32_t exc_return,
                          uint32_t primask)
{
    lt_h743_fault_record_t record;
    if (g_lt_fault_status.magic != LT_H743_FAULT_MAGIC ||
        g_lt_fault_status.in_progress != 0u ||
        g_lt_fault_status.storage_ready == 0u || (PWR->CR1 & PWR_CR1_DBP) == 0u)
        finish_fault();
    g_lt_fault_status.in_progress = 1u;
    /* Volatile stores prevent a compiler-generated memset/library dependency. */
    volatile uint32_t *words = record.words;
    for (uint32_t i = 0u; i < LT_H743_FAULT_WORDS; ++i) words[i] = 0u;
    words[LTF_MAGIC] = LT_H743_FAULT_MAGIC;
    words[LTF_VERSION] = LT_H743_FAULT_VERSION;
    words[LTF_SEQUENCE] = g_lt_fault_status.record_sequence + 1u;
    words[LTF_FLAGS] = ((__get_IPSR() & 0x1FFu) << 16u) | LTF_FRAME_REJECTED;
    words[LTF_RESET_FLAGS] = g_lt_fault_status.reset_flags;
    words[LTF_EXC_RETURN] = exc_return;
    words[LTF_MSP] = msp;
    words[LTF_PSP] = psp;
    words[LTF_PRIMASK] = primask;
    words[LTF_CONTROL] = __get_CONTROL();
    words[LTF_CFSR] = SCB->CFSR;
    words[LTF_HFSR] = SCB->HFSR;
    words[LTF_DFSR] = SCB->DFSR;
    words[LTF_ABFSR] = SCB->ABFSR;
    words[LTF_MMFAR] = SCB->MMFAR;
    words[LTF_BFAR] = SCB->BFAR;
    words[LTF_SHCSR] = SCB->SHCSR;
    words[LTF_ICSR] = SCB->ICSR;
    words[LTF_DEVICE_ID] = g_lt_fault_status.device_id;
    words[LTF_FLASH_SR1] = FLASH->SR1;
    words[LTF_FLASH_SR2] = FLASH->SR2;
    words[LTF_FLASH_ECC_FA1] = FLASH->ECC_FA1;
    words[LTF_FLASH_ECC_FA2] = FLASH->ECC_FA2;
    if ((exc_return & 0x10u) == 0u) words[LTF_FLAGS] |= LTF_FP_EXTENDED;
    if ((words[LTF_CFSR] & SCB_CFSR_BFARVALID_Msk) != 0u)
        words[LTF_FLAGS] |= LTF_BFAR_VALID;
    if ((words[LTF_CFSR] & SCB_CFSR_MMARVALID_Msk) != 0u)
        words[LTF_FLAGS] |= LTF_MMFAR_VALID;
    if (((words[LTF_FLASH_SR1] | words[LTF_FLASH_SR2]) & FLASH_SR_DBECCERR) != 0u)
        words[LTF_FLAGS] |= LTF_FLASH_ECC;
    if ((words[LTF_CFSR] & LTF_STACK_ERROR_MASK) != 0u)
        words[LTF_FLAGS] |= LTF_STACK_ERROR;
    if ((words[LTF_CFSR] & SCB_CFSR_IMPRECISERR_Msk) != 0u)
        words[LTF_FLAGS] |= LTF_PC_IMPRECISE;
    /* Preserve a register-only record BEFORE risking a stack-memory read.
     * If that read faults again, an already-running watchdog can reset the
     * resulting lockup, and the first record remains available after reboot.
     */
    commit_record(&record);
    const uint32_t sp = (exc_return & 4u) != 0u ? psp : msp;
    if (lt_h743_fault_frame_candidate(sp, exc_return, words[LTF_CFSR])) {
        const volatile uint32_t *frame = (const volatile uint32_t *)(uintptr_t)sp;
        /* Core registers start at SP in BOTH basic and FP-extended frames.
         * Never read FP registers: doing so is unnecessary for this recorder.
         */
        for (uint32_t i = 0u; i < 8u; ++i) words[LTF_R0 + i] = frame[i];
        if ((words[LTF_XPSR] & (1u << 24u)) != 0u) {
            words[LTF_FLAGS] &= ~LTF_FRAME_REJECTED;
            words[LTF_FLAGS] |= LTF_FRAME_VALID;
            commit_record(&record);
        }
    }
    finish_fault();
}
