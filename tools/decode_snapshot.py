#!/usr/bin/env python3
"""Decode a 136-byte LeakTrace v7 lt_stats_t export (g_lt.stats), little-endian."""
import argparse
import struct
from pathlib import Path

FIELDS = (
    'magic version flags alloc_calls free_calls alloc_failures record_drops '
    'duplicates unmatched_frees peak_probe_steps max_live_count live_count '
    'live_traced_bytes peak_live_traced_bytes suspect_count suspect_traced_bytes '
    'oldest_live_seconds top_count poll_count last_poll_seconds heap_total_bytes '
    'heap_free_bytes heap_low_water_free_bytes heap_used_bytes heap_peak_used_bytes '
    'heap_used_percent_x100 heap_baseline_used_bytes heap_growth_bytes '
    'fast_last_bucket slow_last_bucket fast_writes slow_writes '
    'fast_head fast_count slow_head slow_count'
).split()
STATS = struct.Struct('<32I4H')


def decode_stats(data, offset=0):
    if offset < 0 or len(data) - offset < STATS.size:
        raise ValueError('Expected 136 bytes of lt_stats_t at the selected offset')
    result = dict(zip(FIELDS, STATS.unpack_from(data, offset)))
    if result['magic'] != 0x4C543037:
        raise ValueError('Invalid LeakTrace v7 magic; check address/offset and initialization')
    if result['version'] != 0x00070000:
        raise ValueError('Unsupported LeakTrace version; use a matching decoder')
    if result['heap_growth_bytes'] >= 0x80000000:
        result['heap_growth_bytes'] -= 0x100000000
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path, help='Raw binary starting at g_lt.stats')
    parser.add_argument('--offset', type=lambda v: int(v, 0), default=0,
                        help='Byte offset of lt_stats_t within a larger dump')
    args = parser.parse_args()
    try:
        stats = decode_stats(args.file.read_bytes(), args.offset)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for name, value in stats.items():
        if name in ('magic', 'version', 'flags'):
            print(f'{name}: 0x{value:08X}')
        else:
            print(f'{name}: {value}')
    if stats['poll_count'] and stats['heap_total_bytes']:
        print(f"heap_used_percent: {stats['heap_used_percent_x100'] / 100:.2f}%")
    else:
        print('heap_used_percent: no nonzero-heap measurement published')
    print('Heap metrics describe the polled heap, not total physical SRAM.')
    print('This raw stats export has no sequence field; capture consistency is external.')


if __name__ == '__main__':
    main()
