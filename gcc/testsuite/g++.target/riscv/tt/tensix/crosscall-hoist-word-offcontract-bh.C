// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Near-miss discriminator for the word audit: the same raw SFPLOADI
// word with destination L3 -- NOT a contract register -- keeps the
// epoch clean, so the hoist still fires.  The decision keys on the
// audited destination field, never on the presence of a raw word.
// { dg-final { scan-tree-dump "hoisted 6 contract materializations from .* into 1 caller" "rvtt_crosscall" } }

#define CCH_CALLEE cch_word3_callee
#define CCH_CALLER cch_word3_caller
#define CCH_LOOP_EXTRA() __asm__ __volatile__ (".ttinsn %0" : : "n" (0x71312345))
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
