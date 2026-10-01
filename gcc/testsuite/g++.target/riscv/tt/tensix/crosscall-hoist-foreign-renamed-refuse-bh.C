// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Renamed + varied twin of the foreign-contract refusal: different
// names, different coefficient values, the explicit read BEFORE the
// loop, and a different contract register (L6).  The refusal must key
// on the contract mask alone -- identical under renaming, value
// variation, and position outside the loop.
// { dg-final { scan-tree-dump "refused .crosscall-caller-foreign-contract." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_CALLEE wq_tile_op
#define CCH_CALLER wq_tile_walk
#define CCH_TILES wn
#define CCH_T wt
#define CCH_ROW wr
#define CCH_X wx
#define CCH_R wv
#define CCH_A0 m0
#define CCH_A1 m1
#define CCH_A2 m2
#define CCH_B0 k0
#define CCH_B1 k1
#define CCH_B2 k2
#define CCH_VAL_A0 0x3f81c000
#define CCH_VAL_A1 0x3e99999a
#define CCH_VAL_A2 0xbd4ccccd
#define CCH_VAL_B0 0x3f490fdb
#define CCH_VAL_B1 0x40b504f3
#define CCH_VAL_B2 0x3e317218
#define CCH_CALLER_HEAD() do { \
    auto wq_g = __builtin_rvtt_sfpreadlreg (6); \
    __builtin_rvtt_sfpstore (nullptr, wq_g, 0, 0, 0, 6, 7); \
  } while (0)
#include "crosscall-hoist-body.h"
