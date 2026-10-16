// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mno-tt-optimize-extend" }
// Check that with -mno-tt-optimize-extend the separate extension instruction is kept.
// { dg-final { scan-assembler "sext\\.h" } }
// { dg-final { scan-assembler-not "srai" } }

volatile short *ps;

int shift_after_extend ()
{
  short v = *ps;
  return v << 3;
}
