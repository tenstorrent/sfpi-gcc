// Renamed-equivalent copy of the minmax shape: identical semantics under
// unrelated names must produce the identical region dump.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=8 row-len=4 runs=1 stride=2 loop=no" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner row-subunits: load,load,simple,store" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner refusal" "rvtt_macro_planner" } }

#define PIPELINE_STAGE()                                                      \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto west_operand = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);    \
      auto east_operand = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);   \
      auto ordered = __builtin_rvtt_sfpswap (west_operand, east_operand, 1);  \
      auto keep = __builtin_rvtt_sfpselect2 (ordered, 0);                     \
      __builtin_rvtt_sfpstore (nullptr, keep, 128, 0, 0, 0, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void completely_unrelated_stage_name ()
{
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
  PIPELINE_STAGE ();
}

#undef PIPELINE_STAGE
