// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Structured-CC-restore fire (EC-F1): the row loop's v_if region is a
// balanced plain-PUSHC/plain-POPC region whose only CC modifiers are
// the audited narrowing compare class, so both the depth-0 coefficient
// (a two-issue pair) and the in-region special-value load hoist to the
// preheader.  The in-region admission is the containment refinement:
// the position's enable set is a subset of the preheader's, and the
// extra lanes belong to the load's own fresh definition.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "cc-position-widening-unproven" "rvtt_invariant" } }
#define CCR_FN ccrestore_fire
#define CCR_COND(x) ((x) == 0.0f)
#define CCR_COEFF 0.6931471805f
#define CCR_SPECIAL -88.72284f
#include "ccrestore-loadi-body.h"
