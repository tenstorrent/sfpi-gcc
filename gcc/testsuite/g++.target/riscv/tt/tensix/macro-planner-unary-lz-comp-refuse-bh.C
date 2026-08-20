// Derived-template Simple-unary admission, fail-closed twin (lane CZ):
// SFPLZ mod 8 (CC_COMP alone) is DOCUMENTED by the ISA functional
// model but refused by the pinned simulator -- a standing doc/sim
// divergence (rvtt-macro-vocab-enum.cc report).  The admitted
// envelope takes the intersection {0, 2, 4}, and the pattern's effect
// audit already excludes mod 8, so the row stays opaque, nothing
// forms, and the explicit SFPLZ survives.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner refusal: row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPLZ" } }

__attribute__((noinline)) void lz_comp_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfplz (a, 8);                                        \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  lz_comp_rows ();
  return 0;
}
