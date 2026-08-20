// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// Unaudited-conversion near miss: SFPCAST mod1=0 (int -> fp32 RNE) is
// a real conversion but has NO audited involution row -- composing
// two of them is not the identity -- so both candidates refuse by
// name and the calendar is untouched.
// { dg-final { scan-tree-dump-times "refused .repr-conversion-unaudited." 2 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "cancelled web" "rvtt_reprprop" } }
// { dg-final { scan-assembler-times "SFPCAST" 2 } }

__attribute__((noinline)) void float_view_roundtrip ()
{
  auto v = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 4, 7);
  v = __builtin_rvtt_sfpcast (v, 0);
  v = __builtin_rvtt_sfpcast (v, 0);
  __builtin_rvtt_sfpstore (nullptr, v, 76, 0, 0, 4, 7);
}
