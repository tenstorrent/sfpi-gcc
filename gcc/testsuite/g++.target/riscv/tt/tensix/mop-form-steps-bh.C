// The step-carrying group class: a straight-line run of
// [playback launch, typed SETRWC address step] groups re-rolls into one
// TTMOP whose template carries the launch word in A0 and the SETRWC
// word in the flags&2 slots (unused step slots written with the
// FIFO-swallowed NOP) -- the production reduce/halo template shape and
// the minmax post-pipeline delivery form.  Execution-bound rows price
// negative under the corrected delivery model, so the force flag builds
// this leg.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fwhole-program -fkeep-static-functions -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form candidate \\(run\\): 4 x launch \\\[0,\\+3\\) \\+ 1 step word\\(s\\), config 16 words, modeled benefit -1968" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, run\\): 4 iterations of launch \\\[0,\\+3\\) -> TTMOP 0, 3, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP template carries 1 SETRWC step word\\(s\\) in the flags&2 slots" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 3, 0" 1 } }
// { dg-final { scan-assembler-times "TTMOPCFG\\t0" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 1 } }
// { dg-final { scan-assembler-not "TTSETRWC" } }

void grouped_run ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);

  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
}
