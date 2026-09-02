// Varied twin of macro-planner-derived-intmul-row-bh.C: the same
// dataflow shape with DIFFERENT constants everywhere -- radix split 19
// instead of 23, operand rows 32/96, a distinct output row 160 -- must
// form through the same derived machinery with the varied values
// visible in the derived words (imm12 0xfed vs 0xfe9; launch addresses
// 0x20/0x60/0xa0).  Proves the formation keys on dataflow shape and
// architectural facts, never on the production kernel's constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=4 seq=3 misc=0x00000040 setc16=3 launches=3 drain=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x94fed0d6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=2: 0x980009e0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9304e020" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=1 word=0x9354e060" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=2 vd=2 word=0x93a4c0a0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto p = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 4, 7);              \
      p = __builtin_rvtt_sfpcast (p, 3);                                      \
      auto q = __builtin_rvtt_sfpload (nullptr, 96, 0, 0, 4, 7);              \
      q = __builtin_rvtt_sfpcast (q, 3);                                      \
      auto low = __builtin_rvtt_sfpmul24 (p, q, 0);                           \
      auto high = __builtin_rvtt_sfpmul24 (p, q, 1);                          \
      auto sp = __builtin_rvtt_sfpshft_i (nullptr, p, -19, 0, 0, 0);          \
      auto cx = __builtin_rvtt_sfpmul24 (sp, q, 0);                           \
      q = __builtin_rvtt_sfpshft_i (nullptr, q, -19, 0, 0, 0);                \
      high = __builtin_rvtt_sfpiadd_v (high, cx, 4);                          \
      q = __builtin_rvtt_sfpmul24 (p, q, 0);                                  \
      high = __builtin_rvtt_sfpiadd_v (high, q, 4);                           \
      high = __builtin_rvtt_sfpshft_i (nullptr, high, 19, 0, 0, 0);           \
      low = __builtin_rvtt_sfpiadd_v (low, high, 4);                          \
      low = __builtin_rvtt_sfpcast (low, 3);                                  \
      __builtin_rvtt_sfpstore (nullptr, low, 160, 0, 0, 4, 7);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void varied_radix_product_rows ()
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
