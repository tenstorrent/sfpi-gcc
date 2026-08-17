// WP10, the real TTNN Where shape: the eight-row select body inside a
// counted loop with no typed ambient enable anywhere.  The
// configuration prefix hoists to the proven structural preheader with
// the MATERIALIZED all-lanes enable at its head (WP9 peeled the first
// row instead; the materialized proof source is that row's own typed
// restore, licensed by rvtt_cc's outermost-CC-depth contract), all
// eight body rows form, and the region-scoped configuration-ownership
// proof tolerates the opaque pre-region init the function-global gate
// would refuse.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=8 row-len=7 runs=1 stride=2 loop=yes" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn" 16 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, 2);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void select_faces (unsigned faces)
{
  unsigned face = 0;
  do
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
  while (++face < faces);
}
