// Mechanism separation on WH: without result scoreboarding the mad
// family's latency window is a REQUIRED-nop site (the DYNAMIC delay
// probe fires), owned by the nop inserter and fill_nop_shadows.  The
// interlock fill must skip it: no move, and the SFPNOP is still
// emitted (the latency-schedule flag is deliberately off here).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-interlock-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Interlock-fill moved" "rvtt_schedule" } }
// { dg-final { scan-assembler "SFPNOP" } }

void wh_required_nop_site_is_skipped ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto q = __builtin_rvtt_sfpmul (p, p, 0);
  auto f = __builtin_rvtt_sfpand (c, c);
  auto g = __builtin_rvtt_sfpor (d, d);
  __builtin_rvtt_sfpwritelreg (q, 0);
  __builtin_rvtt_sfpwritelreg (f, 2);
  __builtin_rvtt_sfpwritelreg (g, 3);
}
