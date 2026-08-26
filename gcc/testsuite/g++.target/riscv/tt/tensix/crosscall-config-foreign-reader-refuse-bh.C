// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-config-prefix -fdump-tree-rvtt_crosscall" }
// Zero-trip observability refusal, config face: the caller reads the
// pair's PROGRAMMED register (12) through the explicit lreg builtin
// after the loop.  The hoisted pair programs the register on loop
// entry even when the body never runs; refuse by the widened
// foreign-contract check.
// { dg-final { scan-tree-dump "refused .crosscall-caller-foreign-contract." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_CALLEE ccr_callee
#define CCH_CALLER ccr_caller
#include "crosscall-config-body.h"
#define CCH_CALLEE_HEAD() CCH_CALLEE_HEAD_CONFIG ()
#define CCH_CALLEE_EXTRA(r) CCH_ROW_CONFIG_READ (r)
#define CCH_CALLER_TAIL() do { \
    auto ccr_g = __builtin_rvtt_sfpreadlreg (12); \
    __builtin_rvtt_sfpstore (nullptr, ccr_g, 0, 0, 0, 6, 7); \
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
