// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mno-tt-optimize-extend" }
// Flag-off twin of rmext-shift-extend-fire-bh.C: with the pass
// disabled the separate extension instruction survives.
// { dg-final { scan-assembler "sext\\.h" } }
// { dg-final { scan-assembler-not "srai" } }

volatile short *ps;

int shift_after_extend ()
{
  short v = *ps;
  return v << 3;
}
