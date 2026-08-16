#!/usr/bin/env bash
# P0/D1 cc-enable refusal-identity oracles.
#
# Records, for every ambient-enable refusal test in the cc-enable
# family, the SHA-256 of the generated assembly with the macro planner
# OFF and ON.  Refusals never mutate: the two columns must be equal.
# The hash is over the .s text (not object .text) because the
# deliberately-unprovable enables in these bodies do not assemble on
# every CPU -- same precedent as the invariant-loadi-barrier .s rows in
# manifest-loadmacro-family-20260816.sha256.
#
# Usage:
#   mint-cc-enable-oracles.sh <xg++> <gcc-build-dir> <outdir>

set -euo pipefail
XGXX=$1; GCCDIR=$2; OUT=$3
SRCDIR=$(cd "$(dirname "$0")" && pwd)
TESTDIR=$SRCDIR/..
mkdir -p "$OUT"

manifest=$OUT/cc-enable-refusal-manifest.txt
: > "$manifest"
echo "# cc-enable refusal-identity manifest: test cpu off-s planner-s (must be equal)" >> "$manifest"
echo "# (each column: sha256 over the -S assembly output)" >> "$manifest"
echo "# minted from sfpi-gcc $(git -C "$SRCDIR" rev-parse HEAD 2>/dev/null || echo unknown)" >> "$manifest"

mint () # name cpu
{
  local name=$1 cpu=$2
  local src=$TESTDIR/$name.C
  "$XGXX" -B"$GCCDIR/" -mcpu=tt-$cpu-tensix -O2 -fno-exceptions -fno-rtti \
	  -S "$src" -o "$OUT/$name-$cpu-off.s"
  "$XGXX" -B"$GCCDIR/" -mcpu=tt-$cpu-tensix -O2 -fno-exceptions -fno-rtti \
	  -mtt-tensix-macro-planner -S "$src" -o "$OUT/$name-$cpu-on.s"
  local off on
  off=$(sha256sum "$OUT/$name-$cpu-off.s" | cut -d' ' -f1)
  on=$(sha256sum "$OUT/$name-$cpu-on.s" | cut -d' ' -f1)
  printf '%-40s %-5s %s %s\n' "$name" "$cpu" "$off" "$on" >> "$manifest"
  if [ "$off" != "$on" ]; then
    echo "REFUSAL-IDENTITY VIOLATION: $name $cpu" >&2
    exit 1
  fi
}

mint macro-planner-cc-enable-unproved-bh bh
mint macro-planner-cc-enable-unproved-wh wh
mint macro-planner-cc-enable-varied-bh   bh
mint macro-planner-cc-enable-loop-bh     bh
mint macro-planner-cc-enable-unproved-qsr qsr32

cat "$manifest"
