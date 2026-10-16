// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Check that immediates inside and outside a balanced v_if region hoist out of the loop on Wormhole.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 3 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "cc-position-widening-unproven" "rvtt_invariant" } }
#define CCR_FN ccrestore_fire_wh
#define CCR_COND(x) ((x) == 0.0f)
#define CCR_COEFF 0.6931471805f
#define CCR_SPECIAL -88.72284f
#include "ccrestore-loadi-body.h"
