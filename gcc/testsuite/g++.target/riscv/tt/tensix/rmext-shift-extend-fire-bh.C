// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-optimize-extend -fdump-rtl-rvtt_rmext-details" }
// Redundant-extension removal (-mtt-optimize-extend, default on at -O
// for tt-wh/tt-bh): a left shift of a dying sign-extended halfword
// merges with the extension into shift-left-16 + arithmetic
// shift-right, eliminating the separate extension instruction.
// { dg-final { scan-rtl-dump "merging with subsequent extend" "rvtt_rmext" } }
// { dg-final { scan-assembler "srai" } }
// { dg-final { scan-assembler-not "sext\\.h" } }

volatile short *ps;

int shift_after_extend ()
{
  short v = *ps;
  return v << 3;
}
