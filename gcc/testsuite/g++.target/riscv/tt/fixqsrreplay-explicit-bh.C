// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-fix-qsrreplay" }
// The Quasar replay erratum check follows the -mtt-fix-qsrreplay flag,
// not the CPU: naming the flag explicitly diagnoses exec-while-load
// replay even on a Blackhole compile (replay-43496-qsr.C covers the
// tt-qsr32 default-on path).

void record ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 20, 1, 1); // { dg-error "Quasar replay" }
}
