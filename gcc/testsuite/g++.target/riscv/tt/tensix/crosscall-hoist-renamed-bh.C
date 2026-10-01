// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Renamed twin of crosscall-hoist-bh.C: every identifier differs, the
// decision must not (nothing keys on names).
// { dg-final { scan-tree-dump "contract candidate, 6 values, LREG mask 0x77" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-times "contract read" 6 "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_CALLEE zebra_inner_kernel
#define CCH_CALLER quokka_outer_driver
#define CCH_TILES herd
#define CCH_T beast
#define CCH_ROW stripe
#define CCH_X pelt
#define CCH_R mane
#define CCH_A0 karoo
#define CCH_A1 savanna
#define CCH_A2 veld
#define CCH_B0 acacia
#define CCH_B1 baobab
#define CCH_B2 mopane
#define CCH_VAL_A0 0x3e4ccccd
#define CCH_VAL_A1 0x3e87ae14
#define CCH_VAL_A2 0x3dbba5e3
#define CCH_VAL_B0 0x3ea3d70a
#define CCH_VAL_B1 0xbd23d70a
#define CCH_VAL_B2 0x3f2e147b
#include "crosscall-hoist-body.h"
