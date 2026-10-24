// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Check that plain SFPSTOCHRND, which advances the PRNG even in deterministic mode, has opaque effects.
// { dg-final { scan-assembler-times {SFPSTOCHRND} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: opaque} 1 } }

__attribute__((noinline)) void
plain_stochrnd_effect ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd (value, 1, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);
}
