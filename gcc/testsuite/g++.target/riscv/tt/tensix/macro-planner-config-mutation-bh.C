// Config-mutation refusal (WP8): a typed write to a planner-owned
// configuration destination between two rows refuses both the mutating
// row (cc/config effect inside the slice) and, function-globally, the
// clean rows' formation.  Bytes stay identical to flags-off.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "row-config-write" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "config-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// (Replay formation may compress the identical explicit rows.)
// { dg-final { scan-assembler "SFPSHFT" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,              \
					    no_increment);                    \
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31,          \
					       0, 0, 0);                      \
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);                   \
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,                \
			       no_increment);                                 \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void config_mutation_between_rows ()
{
  ROW ();
  auto knob = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, no_increment);
  __builtin_rvtt_sfpwriteconfig_v (knob, 4);
  ROW ();
}
