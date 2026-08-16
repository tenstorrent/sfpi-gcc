// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// The fresh add/sub-int row shape (load, cast3, load, cast3, iadd
// CC_NONE, cast3, store).  With SFPIADD and the BH mod-3 SFPCAST
// carrying audited effects the rows are discovered as a closed region;
// formation still refuses by name at the schedule/descriptor layers (no
// proven macro program covers this shape), never as an opaque row.
// { dg-final { scan-rtl-dump-not "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=4 row-len=7" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "row-subunits: load,simple,load,simple,simple,simple,store" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-program-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPIADD" } }

__attribute__((noinline)) void add_int_row_shape ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto ai = __builtin_rvtt_sfpcast (a, 3);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
      auto bi = __builtin_rvtt_sfpcast (b, 3);
      auto s = __builtin_rvtt_sfpiadd_v (bi, ai, 4);
      auto sm = __builtin_rvtt_sfpcast (s, 3);
      __builtin_rvtt_sfpstore (nullptr, sm, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
