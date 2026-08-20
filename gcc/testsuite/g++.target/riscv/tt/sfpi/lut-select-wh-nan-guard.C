// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Wormhole fail-closed guard (no -ffinite-math-only): the certified
// enumeration (rvtt-lut-tables.cc) shows every WH formation -- the base
// all-affine tree included -- diverges from the source tree on the
// 8388607 negative-NaN inputs (WH SFPABS keeps the -NaN sign; the WH
// compare-subtract inherits it, so the tree takes range 0 while the
// LUT buckets |x| by magnitude into the tail; first witness
// 0xff800001).  Without the license that excludes exactly those
// inputs, formation refuses by name and edits nothing.  Blackhole is
// untouched (lut-select-bh.C still forms), and lut-select-wh.C proves
// the same shape still forms on WH under -ffinite-math-only.
// { dg-final { scan-tree-dump "refused \\(lut-wh-negative-nan-divergent\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

#define LUT_TREE_FN lut_tree_wh_nan_guard
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_A0 0.1875f
#define LUT_TREE_B0 0.3125f
#define LUT_TREE_A1 0.2651f
#define LUT_TREE_B1 (-0.0442f)
#define LUT_TREE_A2 0.0913f
#define LUT_TREE_B2 0.4477f
#include "lut-select-tree-body.h"
