import importlib.util
from pathlib import Path
import struct
import unittest
import zlib


def load_tool(name):
    path = Path(__file__).resolve().parents[1] / 'tools' / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


live = load_tool('decode_live')
snapshot = load_tool('decode_snapshot')
fault = load_tool('decode_fault')


class ToolTests(unittest.TestCase):
    def fault_payload(self, **values):
        record = dict.fromkeys(fault.FIELDS, 0)
        record.update(magic=0x4C544631, version=1, flags=(3 << 16) | 1,
                      exc_return=0xFFFFFFFD, psp=0x24000100,
                      xpsr=1 << 24, pc=0x08001234, lr=0x08005679)
        record.update(values)
        data = struct.pack('<31I', *(record[name] for name in fault.FIELDS[:-1]))
        return data + struct.pack('<I', zlib.crc32(data))

    def test_fault_crc_commit_and_offset(self):
        payload = self.fault_payload()
        result = fault.decode_fault(b'prefix' + payload, 6)
        self.assertEqual(result['exception'], 'HardFault')
        self.assertTrue(result['frame_valid'])
        self.assertEqual(result['raw']['pc'], 0x08001234)
        for i in range(128):
            corrupt = bytearray(payload)
            corrupt[i] ^= 1
            with self.assertRaises(ValueError):
                fault.decode_fault(corrupt)

    def test_fault_input_errors(self):
        for data, offset in [(bytes(128), 0), (b'', 0), (bytes(128), -1),
                             (self.fault_payload(version=2), 0)]:
            with self.assertRaises(ValueError):
                fault.decode_fault(data, offset)

    def test_fault_forced_imprecise_and_axim(self):
        result = fault.decode_fault(self.fault_payload(
            cfsr=(1 << 10), hfsr=(1 << 30), abfsr=8 | (3 << 8)))
        self.assertIn('IMPRECISERR', result['cfsr_flags'])
        self.assertIn('FORCED', result['hfsr_flags'])
        self.assertEqual(result['axim_response'], 'DECERR')
        self.assertTrue(any('offending instruction' in note for note in result['notes']))

    def test_fault_stack_errors_reject_pc_and_fp_frame(self):
        result = fault.decode_fault(self.fault_payload(exc_return=0xFFFFFFED))
        self.assertTrue(result['fp_extended'])
        self.assertTrue(result['frame_valid'])
        for bit in (3, 4, 5, 11, 12, 13):
            result = fault.decode_fault(self.fault_payload(cfsr=1 << bit))
            self.assertFalse(result['frame_valid'])
        self.assertFalse(fault.decode_fault(self.fault_payload(flags=(3 << 16) | 2))['frame_valid'])
        self.assertFalse(fault.decode_fault(self.fault_payload(xpsr=0))['frame_valid'])

    def test_fault_address_validity_and_flash_errata(self):
        result = fault.decode_fault(self.fault_payload(
            cfsr=(1 << 7) | (1 << 15), flash_sr2=1 << 26))
        self.assertTrue(result['mmfar_valid'])
        self.assertTrue(result['bfar_valid_flag'])
        self.assertTrue(result['bfar_requires_ecc_review'])
        self.assertTrue(any('ES0392' in note for note in result['notes']))
        result = fault.decode_fault(self.fault_payload())
        self.assertFalse(result['bfar_valid_flag'])
        self.assertFalse(result['mmfar_valid'])

    def test_fault_reset_flags_and_configurable_faults(self):
        for exception, name in [(4, 'MemManage'), (5, 'BusFault'), (6, 'UsageFault')]:
            result = fault.decode_fault(self.fault_payload(
                flags=(exception << 16) | 1, reset_flags=(1 << 26)))
            self.assertEqual(result['exception'], name)
            self.assertEqual(result['boot_reset_flags'], ['IWDG1RSTF'])

    def test_unpacked_large_block_and_full_pc(self):
        data = struct.pack('<IIII', 0xC0001000, 0x90000201, 80000, 33)
        self.assertEqual(live.decode_records(data, 1, 3600),
                         [(0xC0001000, 0x90000201, 80000, 3567)])

    def test_unpacked_host_pointers_and_future_time(self):
        data = struct.pack('<QQII', 0x100001000, 0x100002001, 42, 50)
        self.assertEqual(live.decode_records(data, 1, 40, pointer_bits=64),
                         [(0x100001000, 0x100002001, 42, 0)])

    def test_packed_fields_and_custom_code_base(self):
        size, units, offset = 512, 12, 0x123
        data = struct.pack('<III', 0x24000400,
                           offset | ((size & 255) << 24),
                           (size >> 8) | (units << 8))
        self.assertEqual(live.decode_records(data, 1, 145, 'packed',
                                            text_base=0x90000000),
                         [(0x24000400, 0x90000247, 512, 25)])

    def test_packed_timestamp_rollover(self):
        data = struct.pack('<III', 0x24000400, 0x20FFFFFF,
                           (0xFFFFFF << 8))
        self.assertEqual(live.decode_records(data, 1, 167772170, 'packed'),
                         [(0x24000400, 0, 32, 20)])

    def test_record_input_errors(self):
        with self.assertRaises(ValueError):
            live.decode_records(b'', 1, 0)
        with self.assertRaises(ValueError):
            live.decode_records(b'', -1, 0)
        with self.assertRaises(ValueError):
            live.decode_records(b'', 0, 0, 'packed', pointer_bits=64)

    def test_stats_signed_growth_and_halfword_fields(self):
        values = [0] * 36
        values[0:2] = [0x4C543037, 0x00070000]
        values[27] = 0xFFFFFF80
        values[32:] = [2, 60, 3, 180]
        result = snapshot.decode_stats(b'prefix' + struct.pack('<32I4H', *values), 6)
        self.assertEqual(result['heap_growth_bytes'], -128)
        self.assertEqual(result['fast_count'], 60)
        self.assertEqual(result['slow_count'], 180)

    def test_stats_rejects_wrong_address_or_version(self):
        with self.assertRaises(ValueError):
            snapshot.decode_stats(bytes(136))
        with self.assertRaises(ValueError):
            snapshot.decode_stats(bytes(135))
        values = [0] * 36
        values[0] = 0x4C543037
        values[1] = 0x00080000
        with self.assertRaises(ValueError):
            snapshot.decode_stats(struct.pack('<32I4H', *values))


if __name__ == '__main__':
    unittest.main()
