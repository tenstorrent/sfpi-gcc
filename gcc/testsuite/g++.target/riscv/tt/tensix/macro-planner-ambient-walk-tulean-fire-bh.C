// Audited-TU walk transparency, TU-lean arm (typecast
// recovery): asm shapes the per-word decode cannot classify -- scalar
// scan-audited templates ("fence") and expander words whose delivered
// content lives in template registers (a MOP run word, the typecast
// math-datacopy anatomy) -- pass the entry-ambient walk exactly when
// the TU-wide CC/lane-enable audit stands
// (rvtt_tu_opaque_cc_ambient_preserving_p): the gimple-time TU scan
// classified every opaque-delivery channel as ambient-preserving.  The
// prgm-const flag plus an in-function residency candidate (the mad
// loop) force the memoized TU scan to run; the TU is clean, so the
// walk sees through both asm statements and the region forms.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-prgm-const -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk: derived through opaque init \\(0 raw words decoded ambient-preserving, 2 audited-TU asm\\)" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "ambient-entry-unproven" "rvtt_macro_planner" } }

#define TTI(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

__attribute__((noinline)) void reed_tulean_fire ()
{
  /* Residency candidate: forces the TU scan in THIS function's own
     prgm-const execution (order-independent).  */
  auto x = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (prod, b, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, x, 96, 0, 0, 0, 7);

  /* The lean-only shapes: a scan-audited scalar template and a MOP run
     word (per-word UNPROVEN -- its content is the TU's audited
     template slots).  */
  __asm__ __volatile__ ("fence");
  TTI (0x01800000);		/* MOP (the typecast bb17 word) */

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
