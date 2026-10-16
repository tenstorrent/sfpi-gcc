// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-fix-qsrreplay" }
// Check that -mtt-fix-qsrreplay diagnoses an exec-while-load replay on Blackhole too.

void record ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 20, 1, 1); // { dg-error "Quasar replay" }
}
