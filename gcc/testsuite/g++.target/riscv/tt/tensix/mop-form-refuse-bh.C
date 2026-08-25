// MOP formation near misses, all leaving the delivery byte-identical
// (the force flag is on, so every refusal here is structural, never a
// pricing shadow):
// - a playback loop with a runtime trip count refuses on the trip proof
//   (no estimate ever substitutes: mop-trips-unproved);
// - a contiguous run whose launch range exceeds the 32-slot replay
//   buffer refuses the S+L > 32 co-ownership near miss rather than emit
//   a template word the hardware model rejects as undefined behavior
//   (mop-replay-window-overflow);
// - a counted playback loop carrying a second scalar side effect beyond
//   the counter step refuses on the body shape (mop-body-extra-delivery).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
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
