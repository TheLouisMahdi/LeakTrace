#!/usr/bin/env python3
"""Build/run native C and C++ regression tests with GCC-compatible compilers."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    cc, cxx = shutil.which(args.cc), shutil.which(args.cxx)
    if cc is None or cxx is None:
        parser.error('Native C/C++ compilers are required; use --cc and --cxx')
    source = Path(__file__).resolve().parent
    temp_root = Path(tempfile.gettempdir()).resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    with tempfile.TemporaryDirectory(prefix='LeakTrace7_', dir=temp_root) as directory:
        output = Path(directory).resolve()
        if output.parent != temp_root:
            raise RuntimeError('Test output must stay directly inside the temporary directory')
        common = ['-O2', '-Wall', '-Wextra', '-Werror', '-pedantic']

        def test(name, compiler, standard, defines=(), extra=()):
            executable = output / (name + suffix)
            extension = '.cpp' if name == 'cpp' else '.c'
            command = [compiler, standard, *common, *defines,
                       str(source / ('test_' + name + extension)), *extra,
                       '-o', str(executable)]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)
            print('PASS:', name, flush=True)

        for name in ('core', 'portable', 'adapters', '48h', 'time_future', 'h743_fault'):
            test(name, cc, '-std=c11')
        small = ['-DLT_FAST_SLOTS=4u', '-DLT_SLOW_SLOTS=4u']
        test('stdlib', cc, '-std=c11',
             ['-DLT_MAX_LIVE=16u', '-DLT_BUCKETS=8u', *small],
             [str(source.parent / 'LeakTrace_Stdlib.c')])
        test('cpp', cxx, '-std=c++17',
             ['-DLT_MAX_LIVE=32u', '-DLT_BUCKETS=16u', *small],
             [str(source.parent / 'LeakTrace_Cpp.cpp')])
    print('PASS: all native regression tests')


if __name__ == '__main__':
    main()
