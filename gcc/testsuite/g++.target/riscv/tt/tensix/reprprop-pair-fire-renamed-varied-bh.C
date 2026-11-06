// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Check that the conversion pair cancels with renamed symbols, varied Dst addresses and store increment mode.
// { dg-final { scan-tree-dump-times "repr-prop: cancelled web .1 sources, 0 chooses, 1 sinks." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_reprprop" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

__attribute__((noinline)) void carry_magnitude_lane ()
{
  auto q = __builtin_rvtt_sfpload (nullptr, 34, 0, 0, 4, 7);
  q = __builtin_rvtt_sfpcast (q, 3);
  q = __builtin_rvtt_sfpcast (q, 3);
  __builtin_rvtt_sfpstore (nullptr, q, 98, 0, 0, 4, 7);
}
