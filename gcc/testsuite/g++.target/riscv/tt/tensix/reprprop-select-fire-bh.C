// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Check that two converted values merged at a scalar-guarded select and then reconverted cancel.
// { dg-final { scan-tree-dump-times "repr-prop: cancelled web .2 sources, 1 chooses, 1 sinks." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_reprprop" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

__attribute__((noinline)) void select_signed_rows (int take_first)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  a = __builtin_rvtt_sfpcast (a, 3);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  b = __builtin_rvtt_sfpcast (b, 3);
  auto r = take_first ? a : b;
  r = __builtin_rvtt_sfpcast (r, 3);
  __builtin_rvtt_sfpstore (nullptr, r, 128, 0, 0, 4, 7);
}
