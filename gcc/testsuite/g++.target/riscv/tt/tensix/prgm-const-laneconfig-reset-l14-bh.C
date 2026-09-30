// The exp kernel's exact init shape, all raw and all UNDECLARED: the
// LaneConfig default-reset (audited by class, claims nothing) next to
// two decoded SFPCONFIG words claiming L12/L13.  The freedom proof
// passes, sees L12/L13 claimed, and the allocator binds L14 -- the
// post-header exp fire shape with no declaration markers at all.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L14 for invariant immediate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

void exp_shape_init ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1)); /* LaneConfig reset */
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000C0)); /* claims L12 */
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000D0)); /* claims L13 */
}

void prgm_reset_fire_binds_l14 ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
