#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Decode the H743 port's 128-byte RTC backup-register fault record."""
import argparse
import json
import struct
import zlib
from pathlib import Path

FIELDS = (
    'magic version sequence flags reset_flags exc_return msp psp primask control '
    'cfsr hfsr dfsr abfsr mmfar bfar shcsr icsr device_id '
    'flash_sr1 flash_sr2 flash_ecc_fa1 flash_ecc_fa2 '
    'r0 r1 r2 r3 r12 lr pc xpsr crc'
).split()
CFSR = {
    0: 'IACCVIOL', 1: 'DACCVIOL', 3: 'MUNSTKERR', 4: 'MSTKERR',
    5: 'MLSPERR', 7: 'MMARVALID', 8: 'IBUSERR', 9: 'PRECISERR',
    10: 'IMPRECISERR', 11: 'UNSTKERR', 12: 'STKERR', 13: 'LSPERR',
    15: 'BFARVALID', 16: 'UNDEFINSTR', 17: 'INVSTATE', 18: 'INVPC',
    19: 'NOCP', 24: 'UNALIGNED', 25: 'DIVBYZERO',
}
HFSR = {1: 'VECTTBL', 30: 'FORCED', 31: 'DEBUGEVT'}
DFSR = {0: 'HALTED', 1: 'BKPT', 2: 'DWTTRAP', 3: 'VCATCH', 4: 'EXTERNAL'}
ABFSR = {0: 'ITCM', 1: 'DTCM', 2: 'AHBP', 3: 'AXIM', 4: 'EPPB'}
RESET = {17: 'CPURSTF', 19: 'D1RSTF', 20: 'D2RSTF', 21: 'BORRSTF',
         22: 'PINRSTF', 23: 'PORRSTF', 24: 'SFTRSTF', 26: 'IWDG1RSTF',
         28: 'WWDG1RSTF', 30: 'LPWRRSTF'}


def flag_names(value, definitions):
    return [name for bit, name in definitions.items() if value & (1 << bit)]


def decode_fault(data, offset=0):
    if offset < 0 or len(data) - offset < 128:
        raise ValueError('Expected 128 bytes of H743 fault record at the selected offset')
    payload = data[offset:offset + 128]
    raw = dict(zip(FIELDS, struct.unpack('<32I', payload)))
    if raw['magic'] != 0x4C544631:
        raise ValueError('No committed H743 fault record: check address, backup reset or initialization')
    if raw['version'] != 1:
        raise ValueError('Unsupported H743 fault format version')
    if raw['crc'] != zlib.crc32(payload[:124]):
        raise ValueError('Fault CRC mismatch: incomplete, changed or corrupt capture')
    exception = (raw['flags'] >> 16) & 0x1FF
    frame_valid = bool(raw['flags'] & 1) and not bool(raw['flags'] & 2)
    frame_valid = frame_valid and bool(raw['xpsr'] & (1 << 24))
    frame_valid = frame_valid and not bool(raw['cfsr'] & 0x3838)
    frame_valid = frame_valid and raw['exc_return'] in (
        0xFFFFFFF1, 0xFFFFFFF9, 0xFFFFFFFD, 0xFFFFFFE1, 0xFFFFFFE9, 0xFFFFFFED)
    flash_double = bool((raw['flash_sr1'] | raw['flash_sr2']) & (1 << 26))
    notes = []
    if not frame_valid:
        notes.append('Stack frame unavailable/untrusted; do not resolve the stored PC/LR.')
    if raw['cfsr'] & (1 << 10):
        notes.append('IMPRECISERR: stacked PC need not identify the offending instruction.')
    if flash_double:
        notes.append('Flash double ECC: consult ECC_FA1/2 and device revision; ES0392 can make BFAR unreliable.')
    if raw['hfsr'] & (1 << 30):
        notes.append('FORCED is an escalated fault; inspect CFSR for the underlying status.')
    notes.append('Reset flags describe entry into this boot, before this recorded fault.')
    notes.append('Flags may be sticky; this is diagnostic evidence, not proof of a leak or a unique root cause.')
    return {
        'raw': raw,
        'exception': {2: 'NMI', 3: 'HardFault', 4: 'MemManage', 5: 'BusFault',
                      6: 'UsageFault'}.get(exception, f'Exception {exception}'),
        'frame_valid': frame_valid,
        'frame_stack': 'PSP' if raw['exc_return'] & 4 else 'MSP',
        'fp_extended': not bool(raw['exc_return'] & 0x10),
        'cfsr_flags': flag_names(raw['cfsr'], CFSR),
        'hfsr_flags': flag_names(raw['hfsr'], HFSR),
        'dfsr_flags': flag_names(raw['dfsr'], DFSR),
        'abfsr_flags': flag_names(raw['abfsr'], ABFSR),
        'axim_response': ('OKAY', 'EXOKAY', 'SLVERR', 'DECERR')[(raw['abfsr'] >> 8) & 3]
                         if raw['abfsr'] & 8 else None,
        'boot_reset_flags': flag_names(raw['reset_flags'], RESET),
        'mmfar_valid': bool(raw['cfsr'] & (1 << 7)),
        'bfar_valid_flag': bool(raw['cfsr'] & (1 << 15)),
        'bfar_requires_ecc_review': flash_double,
        'notes': notes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path, help='Raw 128-byte RTC_BKP0R..BKP31R export')
    parser.add_argument('--offset', type=lambda value: int(value, 0), default=0)
    parser.add_argument('--json', action='store_true', help='Print structured results')
    args = parser.parse_args()
    try:
        result = decode_fault(args.file.read_bytes(), args.offset)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(result, indent=2))
        return
    print(f"exception: {result['exception']}")
    for name, value in result['raw'].items():
        print(f'{name}: 0x{value:08X}')
    for name, value in result.items():
        if name not in ('raw', 'exception', 'notes'):
            print(f'{name}: {value}')
    for note in result['notes']:
        print('Note:', note)


if __name__ == '__main__':
    main()
