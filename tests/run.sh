#!/bin/sh
set -eu
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for f in core portable adapters 48h time_future h743_fault; do
  cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic "test_${f}.c" -o "$tmp/$f"
  "$tmp/$f"
done
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic -DLT_MAX_LIVE=16u -DLT_BUCKETS=8u -DLT_FAST_SLOTS=4u -DLT_SLOW_SLOTS=4u test_stdlib.c ../LeakTrace_Stdlib.c -o "$tmp/stdlib"
"$tmp/stdlib"
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -DLT_MAX_LIVE=32u -DLT_BUCKETS=16u -DLT_FAST_SLOTS=4u -DLT_SLOW_SLOTS=4u test_cpp.cpp ../LeakTrace_Cpp.cpp -o "$tmp/cpp"
"$tmp/cpp"
echo "PASS: all standard tests"
