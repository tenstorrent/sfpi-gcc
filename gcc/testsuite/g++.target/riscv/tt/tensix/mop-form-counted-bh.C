// The counted-loop delivery form at the DEFAULT threshold: a user
// playback loop whose per-trip delivery (launch + two loop-control
// words, 369 centislots) dominates its 3-slot row execution, so the
// corrected model prices 20 x 69 - 9 x 123 = +273 >= 60 and the loop
// re-rolls into one TTMOP with its control removed -- no force flag,
// no threshold override.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form candidate \\(counted loop\\): 20 x launch \\\[0,\\+3\\), config 9 words, modeled benefit 273" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, counted loop\\): 20 iterations of launch \\\[0,\\+3\\) -> TTMOP 0, 19, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "Removed counted-loop control" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 19, 0" 1 } }
// { dg-final { scan-assembler-times "TTMOPCFG\\t0" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 1 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

void user_playback_counted ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}
