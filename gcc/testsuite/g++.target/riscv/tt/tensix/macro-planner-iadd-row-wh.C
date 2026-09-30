// { dg-options "-mcpu=tt-wh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// WH twin of the iadd row (no int<->int cast exists on WH; the row is
// load, load, iadd CC_NONE, store).  Audited SFPIADD effects close the
// region; formation refuses by name downstream.
// { dg-final { scan-rtl-dump-not "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=4 row-len=4" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor: templates=1 seq=2 misc=0x00000010 setc16=3 launches=2 drain=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x790001c4" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=1 word=0x9314c000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x93448040" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=4 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

__attribute__((noinline)) void iadd_row_wh ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 3);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 3);
      auto s = __builtin_rvtt_sfpiadd_v (b, a, 4);
      __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 4, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
