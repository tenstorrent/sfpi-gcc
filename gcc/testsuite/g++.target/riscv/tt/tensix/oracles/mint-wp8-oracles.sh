#!/usr/bin/env bash
# WP8 byte-parity oracle minting (HANDOFF section 6b, steps 2/3).
#
# Records the .text bytes the QUARANTINED exact-calendar pass
# (rtl-rvtt-loadmacro.cc, -mtt-tensix-emit-loadmacro) emits for every
# in-tree signbit shape and for the cast-round oracle body, next to the
# flags-off explicit bytes and the generic macro planner's bytes
# (-mtt-tensix-macro-planner).  The recorded emit-loadmacro hashes are
# the frozen parity expectations that outlive the pass's WP8 deletion;
# wp8-oracle-manifest.txt is the committed snapshot.
#
# Usage:
#   mint-wp8-oracles.sh <xg++> <gcc-build-dir> <binutils-bindir> <outdir>
#
# The quarantined pass must still be in the compiler when re-minting
# column 2 (i.e. run this at or before the WP8 deletion commit).

set -euo pipefail
XGXX=$1; GCCDIR=$2; BINUTILS=$3; OUT=$4
SRCDIR=$(cd "$(dirname "$0")" && pwd)
TESTDIR=$SRCDIR/..
mkdir -p "$OUT"

compile () # src cpu flagset out.o
{
  "$XGXX" -B"$BINUTILS/" -B"$GCCDIR/" -mcpu=tt-$2-tensix -O2 \
	  -fno-exceptions -fno-rtti $3 -c "$1" -o "$4"
}

text_hash () # obj
{
  # SHA-256 over the disassembled instruction words of every code
  # section, in section order.  (objcopy -O binary cannot express
  # multiple code sections at overlapping LMA 0, e.g. the linkonce
  # template sections of the address-boundary shape.)
  "$BINUTILS"/objdump -d "$1" | awk '/^[ \t]*[0-9a-f]+:/ { print $2 }' \
    | sha256sum | cut -d' ' -f1
}

manifest=$OUT/wp8-oracle-manifest.txt
: > "$manifest"
echo "# WP8 oracle manifest: shape cpu off emit-loadmacro planner" >> "$manifest"
echo "# (each column: sha256 over the objdump -d instruction words of all code sections)" >> "$manifest"
echo "# minted from sfpi-gcc $(git -C "$SRCDIR" rev-parse HEAD 2>/dev/null || echo unknown)" >> "$manifest"

mint () # name src cpu
{
  local name=$1 src=$2 cpu=$3
  compile "$src" "$cpu" "" "$OUT/$name-$cpu-off.o"
  local off; off=$(text_hash "$OUT/$name-$cpu-off.o")
  local emit=quarantined-pass-deleted
  if "$XGXX" -B"$BINUTILS/" -B"$GCCDIR/" -mtt-tensix-emit-loadmacro -E -x c++ /dev/null >/dev/null 2>&1; then
    compile "$src" "$cpu" -mtt-tensix-emit-loadmacro "$OUT/$name-$cpu-emit.o"
    emit=$(text_hash "$OUT/$name-$cpu-emit.o")
  fi
  compile "$src" "$cpu" -mtt-tensix-macro-planner "$OUT/$name-$cpu-planner.o"
  local planner; planner=$(text_hash "$OUT/$name-$cpu-planner.o")
  printf '%-28s %-3s %s %s %s\n' "$name" "$cpu" "$off" "$emit" "$planner" >> "$manifest"
}

# Signbit family (bodies preserved here verbatim from the deleted
# quarantined-pass tests; dg directives stripped).
mint staged            "$SRCDIR/staged-bh.C"            bh
mint staged            "$SRCDIR/staged-wh.C"            wh
mint staged-loop       "$SRCDIR/staged-loop-bh.C"       bh
mint staged-loop       "$SRCDIR/staged-loop-wh.C"       wh
mint staged-successor  "$SRCDIR/staged-successor-bh.C"  bh
mint staged-boundary   "$SRCDIR/staged-address-boundary-bh.C" bh
mint staged-fixed-asm  "$SRCDIR/staged-fixed-asm-bh.C"  bh
mint staged-refuse     "$SRCDIR/staged-refuse-bh.C"     bh
mint staged-loop-refuse "$SRCDIR/staged-loop-refuse-bh.C" bh

# Cast-round family (oracle body in this directory; the quarantined
# pass had no in-tree cast-round test, so the body reproduces its
# matcher's proven envelope exactly).
mint cast-round        "$SRCDIR/cast-round-rows.C"                 bh
mint cast-round        "$SRCDIR/cast-round-rows.C"                 wh

cat "$manifest"
