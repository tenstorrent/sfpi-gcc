// A MAD-pair refusal must remain authoritative for HOISTED-REUSE.
// `half' is a shortened, fold-vulnerable materialization shared by the
// pair and another consumer.  MAD-pair therefore refuses it.  Reclaiming
// it later through the broad hoisted-reuse class would remove SFPADDI and
// expose MUL+ADD to SFPMAD, changing floating-point rounding.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "madpair loop bb \\d+ refused .madpair-shared-constant" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "hoisted-reuse loop bb \\d+ candidate: out-of-loop constant 0x42fe0000" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM L1\\d for constant 0x42fe0000 .hoisted-reuse class" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }
// { dg-final { scan-assembler "SFPADDI" } }

void hoisted_reuse_madpair_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto side = __builtin_rvtt_sfpreadlreg (1);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto half = __builtin_rvtt_sfploadi (nullptr, 0x42fe, 0, 0, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (prod, half, 0);
      side = __builtin_rvtt_sfpadd (side, half, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (side, 1);
}
