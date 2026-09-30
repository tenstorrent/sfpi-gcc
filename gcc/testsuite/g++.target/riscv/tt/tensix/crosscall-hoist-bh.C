// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// The cross-call prefix hoist: the noinline callee's six call-invariant
// coefficient materializations (pinned to L0-L2/L4-L6 by the SFPLUTFP32
// operand constraints) move to the caller's tile-loop preheader; the
// callee reads the contract registers and re-pins them at every return.
// { dg-final { scan-tree-dump "contract candidate, 6 values, LREG mask 0x77" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-times "contract read" 6 "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-times "placed contract materialization" 6 "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }
// { dg-final { scan-assembler "READ L0" } }
// { dg-final { scan-assembler "READ L6" } }
// { dg-final { scan-assembler "WRITE L0" } }

#define CCH_CALLEE cch_fire_callee
#define CCH_CALLER cch_fire_caller
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
#define CCH_VAL_A0 0x3e4ccccd
#define CCH_VAL_A1 0x3e87ae14
#define CCH_VAL_A2 0x3dbba5e3
#define CCH_VAL_B0 0x3ea3d70a
#define CCH_VAL_B1 0xbd23d70a
#define CCH_VAL_B2 0x3f2e147b
#include "crosscall-hoist-body.h"
