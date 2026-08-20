// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Predicated live-value merge web: the audited choose insn
// (sfpassign_lv) lanewise selects between two converted producers
// under a CC state computed from a NON-web value; the merge result
// feeds the inverse conversion.  The per-lane argument licenses the
// cancellation under any lane-enable history.
// { dg-final { scan-tree-dump-times "repr-prop: cancelled web .2 sources, 1 chooses, 1 sinks." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_reprprop" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

__attribute__((noinline)) void merge_signed_rows ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  a = __builtin_rvtt_sfpcast (a, 3);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  b = __builtin_rvtt_sfpcast (b, 3);
  auto gate = __builtin_rvtt_sfpload (nullptr, 128, 0, 0, 4, 7);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (gate, 0);
  auto r = __builtin_rvtt_sfpassign_lv (a, b);
  __builtin_rvtt_sfppopc (0);
  r = __builtin_rvtt_sfpcast (r, 3);
  __builtin_rvtt_sfpstore (nullptr, r, 192, 0, 0, 4, 7);
}
