#!/usr/bin/env python3
"""Resolve a saved return PC with the exact matching ELF; stdlib only."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("pc", nargs="+", help="Return PC(s), e.g. 0x08001235")
    parser.add_argument("--tool", default="arm-none-eabi-addr2line")
    parser.add_argument("--thumb", action="store_true", help="Cortex-M Thumb: resolve PC-2")
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")
    tool = shutil.which(args.tool)
    if tool is None:
        parser.error("ARM addr2line not found; pass --tool with its full path")
    for raw in args.pc:
        pc = int(raw, 0)
        # A Thumb return address refers to the instruction AFTER the call.
        # The last halfword before return is inside both 16- and 32-bit calls.
        lookup = ((pc & ~1) - 2) if args.thumb else pc
        if lookup < 0:
            parser.error(f"Invalid return PC: {raw}")
        result = subprocess.run([tool, "-e", str(args.elf), "-f", "-C", "-i",
                                 hex(lookup)], capture_output=True, text=True)
        if result.returncode:
            sys.exit(result.stderr.strip() or "addr2line failed")
        print(f"return_pc={raw} lookup={hex(lookup)}\n{result.stdout.strip()}\n")


if __name__ == "__main__":
    main()
