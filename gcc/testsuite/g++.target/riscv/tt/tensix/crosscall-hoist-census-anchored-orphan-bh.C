// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Anchor discriminator (the production trisc shape): the TU carries
// its own `_start', so the reset vector is the image's ONLY external
// entry.  An orphaned PUBLIC body nothing live calls (the out-of-line
// copy left behind by inlining) is dead, not a hidden entry: the
// census must keep skipping it -- its unresolvable parameter-relative
// volatile store must NOT poison the audit -- and the hoist under the
// off-contract template slot still fires.
// { dg-final { scan-tree-dump "census skips unreachable body void cch_orphan_copy" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x8" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_TMPL ccha_tmpl
#define CCH_CALLEE ccha_callee
#define CCH_CALLER ccha_caller
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
  void cch_orphan_copy (unsigned *p) { ((volatile unsigned *) p)[0] = 1u; } \
  extern "C" void _start () { ccha_tmpl (4, 2); ccha_caller (4); }
#include "crosscall-hoist-census-body.h"
