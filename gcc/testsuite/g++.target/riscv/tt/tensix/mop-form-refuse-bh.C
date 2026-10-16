// Check that MOP formation refuses a runtime trip count, a replay-window overflow and an extra side effect in the loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-min-benefit=0 -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-trips-unproved\\): loop bb \\d+ trip count is not provably constant" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-replay-window-overflow\\): launch \\\[20,\\+16\\) exceeds the 32-slot replay buffer \\(S\\+L > 32\\)" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-body-extra-delivery\\)" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

void user_playback_runtime (unsigned n)
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != n; ++i)
    __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
}

void overflow_run ()
{
  __builtin_rvtt_ttreplay (nullptr, 16, 0, 0, 20, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 16, 0, 0, 20, 0, 0);
}

unsigned extra_scalar_side_effect (unsigned seed)
{
  unsigned acc = seed;
  for (unsigned i = 0; i != 12; ++i)
    {
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
      acc = (acc << 1) ^ i;
    }
  return acc;
}
