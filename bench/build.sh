#!/usr/bin/env bash
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
g++ -std=c++20 -O2 \
    -I"$REPO/include" \
    -I"$REPO/extern/ViennaTypeListLibrary" \
    -I"$REPO/extern/ViennaStrongType" \
    -o "$REPO/bench/bench.exe" \
    "$REPO/bench/bench.cpp"
echo "Built: $REPO/bench/bench.exe"
