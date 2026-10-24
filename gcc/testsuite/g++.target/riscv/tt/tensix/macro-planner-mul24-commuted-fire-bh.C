#include "rtl-lane-reset.h"

// Check that an in-place SFPMUL24 tied to its VA-side factor forms through the commuted template word.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: derived-calendar events=5 staging=none drain=2 kind-mask=0x3" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=3 seq=2 misc=0x00000310 setc16=3 launches=2 drain=2 planned-lregs=0x7 prefix=all-lanes" 1 "rvtt_macro_planner" } }
// The commuted product's derived word: SFPMUL24 LOWER, VC pinned to the
// zero constant L9, the surviving factor named through the VA field
// (L1), VB supplied by the launch VD at execution.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x980109d0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The hosted product vanishes into the calendar; the accumulate, one
// slack shift, and one cast remain the row's explicit issues.
// { dg-final { scan-assembler-not "SFPMUL24" } }
// The launches are fixed-VD value carriers (drain=2), so each row is
// followed by its drain; the uniform rows then replay as one
// execute-while-record copy plus seven playbacks.
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPIADD" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\]+, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\]+, 0, 0" 7 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467618816" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2470756416" 8 } }

// Mode-0 casts keep the row in the LReg16 capability class; this test is
// about the commuted SFPMUL24, not mode-3 casts.

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 0);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 0);                                      \
      b = __builtin_rvtt_sfpmul24 (b, a, 0);                                  \
      auto t = __builtin_rvtt_sfpshft_i (nullptr, a, 1, 0, 0, 0);             \
      t = __builtin_rvtt_sfpshft_i (nullptr, t, 2, 0, 0, 0);                  \
      b = __builtin_rvtt_sfpiadd_v (b, t, 4);                                 \
      b = __builtin_rvtt_sfpcast (b, 0);                                      \
      __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void commuted_product_rows ()
{
  RVTT_TEST_LANE_RESET ();
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  RVTT_TEST_LANE_RESET ();
  commuted_product_rows ();
  return 0;
}
