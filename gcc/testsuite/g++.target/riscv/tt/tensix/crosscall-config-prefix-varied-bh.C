// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-config-prefix -fdump-tree-rvtt_crosscall" }
// The fire under renamed identifiers, a different immediate, and a
// different programmable-constant register (13): nothing may key on
// names or values.
// { dg-final { scan-tree-dump "config pair .creg 13. joins the contract" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "placed config pair .creg 13." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations ..config prefix. from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_CALLEE qq_leaf
#define CCH_CALLER qq_top
#define CCH_CFG_DEST 13
#define CCH_CFG_VAL 15872
#define CCH_CFG_V vv
#define CCH_CFG_R rr
#include "crosscall-config-body.h"
#define CCH_CALLEE_HEAD() CCH_CALLEE_HEAD_CONFIG ()
#define CCH_CALLEE_EXTRA(r) CCH_ROW_CONFIG_READ (r)
#define CCH_TILES nblk
#define CCH_T tt
#define CCH_ROW rw
#define CCH_X xin
#define CCH_R res
#define CCH_A0 c0
#define CCH_A1 c1
#define CCH_A2 c2
#define CCH_B0 d0
#define CCH_B1 d1
#define CCH_B2 d2
#define CCH_VAL_A0 0x3f000000
#define CCH_VAL_A1 0x3e000001
#define CCH_VAL_A2 0x3d800003
#define CCH_VAL_B0 0x3f400000
#define CCH_VAL_B1 0xbe100000
#define CCH_VAL_B2 0x3f100007
#include "crosscall-hoist-body.h"
