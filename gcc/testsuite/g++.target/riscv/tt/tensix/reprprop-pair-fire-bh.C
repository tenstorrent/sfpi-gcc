// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Direct composition of the audited BH involution (SFPCAST mod1=3,
// two's-complement <-> sign-magnitude): a load-side conversion whose
// only consumer is the store-side inverse.  The pair cancels
// bit-exactly (the smag-bh.C "FIXME: Doesn't convert." shape at
// builtin level).
// { dg-final { scan-tree-dump-times "repr-prop: cancelled web .1 sources, 0 chooses, 1 sinks." 1 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_reprprop" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\t" 1 } }

__attribute__((noinline)) void roundtrip_row ()
{
  auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  v = __builtin_rvtt_sfpcast (v, 3);
  v = __builtin_rvtt_sfpcast (v, 3);
  __builtin_rvtt_sfpstore (nullptr, v, 2, 0, 0, 4, 7);
}
