// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// A plain `main' does not close the external entry surface either.
// This public alternate entry has no in-TU caller and performs a
// parameter-relative volatile store that may alias the MOP template.
// Linkage-derived rooting must include it and fail the template audit.
// { dg-final { scan-tree-dump-not "census skips unreachable body void cchm_alternate_entry" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "refused .crosscall-caller-mop-slot-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_TMPL cchm_tmpl
#define CCH_CALLEE cchm_callee
#define CCH_CALLER cchm_caller
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
#define CCH_ENTRY_DEF \
  void cchm_alternate_entry (unsigned *p) { ((volatile unsigned *) p)[0] = 1u; } \
  int main () { cchm_tmpl (4, 2); cchm_caller (4); return 0; }
#include "crosscall-hoist-census-body.h"
