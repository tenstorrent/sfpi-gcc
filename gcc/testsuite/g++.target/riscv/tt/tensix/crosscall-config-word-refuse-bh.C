// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-config-prefix -fdump-tree-rvtt_crosscall" }
// Delivered-word refusal, config face: an SFPCONFIG-class raw word in
// the caller's loop could rewrite the programmed constant register
// between calls; with a config pair riding the contract the word
// refuses outright (without the pair the same word is admitted by the
// historical LREG-only audit).
// { dg-final { scan-tree-dump "refused .crosscall-caller-config-word-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_CALLEE ccx_callee
#define CCH_CALLER ccx_caller
#include "crosscall-config-body.h"
#define CCH_CALLEE_HEAD() CCH_CALLEE_HEAD_CONFIG ()
#define CCH_CALLEE_EXTRA(r) CCH_ROW_CONFIG_READ (r)
#define CCH_LOOP_EXTRA() asm volatile (".ttinsn %0" :: "i" (0x91000000))
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
