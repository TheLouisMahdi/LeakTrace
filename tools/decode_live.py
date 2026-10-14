#!/usr/bin/env python3
"""Decode LeakTrace v7 live[] records; Python standard library only.

Export from g_lt.live[0] and use live_count/last_poll_seconds from the same
consistent capture. Choose the exact record format compiled into the target.
"""
import argparse
import shutil
import struct
import subprocess
from collections import defaultdict
from pathlib import Path


def decode_records(data, count, now, record_format='unpacked', pointer_bits=32,
                   text_base=0x08000000):
    if count < 0 or not 0 <= now <= 0xFFFFFFFF:
        raise ValueError('count must be nonnegative; now must be uint32 seconds')
    if pointer_bits not in (32, 64):
        raise ValueError('pointer-bits must be 32 or 64')
    if record_format == 'packed':
        if pointer_bits != 32:
            raise ValueError('Packed records require 32-bit pointers')
        layout = struct.Struct('<III')
    elif record_format == 'unpacked':
        layout = struct.Struct('<IIII' if pointer_bits == 32 else '<QQII')
    else:
        raise ValueError('Unknown record format')
    if len(data) < count * layout.size:
        raise ValueError(f'Dump shorter than count * {layout.size} bytes')
    rows = []
    for record in layout.iter_unpack(data[:count * layout.size]):
        if record_format == 'packed':
            address, pc_size, size_time = record
            offset = pc_size & 0xFFFFFF
            size = (pc_size >> 24) | ((size_time & 0xFF) << 8)
            born_unit = (size_time >> 8) & 0xFFFFFF
            elapsed = ((now // 10 - born_unit) & 0xFFFFFF)
            age = 0 if elapsed >= 0x800000 else elapsed * 10 + now % 10
            pc = 0 if offset == 0xFFFFFF else text_base + offset * 2 + 1
        else:
            address, pc, size, born = record
            elapsed = (now - born) & 0xFFFFFFFF
            age = elapsed if elapsed < 0x80000000 else 0
        rows.append((address, pc, size, age))
    return rows


def location(pc, elf, tool):
    if not pc or not elf or not tool or (pc & ~1) < 2:
        return 'unknown'
    result = subprocess.run(
        [tool, '-e', str(elf), '-f', '-C', '-i', hex((pc & ~1) - 2)],
        capture_output=True, text=True, check=False)
    if result.returncode:
        return 'unknown'
    return ' | '.join(result.stdout.strip().splitlines()[:2])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--count', type=int, required=True)
    parser.add_argument('--now', type=lambda v: int(v, 0), required=True)
    parser.add_argument('--format', choices=('packed', 'unpacked'), default='unpacked')
    parser.add_argument('--pointer-bits', type=int, choices=(32, 64), default=32)
    parser.add_argument('--text-base', type=lambda v: int(v, 0), default=0x08000000)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--tool', default='arm-none-eabi-addr2line')
    parser.add_argument('--limit', type=int, default=30)
    parser.add_argument('--all', action='store_true')
    args = parser.parse_args()
    if args.limit < 1:
        parser.error('limit must be at least 1')
    tool = shutil.which(args.tool) if args.elf else None
    if args.elf and (not args.elf.is_file() or tool is None):
        parser.error('Matching ELF and addr2line are required for source resolution')
    try:
        rows = decode_records(args.dump.read_bytes(), args.count, args.now,
                              args.format, args.pointer_bits, args.text_base)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    grouped = defaultdict(lambda: [0, 0, 0])
    for _, pc, size, age in rows:
        group = grouped[pc]
        group[0] += 1
        group[1] += size
        group[2] = max(group[2], age)
    print(f'live={len(rows)} sites={len(grouped)} format={args.format}')
    for pc, (count, total, age) in sorted(
            grouped.items(), key=lambda item: (-item[1][1], -item[1][2]))[:args.limit]:
        print(f'pc=0x{pc:X} blocks={count} bytes={total} oldest_s={age}  '
              f'{location(pc, args.elf, tool)}')
    if args.all:
        print('\nLIVE RECORDS')
        for address, pc, size, age in sorted(rows, key=lambda row: -row[3]):
            print(f'addr=0x{address:X} size={size} age_s={age} pc=0x{pc:X}')


if __name__ == '__main__':
    main()
