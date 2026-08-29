// Audited-TU walk transparency, TU-lean POISON near-miss (lane IV):
// the lean is a TU-level fact -- ONE unauditable statement anywhere in
// the translation unit (here an unrecognized asm template in a
// function the fire never calls) dirties the CC/lane-enable audit, and
// the SAME fire body that forms in the clean-TU twin
// (macro-planner-ambient-walk-tulean-fire-bh.C) keeps the named
// refusal here.  Fail-closed: an unknown asm could deliver a
// lane-enable write the walk cannot see.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-prgm-const -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing \\(ambient-entry-unproven\\)" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk dirty: insn \\d+ bb \\d+ \\(non-.ttinsn assembly in" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formation: entry-ambient all-lanes derived" "rvtt_macro_planner" } }

#define TTI(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

__attribute__((noinline)) void bogwood_poison_unaudited_asm ()
{
  /* Never called by the fire; poisons the TU audit all the same.
     (.word of the scalar NOP encoding: assembles everywhere, and the
     scan has no audited fact for the template.)  */
  __asm__ __volatile__ (".word 0x00000013");
}

__attribute__((noinline)) void reed_tulean_refuse ()
{
  auto x = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (prod, b, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, x, 96, 0, 0, 0, 7);

  __asm__ __volatile__ ("fence");
  TTI (0x01800000);		/* MOP */

#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
