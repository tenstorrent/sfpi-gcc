// WP9 genericity: renamed-equivalent twin of
// macro-planner-select-form-bh.C -- different function and value names,
// identical structure -- must derive the identical descriptor (name
// independence).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000706" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn" 16 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define PICK()                                                                \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto gate = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);            \
      auto lhs = __builtin_rvtt_sfpload (nullptr, 0, 0, 32, 6, 7);            \
      auto rhs = __builtin_rvtt_sfpload (nullptr, 0, 0, 64, 6, 7);            \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (gate, 2);                                    \
      auto chosen = __builtin_rvtt_sfpassign_lv (rhs, lhs);                   \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, chosen, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void choose_lanes ()
{
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
}
