#!/bin/sh
# Regenerates src/wood.cpp, the walnut_grain face's wood, from tools/wood.cpp (needs a C++ compiler on this computer).
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
c++ -O2 -o "$T/wood" tools/wood.cpp
"$T/wood" > src/wood.cpp
echo "src/wood.cpp: $(grep -c 0x src/wood.cpp) lines"
