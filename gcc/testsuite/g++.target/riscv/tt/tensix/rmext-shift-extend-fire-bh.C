// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-optimize-extend -fdump-rtl-rvtt_rmext-details" }
// Check that a left shift of a sign-extended halfword merges with the extension into a shift pair with no sext.h.
// { dg-final { scan-rtl-dump "merging with subsequent extend" "rvtt_rmext" } }
// { dg-final { scan-assembler "srai" } }
// { dg-final { scan-assembler-not "sext\\.h" } }

volatile short *ps;

int shift_after_extend ()
{
  short v = *ps;
  return v << 3;
}
