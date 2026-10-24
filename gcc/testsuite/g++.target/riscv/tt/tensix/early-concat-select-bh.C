// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_check_early" }
// { dg-final { scan-tree-dump-not "__builtin_rvtt_sfpconcat2" "rvtt_check_early" } }
// { dg-final { scan-tree-dump-not "__builtin_rvtt_sfpselect2" "rvtt_check_early" } }
// { dg-final { scan-assembler "SFPMOV\\tL2, L1, 2" } }

/* Comparison-object plumbing must be gone before the structured CC-mask and
   integer-abs passes inspect operands.  Check the unconditional early pass,
   since the existing immvar canonicalization runs after those consumers.  */
void select_original_operand ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto pair = __builtin_rvtt_sfpconcat2 (a, b);
  auto result = __builtin_rvtt_sfpselect2 (pair, 1);
  __builtin_rvtt_sfpwritelreg (result, 2);
}
