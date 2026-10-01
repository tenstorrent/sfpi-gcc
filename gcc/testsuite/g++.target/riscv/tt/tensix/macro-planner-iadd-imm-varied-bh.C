// Derived-template SFPIADD-immediate admission, varied surface
//: a different immediate (+2047, the top of the signed
// 12-bit range), different names, and composition with the
// established in-place immediate-shift class -- the row hosts the
// new iadd-imm word on the Simple sub-unit AND the frozen SHFT2 pair
// on Round in one signbit-style three-event calendar.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x797ff0c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x94ffd0d6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }

#define TILE(off)                                                             \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, (off));           \
      v = __builtin_rvtt_sfpiadd_i (nullptr, v, 2047, 0, 0, 4);               \
      v = __builtin_rvtt_sfpshft_i (nullptr, v, -3, 0, 0, 0);                 \
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 4, (off));                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void saturating_bias_rows ()
{
  TILE (7); TILE (7); TILE (7); TILE (7);
  TILE (7); TILE (7); TILE (7); TILE (7);
}
#undef TILE

int main ()
{
  saturating_bias_rows ();
  return 0;
}
