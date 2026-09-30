// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Foreign-leaf near miss: one arm of the merge enters the web without
// passing through a source conversion (a raw load), so cancelling the
// remaining conversions would convert the raw arm's bits on exit --
// observable.  The web refuses by name; the sink cast's own seed then
// refuses at the store.  Calendar untouched.
// { dg-final { scan-tree-dump-times "refused .repr-web-leaf-unproven." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-times "refused .repr-web-consumer-not-transparent." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "cancelled web" "rvtt_reprprop" } }
// { dg-final { scan-assembler-times "SFPCAST" 2 } }

__attribute__((noinline)) void mixed_leaf_select (int take_first)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  a = __builtin_rvtt_sfpcast (a, 3);
  auto b = __builtin_rvtt_sfpload (nullptr, 68, 0, 0, 4, 7);
  auto r = take_first ? a : b;
  r = __builtin_rvtt_sfpcast (r, 3);
  __builtin_rvtt_sfpstore (nullptr, r, 132, 0, 0, 4, 7);
}
