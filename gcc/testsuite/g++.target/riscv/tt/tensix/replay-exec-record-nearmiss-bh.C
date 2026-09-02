// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record -fdump-rtl-rvtt_replay" }
// Position-aware asm boundary (FE-F1 follow-up): a non-empty
// asm in the loop preheader sits BEFORE the hoisted record -- the
// exec-while-record exchange moves the payload's execution from the
// first launch back to the record, so only words BETWEEN the two are
// crossed; a pre-record word is outside the motion window, exactly like
// the typed Tensix words the conversion already admits there.  (The
// LLK per-tile wrapper's raw TTI_STALLWAIT word sits in that position
// on every llk_math_eltwise_sfpu_common.h tile loop, and refusing on it
// left the hardware-refuted no-exec re-record delivery in place on the
// sparse_k_filter shape.)  The record therefore executes trip 1 and the
// first launch is dropped: 15 launches remain.  Words after the record
// keep refusing -- the hoisted record terminates the preheader's Tensix
// content by construction, so the refusing arm is defensive.
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 15 } }
// { dg-final { scan-rtl-dump "Exec-while-record: capture insn \[0-9\]+ executes trip 1" "rvtt_replay" } }
void exec_record_asm_guard ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  __asm__ __volatile__ ("nop");
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
