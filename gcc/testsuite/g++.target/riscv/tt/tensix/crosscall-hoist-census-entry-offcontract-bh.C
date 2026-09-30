// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Near-miss discriminator for the rooted census: identical plain-main
// TU, but the template slot's SFPLOADI destination is L3 -- NOT a
// contract register.  The rooted census walks the same store, the
// audited destination field keeps the epoch clean, and the hoist
// still fires: rooting must not over-refuse the externally-visible
// entry shape, and the decision keys on the audited field only.
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x8" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_TMPL ccho_tmpl
#define CCH_CALLEE ccho_callee
#define CCH_CALLER ccho_caller
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
#define CCH_SLOT_WORD 0x71312345
#define CCH_ENTRY_DEF int main () { ccho_tmpl (4, 2); ccho_caller (4); return 0; }
#include "crosscall-hoist-census-body.h"
