// Commuted-SFPMUL24 near-miss: the product's destination is NEITHER of
// its factors (both stay live into the accumulates below, so the
// allocator must give the product a fresh register).  Neither the
// established VB:=VD route nor the commuted admission applies — the
// launch VD supplies no factor — so the product stays outside the
// admitted class and the row keeps the established refusal
// byte-identically: no calendar forms and every word issues
// explicitly.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-refusal: descriptor-program-unproven" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPMUL24" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto p = __builtin_rvtt_sfpmul24 (b, a, 0);                             \
      auto t = __builtin_rvtt_sfpshft_i (nullptr, a, 1, 0, 0, 0);             \
      p = __builtin_rvtt_sfpiadd_v (p, t, 4);                                 \
      p = __builtin_rvtt_sfpiadd_v (p, b, 4);                                 \
      p = __builtin_rvtt_sfpcast (p, 3);                                      \
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void fresh_dest_product_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  fresh_dest_product_rows ();
  return 0;
}
