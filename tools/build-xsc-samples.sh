#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <toolchain-root> <output-dir>" >&2
  exit 1
fi

TC_ROOT="$1"
OUT="$2"
mkdir -p "$OUT"

CC="${TC_ROOT%/}/bin/x86_64-xsc-linux-gnu-gcc"
if [ ! -x "$CC" ]; then
  echo "error: cross-compiler not found at $CC" >&2
  exit 1
fi

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

$CC -O2 -static -o "$OUT/xsc_ring_demo" "$SRC_DIR/samples/xsc_ring_demo.c"
