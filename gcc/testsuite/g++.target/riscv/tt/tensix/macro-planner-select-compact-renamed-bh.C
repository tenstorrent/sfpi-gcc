// WP10 genericity: renamed-equivalent twin of the compact 3-slot
// select formation -- every function and macro name differs, the shape
// and constants are the same, and the identical calendar re-derives
// (no name participates in any planner decision).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000770" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9306e000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define PICK_ONE_LANE_SLICE()                                                 \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto gate = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);            \
      auto yes_bits = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);       \
      auto no_bits = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);        \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (gate, 2);                                    \
      auto merged = __builtin_rvtt_sfpassign_lv (no_bits, yes_bits);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, merged, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void pick_lane_slices ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
  PICK_ONE_LANE_SLICE ();
}
