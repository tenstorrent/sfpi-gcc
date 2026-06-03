// Verify that the out-of-range diagnostic for a biased SFPU builtin argument
// reports the bias-adjusted user-visible range, not the raw hardware range.
// ttreplay's first integer argument is a 5-bit unsigned field with bias=1,
// giving a valid source range of [1, 32].  Passing 0 must cite [1, 32].
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O1 -fno-exceptions -fno-rtti" }
// { dg-skip-if "Skip LTO tests of builtin compilation" { *-*-* } { "-flto" } }

extern void *sfpi_iptr;

void test_ttreplay_below_bias ()
{
  __builtin_rvtt_ttreplay (sfpi_iptr, 0, 0, 0, 0, 0, 0); // { dg-error "argument 2 .*is out of range \\\[1, 32\\\]" }
}
