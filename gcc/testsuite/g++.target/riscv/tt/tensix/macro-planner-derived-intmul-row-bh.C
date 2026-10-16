// Check that a multi-sub-unit integer multiply row forms a derived calendar from the capability tables.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=12 issues=12 launches=3 explicit=9 launched-events=6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: derived-calendar events=6 staging=none drain=2 kind-mask=0x0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=4 seq=3 misc=0x00000040 setc16=3 launches=3 drain=2" 1 "rvtt_macro_planner" } }
// The shared in-place cast template (both casts, one destination):
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x900000c3" 1 "rvtt_macro_planner" } }
// The SHFT2 immediate realization of the in-place -23 shift:
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x94fe90d6" 1 "rvtt_macro_planner" } }
// The VB-routed SFPMUL24 with the mandated zero-constant VC (L9):
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=2: 0x980009e0" 1 "rvtt_macro_planner" } }
// The store-producer cast reading its VC source by name (L3):
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=3: 0x900003f3" 1 "rvtt_macro_planner" } }
// Macro 2 executes template 3 into VD and the delayed store reads VD: neither
// byte carries the LReg16 route bit.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=6: 0x0b000087" 1 "rvtt_macro_planner" } }
// Pinned launch VDs: the carriers keep their own physical destinations;
// the store-only carrier takes the proven-clobberable temporary L2.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9304e000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=1 word=0x9354e040" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=2 vd=2 word=0x93a4c000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The hosted events vanish into the calendar; the launches issue as
// planner words (8 rows x 3 launches).
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466570240" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2471813184" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2477047808" 8 } }
// { dg-final { scan-assembler-times "SFPMUL24" 3 } }
// { dg-final { scan-assembler-times "SFPIADD" 3 } }
// { dg-final { scan-assembler-times "SFPSHFT\\t" 2 } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      auto sa = __builtin_rvtt_sfpshft_i (nullptr, a, -23, 0, 0, 0);          \
      auto c0 = __builtin_rvtt_sfpmul24 (sa, b, 0);                           \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -23, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, c0, 4);                              \
      b = __builtin_rvtt_sfpmul24 (a, b, 0);                                  \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void wide_int_product_rows ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
