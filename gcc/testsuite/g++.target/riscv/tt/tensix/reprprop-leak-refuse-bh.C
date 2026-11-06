// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Check that a converted value that also escapes to a store refuses by name.
// { dg-final { scan-tree-dump-times "refused .repr-web-consumer-not-transparent." 2 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "cancelled web" "rvtt_reprprop" } }
// { dg-final { scan-assembler-times "SFPCAST" 2 } }

__attribute__((noinline)) void escaping_roundtrip ()
{
  auto v = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 4, 7);
  auto t = __builtin_rvtt_sfpcast (v, 3);
  __builtin_rvtt_sfpstore (nullptr, t, 70, 0, 0, 4, 7);
  auto u = __builtin_rvtt_sfpcast (t, 3);
  __builtin_rvtt_sfpstore (nullptr, u, 134, 0, 0, 4, 7);
}
