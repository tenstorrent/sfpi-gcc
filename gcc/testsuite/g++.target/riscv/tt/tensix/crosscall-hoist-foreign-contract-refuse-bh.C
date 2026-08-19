// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Zero-trip observability refusal: the caller reads a CONTRACT
// register (L0) through the explicit lreg builtin AFTER the tile loop.
// The hoisted loads execute on loop entry even when the body never
// runs, so on a zero-trip path this read would observe the hoisted
// clobber where the original left the register untouched.  Refuse by
// name; the per-call prefix stays.
// { dg-final { scan-tree-dump "refused .crosscall-caller-foreign-contract." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_CALLEE cchf_callee
#define CCH_CALLER cchf_caller
#define CCH_CALLER_TAIL() do { \
    auto cchf_g = __builtin_rvtt_sfpreadlreg (0); \
    __builtin_rvtt_sfpstore (nullptr, cchf_g, 0, 0, 0, 6, 7); \
  } while (0)
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
