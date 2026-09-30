// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Wave-8 census-rooting repro, production shape: the TU's only entry
// is plain `main', reachable ONLY from outside the TU (crt0).  The
// census must root the externally-visible entry, see the SFPLOADI->L0
// template-slot store, and refuse the MOP-crossing hoist by name.
// The unrooted-census defect hoisted 6 materializations here wrongly.
// { dg-final { scan-tree-dump "refused .crosscall-caller-mop-slot-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x1" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "census skips unreachable body int main" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_TMPL cche_tmpl
#define CCH_CALLEE cche_callee
#define CCH_CALLER cche_caller
#define CCH_OUTER outer
#define CCH_INNER inner
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
#define CCH_SLOT_WORD 0x71012345
#define CCH_ENTRY_DEF int main () { cche_tmpl (4, 2); cche_caller (4); return 0; }
#include "crosscall-hoist-census-body.h"
