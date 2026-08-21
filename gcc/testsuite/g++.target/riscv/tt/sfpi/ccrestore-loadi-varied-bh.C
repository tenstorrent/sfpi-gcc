// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Renamed-equivalent, varied-constant, varied-direction, varied-trip
// twin of ccrestore-loadi-fire-bh.C: the decisions must be value- and
// name-independent.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "cc-position-widening-unproven" "rvtt_invariant" } }
#define CCR_FN some_other_kernel_row
#define CCR_COND(x) ((x) <= 1.75f)
#define CCR_COEFF 2.30258509f
#define CCR_SPECIAL 41.09375f
#define CCR_TRIPS 16
#include "ccrestore-loadi-body.h"
