// The trusted effects-declaration channel is RETIRED (2026-08-18
// ruling): __builtin_rvtt_ttregion_begin/end are deprecated no-ops that
// declare nothing, so a raw MOP word refuses through the audited table
// like any other unaudited opcode even when a marker pair brackets it
// and even though a fusion candidate exists.  MOP-expansion effects
// become provable only through the mop_cfg dataflow derivation
// (NOTES-mop-effect-derivation-laneBC.md).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .opaque-region-undeclared.: unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

void formerly_declared_init ()
{
  __builtin_rvtt_ttregion_begin ((1u << 12) | (1u << 13), 0);
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));
  __builtin_rvtt_ttregion_end ();
}

void no_fire_after_marker_retirement ()
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
