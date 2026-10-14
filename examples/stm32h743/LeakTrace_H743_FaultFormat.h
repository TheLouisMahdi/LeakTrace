/* SPDX-License-Identifier: MIT
 * Fixed little-endian 128-byte wire format; no MCU/RTOS dependencies.
 */
#ifndef LEAKTRACE_H743_FAULT_FORMAT_H
#define LEAKTRACE_H743_FAULT_FORMAT_H
#include <stdint.h>
#define LT_H743_FAULT_MAGIC 0x4C544631u
#define LT_H743_FAULT_VERSION 1u
#define LT_H743_FAULT_WORDS 32u
enum {
    LTF_MAGIC, LTF_VERSION, LTF_SEQUENCE, LTF_FLAGS, LTF_RESET_FLAGS,
    LTF_EXC_RETURN, LTF_MSP, LTF_PSP, LTF_PRIMASK, LTF_CONTROL,
    LTF_CFSR, LTF_HFSR, LTF_DFSR, LTF_ABFSR, LTF_MMFAR, LTF_BFAR,
    LTF_SHCSR, LTF_ICSR, LTF_DEVICE_ID, LTF_FLASH_SR1, LTF_FLASH_SR2,
    LTF_FLASH_ECC_FA1, LTF_FLASH_ECC_FA2,
    LTF_R0, LTF_R1, LTF_R2, LTF_R3, LTF_R12, LTF_LR, LTF_PC, LTF_XPSR,
    LTF_CRC
};
#define LTF_FRAME_VALID       0x01u
#define LTF_FRAME_REJECTED    0x02u
#define LTF_FP_EXTENDED       0x04u
#define LTF_BFAR_VALID        0x08u
#define LTF_MMFAR_VALID       0x10u
#define LTF_FLASH_ECC         0x20u
#define LTF_STACK_ERROR       0x40u
#define LTF_PC_IMPRECISE      0x80u
#define LTF_STACK_ERROR_MASK  0x00003838u
typedef struct { uint32_t words[LT_H743_FAULT_WORDS]; } lt_h743_fault_record_t;

/* CRC-32/ISO-HDLC: reflected 0xEDB88320, init/final xor 0xFFFFFFFF. */
static inline uint32_t lt_h743_fault_crc(const lt_h743_fault_record_t *record)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < LTF_CRC; ++i) {
        uint32_t word = record->words[i];
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            crc ^= word & 0xFFu;
            word >>= 8u;
            for (uint32_t bit = 0u; bit < 8u; ++bit)
                crc = (crc >> 1u) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
static inline int lt_h743_fault_valid(const lt_h743_fault_record_t *record)
{
    return record != 0 && record->words[LTF_MAGIC] == LT_H743_FAULT_MAGIC &&
           record->words[LTF_VERSION] == LT_H743_FAULT_VERSION &&
           record->words[LTF_CRC] == lt_h743_fault_crc(record);
}

/* Only the internal SRAM banks below are candidates. This cannot prove that
 * memory is powered, ECC-clean or otherwise readable after hardware damage.
 */
static inline int lt_h743_fault_frame_candidate(uint32_t sp, uint32_t exc_return,
                                                uint32_t cfsr)
{
    const uint32_t ends[][2] = {
        {0x20000000u, 0x20020000u}, /* DTCM */
        {0x24000000u, 0x24080000u}, /* AXI SRAM */
        {0x30000000u, 0x30048000u}, /* SRAM1/2/3 */
        {0x38000000u, 0x38010000u}  /* SRAM4 */
    };
    if (exc_return != 0xFFFFFFF1u && exc_return != 0xFFFFFFF9u &&
        exc_return != 0xFFFFFFFDu && exc_return != 0xFFFFFFE1u &&
        exc_return != 0xFFFFFFE9u && exc_return != 0xFFFFFFEDu) return 0;
    if ((sp & 7u) != 0u || (cfsr & LTF_STACK_ERROR_MASK) != 0u) return 0;
    const uint32_t span = (exc_return & 0x10u) != 0u ? 32u : 104u;
    for (uint32_t i = 0u; i < 4u; ++i)
        if (sp >= ends[i][0] && sp <= ends[i][1] - span) return 1;
    return 0;
}
#endif
