// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-fix-qsrreplay" }
// Flag-off twin of the Quasar replay erratum check: -mno-tt-fix-qsrreplay
// overrides the tt-qsr32 default and the exec-while-load replay
// compiles without diagnostic.
// { dg-final { scan-assembler "TTREPLAY" } }

void record ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 20, 1, 1);
}
