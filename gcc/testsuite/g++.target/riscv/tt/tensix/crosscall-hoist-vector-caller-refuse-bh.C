// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Refusal twin: the caller's tile loop carries its own SFPU vector
// dataflow -- its LREG identity is a register-allocation decision the
// contract cannot see; refuse by name.
// { dg-final { scan-tree-dump "refused .crosscall-caller-stmt-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_CALLEE cch_vec_callee
#define CCH_CALLER cch_vec_caller
#define CCH_LOOP_EXTRA()						\
  do									\
    {									\
      auto cch_vec_tmp = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);		\
      __builtin_rvtt_sfpstore (nullptr, cch_vec_tmp, 32, 0, 0, 6, 7);	\
    }									\
  while (0)
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
