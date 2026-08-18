// Near miss for the PRESSURE class: capacity is not exceeded, so the
// pressure class must idle and no PRGM register is touched (the
// out-of-loop constants are materialized once either way -- parking
// them would change bytes for nothing).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-residency: pressure \\d+ within the 8-LREG file; pressure class idle" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void residency_within_idle (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
