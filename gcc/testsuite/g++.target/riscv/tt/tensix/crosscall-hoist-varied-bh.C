// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Varied-constants twin of crosscall-hoist-bh.C: entirely different
// coefficient values, identical decision (nothing keys on values).
// { dg-final { scan-tree-dump "contract candidate, 6 values, LREG mask 0x77" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-times "contract read" 6 "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_CALLEE cch_varied_callee
#define CCH_CALLER cch_varied_caller
#define CCH_TILES tiles
#define CCH_T t
#define CCH_ROW row
#define CCH_X x
#define CCH_R r
#define CCH_A0 a0
#define CCH_A1 a1
#define CCH_A2 a2
#define CCH_B0 b0
#define CCH_B1 b1
#define CCH_B2 b2
#define CCH_VAL_A0 0x40490fdb
#define CCH_VAL_A1 0x402df854
#define CCH_VAL_A2 0x3fb504f3
#define CCH_VAL_B0 0xc0066666
#define CCH_VAL_B1 0x3c23d70a
#define CCH_VAL_B2 0xbf7080e0
#include "crosscall-hoist-body.h"
