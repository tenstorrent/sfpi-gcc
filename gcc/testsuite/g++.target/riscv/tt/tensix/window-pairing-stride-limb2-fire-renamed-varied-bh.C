// Lane GJ window-pairing stride-phase generalization, renamed-varied
// twin: the limb-2 shape with different symbol names, a different
// second-operand offset (96), and six copies instead of eight.  The
// verdict is structural (first-word absorber admitted under the flag,
// exact model prices one NOP, fixed-VD WAR hazard names the bound) --
// no operation identity, coefficient value, or row count participates.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -mtt-tensix-optimize-window-pairing-stride -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 2 -> 1 rows=6 bound=window-pairing-lreg-overlap" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 4, 0, 0" 5 } }

#define KROW()                                                                \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto opa = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);             \
      opa = __builtin_rvtt_sfpcast (opa, 3);                                  \
      auto opb = __builtin_rvtt_sfpload (nullptr, 96, 0, 0, 4, 7);            \
      opb = __builtin_rvtt_sfpcast (opb, 3);                                  \
      auto uppr = __builtin_rvtt_sfpmul24 (opa, opb, 1);                      \
      auto lowr = __builtin_rvtt_sfpmul24 (opa, opb, 0);                      \
      uppr = __builtin_rvtt_sfpshft_i (nullptr, uppr, 23, 0, 0, 0);           \
      lowr = __builtin_rvtt_sfpiadd_v (lowr, uppr, 4);                        \
      lowr = __builtin_rvtt_sfpcast (lowr, 3);                                \
      __builtin_rvtt_sfpstore (nullptr, lowr, 0, 0, 0, 4, 7);                 \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void limb2_rows_rv ()
{
  KROW (); KROW (); KROW (); KROW (); KROW (); KROW ();
}
