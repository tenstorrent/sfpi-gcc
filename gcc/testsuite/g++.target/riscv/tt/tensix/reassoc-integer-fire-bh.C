// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// The PROVEN integer/bitwise class: chains of exact associative
// operators (bitwise XOR/AND here) rebalance WITHOUT the FP license --
// value-identical on every input by associativity of the exact
// operator (no rounding exists) -- and are separately labeled.  The FP
// chain in the same TU still refuses by name (the license separation
// inside one TU).  the -fassociative-math license is deliberately ABSENT.
// { dg-final { scan-tree-dump-times "reassoc: integer rebalance depth 3->2 .sfpxor chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-times "reassoc: integer rebalance depth 3->2 .sfpand chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-times "associative-math-license-absent" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }
#define RA_KERNEL ra_xor_chain
#define RA_N 4
#define RA_OP(a, b) __builtin_rvtt_sfpxor ((a), (b))
#define RA_X0 k0
#define RA_X1 k1
#define RA_X2 k2
#define RA_X3 k3
#define RA_S1 h1
#define RA_S2 h2
#define RA_SL h3
#include "reassoc-body.h"

#undef RA_KERNEL
#undef RA_OP
#undef RA_X0
#undef RA_X1
#undef RA_X2
#undef RA_X3
#undef RA_S1
#undef RA_S2
#undef RA_SL
#define RA_KERNEL ra_and_chain
#define RA_OP(a, b) __builtin_rvtt_sfpand ((a), (b))
#define RA_X0 m0
#define RA_X1 m1
#define RA_X2 m2
#define RA_X3 m3
#define RA_S1 g1
#define RA_S2 g2
#define RA_SL g3
#include "reassoc-body.h"

#undef RA_KERNEL
#undef RA_OP
#undef RA_X0
#undef RA_X1
#undef RA_X2
#undef RA_X3
#undef RA_S1
#undef RA_S2
#undef RA_SL
#define RA_KERNEL ra_fp_refuses_here
#define RA_X0 f0
#define RA_X1 f1
#define RA_X2 f2
#define RA_X3 f3
#define RA_S1 e1
#define RA_S2 e2
#define RA_SL e3
#include "reassoc-body.h"
