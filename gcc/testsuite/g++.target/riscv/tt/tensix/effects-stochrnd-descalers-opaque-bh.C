// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Check that both integer-descale SFPSTOCHRND forms, which advance the PRNG, have opaque effects.
// { dg-final { scan-assembler-times {SFPSTOCHRND} 2 } }
// { dg-final { scan-assembler-times {# xtt-effects: opaque} 2 } }

__attribute__((noinline)) void
descale_i_effect ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd_descale_i
    (nullptr, value, 3, 0, 0, 4, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);
}

__attribute__((noinline)) void
descale_v_effect ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto shift = __builtin_rvtt_sfpload (nullptr, 128, 0, 0, 0, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd_descale_v
    (shift, value, 4, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 64, 0, 0, 2, 7);
}
