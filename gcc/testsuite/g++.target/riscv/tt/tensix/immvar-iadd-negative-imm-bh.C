// Check that a two's-complement SFPIADD immediate inside the 12-bit signed field keeps the immediate form.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fdump-tree-rvtt_immvar_expand" }
// { dg-final { scan-tree-dump-times "sfpiadd_i \\(\[^,\]*, \[^,\]*, 4294967295," 1 "rvtt_immvar_expand" } }
// { dg-final { scan-tree-dump-times "sfpiadd_i \\(\[^,\]*, \[^,\]*, 4294965248," 1 "rvtt_immvar_expand" } }
// { dg-final { scan-tree-dump-times "sfpiadd_v" 1 "rvtt_immvar_expand" } }
// { dg-final { scan-assembler "SFPIADD\tL\\d+, L\\d+, -1, 5" } }
// { dg-final { scan-assembler "SFPIADD\tL\\d+, L\\d+, -2048, 5" } }

void decrement (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpiadd_i (nullptr, x, 0xffffffffu, 0, 0, 4);
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void field_floor (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (2);
  y = __builtin_rvtt_sfpiadd_i (nullptr, y, 0xfffff800u, 0, 0, 4);
  __builtin_rvtt_sfpwritelreg (y, 3);
}

void below_field (void)
{
  auto z = __builtin_rvtt_sfpreadlreg (4);
  z = __builtin_rvtt_sfpiadd_i (nullptr, z, 0xfffff7ffu, 0, 0, 4);
  __builtin_rvtt_sfpwritelreg (z, 5);
}
