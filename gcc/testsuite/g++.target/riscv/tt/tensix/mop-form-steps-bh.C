// Check that a run of [playback launch, SETRWC step] groups is a MOP candidate that the cost model refuses (mop-profitability).
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-mop-form -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form candidate \\(run\\): 4 x launch \\\[0,\\+3\\) \\+ 1 step word\\(s\\), config 16 words, modeled benefit -1968" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-profitability\\): modeled benefit -1968 below threshold" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP" } }

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
