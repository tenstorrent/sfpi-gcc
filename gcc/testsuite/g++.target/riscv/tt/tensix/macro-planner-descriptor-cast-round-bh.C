// Unary cast/round synthesis: both templates field-packed from admitted
// sources (positional routing selectors 0xC/0xD), the fully
// delay-documented sequence word and misc word reproduced.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x900000c0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x8e0000d1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=4: 0x534d0004" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=8: 0x00000100" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "descriptor-refusal" "rvtt_macro_planner" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);          \
      auto cast = __builtin_rvtt_sfpcast (loaded, 0);                         \
      auto rounded                                                            \
	= __builtin_rvtt_sfpstochrnd_i (nullptr, cast, 0, 0, 0, 1, 0);        \
      __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);              \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void cast_round_rows ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
