// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Renamed-equivalent, varied-constant twin of reprprop-pair-fire-bh.C
// (different function name, different Dst addresses, different
// address-increment mode field on the store): the cancellation must
// key on the typed conversion insn and its audited constant mod, not
// on any surrounding constant fingerprint.
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
