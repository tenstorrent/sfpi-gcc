// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-repr-prop -fdump-tree-rvtt_reprprop" }
// The mulint32-class near miss (renamed, varied addresses): the
// conversion results are consumed by the radix-23 wrap-product chain
// (MUL24s, shifts, adds) -- none of which lanewise choose bits -- so
// the web is OBSERVABLE and must refuse by name with the calendar
// untouched.  This is the design-intended verdict: sm<->2c conversion
// does not commute with wrap multiplication, so no sound rewrite can
// remove these conversions while the boundary contract is
// sign-magnitude (the raw-contract rewrite is an owner decision,
// mulint32-raw-contract-owner-decision).
// { dg-final { scan-tree-dump-times "refused .repr-web-consumer-not-transparent." 3 "rvtt_reprprop" } }
// { dg-final { scan-tree-dump-not "cancelled web" "rvtt_reprprop" } }
// { dg-final { scan-assembler-times "SFPCAST" 3 } }

__attribute__((noinline)) void wide_product_lane ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto u = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  u = __builtin_rvtt_sfpcast (u, 3);
  auto w = __builtin_rvtt_sfpload (nullptr, 72, 0, 0, 4, 7);
  w = __builtin_rvtt_sfpcast (w, 3);
  auto lo = __builtin_rvtt_sfpmul24 (u, w, 0);
  auto hi = __builtin_rvtt_sfpmul24 (u, w, 1);
  auto su = __builtin_rvtt_sfpshft_i (nullptr, u, -23, 0, 0, 0);
  auto c0 = __builtin_rvtt_sfpmul24 (su, w, 0);
  w = __builtin_rvtt_sfpshft_i (nullptr, w, -23, 0, 0, 0);
  hi = __builtin_rvtt_sfpiadd_v (hi, c0, 4);
  w = __builtin_rvtt_sfpmul24 (u, w, 0);
  hi = __builtin_rvtt_sfpiadd_v (hi, w, 4);
  hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);
  lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);
  lo = __builtin_rvtt_sfpcast (lo, 3);
  __builtin_rvtt_sfpstore (nullptr, lo, 8, 0, 0, 4, 7);
}
