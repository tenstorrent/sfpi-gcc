// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// The plain form advances hidden PRNG state even in deterministic mode.
// Scheduling must see an opaque effect until entropy ordering is modeled.
// { dg-final { scan-assembler-times {SFPSTOCHRND} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: opaque} 1 } }

__attribute__((noinline)) void
plain_stochrnd_effect ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd (value, 1, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);
}
