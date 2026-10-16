// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-fix-qsrreplay" }
// Check that -mno-tt-fix-qsrreplay accepts an exec-while-load replay on tt-qsr32.
// { dg-final { scan-assembler "TTREPLAY" } }

void record ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 20, 1, 1);
}
