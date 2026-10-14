import importlib.util
from pathlib import Path
import struct
import unittest


def load_tool(name):
    path = Path(__file__).resolve().parents[1] / 'tools' / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


live = load_tool('decode_live')
snapshot = load_tool('decode_snapshot')


class ToolTests(unittest.TestCase):
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
