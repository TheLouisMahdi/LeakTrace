/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../examples/stm32h743/LeakTrace_H743_FaultFormat.h"
int main(void)
{
    lt_h743_fault_record_t record = {{0u}};
    assert(sizeof(record) == 128u);
    assert(!lt_h743_fault_valid(NULL));
    assert(!lt_h743_fault_valid(&record));
    record.words[LTF_MAGIC] = LT_H743_FAULT_MAGIC;
    record.words[LTF_VERSION] = 1u;
    record.words[LTF_CRC] = lt_h743_fault_crc(&record);
    /* Independent Python zlib reference for these exact 124 payload bytes. */
    assert(record.words[LTF_CRC] == 0xF7AE8830u);
    assert(lt_h743_fault_valid(&record));
    /* Every single-bit change, including commit/version/CRC, is rejected. */
    for (uint32_t word = 0u; word < 32u; ++word) {
        for (uint32_t bit = 0u; bit < 32u; ++bit) {
            record.words[word] ^= 1u << bit;
            assert(!lt_h743_fault_valid(&record));
            record.words[word] ^= 1u << bit;
        }
    }
    /* Partial overwrite cannot pass the committed-record check. */
    record.words[LTF_MAGIC] = 0u;
    assert(!lt_h743_fault_valid(&record));
    const uint32_t banks[][2] = {{0x20000000u, 0x20020000u},
        {0x24000000u, 0x24080000u}, {0x30000000u, 0x30048000u},
        {0x38000000u, 0x38010000u}};
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(lt_h743_fault_frame_candidate(banks[i][0], 0xFFFFFFFDu, 0u));
        assert(lt_h743_fault_frame_candidate(banks[i][1] - 32u, 0xFFFFFFFDu, 0u));
        assert(!lt_h743_fault_frame_candidate(banks[i][1] - 24u, 0xFFFFFFFDu, 0u));
        assert(lt_h743_fault_frame_candidate(banks[i][1] - 104u, 0xFFFFFFEDu, 0u));
        assert(!lt_h743_fault_frame_candidate(banks[i][1] - 96u, 0xFFFFFFEDu, 0u));
    }
    assert(!lt_h743_fault_frame_candidate(0xFFFFFFF8u, 0xFFFFFFFDu, 0u));
    assert(!lt_h743_fault_frame_candidate(0u, 0xFFFFFFFDu, 0u));
    assert(!lt_h743_fault_frame_candidate(0x24000004u, 0xFFFFFFFDu, 0u));
    assert(!lt_h743_fault_frame_candidate(0xC0000000u, 0xFFFFFFFDu, 0u));
    assert(!lt_h743_fault_frame_candidate(0x24000000u, 0xFFFFFFF5u, 0u));
    for (uint32_t bit = 0u; bit < 32u; ++bit)
        if (((1u << bit) & LTF_STACK_ERROR_MASK) != 0u)
            assert(!lt_h743_fault_frame_candidate(0x24000000u, 0xFFFFFFFDu, 1u << bit));
    puts("PASS: H743 fault CRC corruption/partial commit and basic/FP frame boundaries");
    return 0;
}
