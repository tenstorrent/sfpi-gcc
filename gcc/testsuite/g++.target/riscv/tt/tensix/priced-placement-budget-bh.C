// PLACEMENT-ARBITER budget near-miss (the placement arbiter): sixty-five distinct
// loop-class candidates exceed the deterministic arbitration budget
// (RVTT_PLACE_MAX_CANDIDATES = 64), so the arbiter refuses the whole
// class by name (place-budget-exhausted) and the legacy policy chain
// stands byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "residency-rank loop-class over budget .place-budget-exhausted.; the legacy order stands" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant" 3 "rvtt_prgm_const" } }

#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void budget_exhausted (void)
{
  for (unsigned ix = 0; ix != 5; ++ix)
    {
      { auto q0 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000100, 0, 0, 31); ST (q0, 208); }
      { auto q1 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000108, 0, 0, 31); ST (q1, 208); }
      { auto q2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000110, 0, 0, 31); ST (q2, 208); }
      { auto q3 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000118, 0, 0, 31); ST (q3, 208); }
      { auto q4 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000120, 0, 0, 31); ST (q4, 208); }
      { auto q5 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000128, 0, 0, 31); ST (q5, 208); }
      { auto q6 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000130, 0, 0, 31); ST (q6, 208); }
      { auto q7 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000138, 0, 0, 31); ST (q7, 208); }
      { auto q8 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000140, 0, 0, 31); ST (q8, 208); }
      { auto q9 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000148, 0, 0, 31); ST (q9, 208); }
      { auto q10 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000150, 0, 0, 31); ST (q10, 208); }
      { auto q11 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000158, 0, 0, 31); ST (q11, 208); }
      { auto q12 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000160, 0, 0, 31); ST (q12, 208); }
      { auto q13 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000168, 0, 0, 31); ST (q13, 208); }
      { auto q14 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000170, 0, 0, 31); ST (q14, 208); }
      { auto q15 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000178, 0, 0, 31); ST (q15, 208); }
      { auto q16 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000180, 0, 0, 31); ST (q16, 208); }
      { auto q17 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000188, 0, 0, 31); ST (q17, 208); }
      { auto q18 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000190, 0, 0, 31); ST (q18, 208); }
      { auto q19 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000198, 0, 0, 31); ST (q19, 208); }
      { auto q20 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001a0, 0, 0, 31); ST (q20, 208); }
      { auto q21 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001a8, 0, 0, 31); ST (q21, 208); }
      { auto q22 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001b0, 0, 0, 31); ST (q22, 208); }
      { auto q23 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001b8, 0, 0, 31); ST (q23, 208); }
      { auto q24 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001c0, 0, 0, 31); ST (q24, 208); }
      { auto q25 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001c8, 0, 0, 31); ST (q25, 208); }
      { auto q26 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001d0, 0, 0, 31); ST (q26, 208); }
      { auto q27 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001d8, 0, 0, 31); ST (q27, 208); }
      { auto q28 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001e0, 0, 0, 31); ST (q28, 208); }
      { auto q29 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001e8, 0, 0, 31); ST (q29, 208); }
      { auto q30 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001f0, 0, 0, 31); ST (q30, 208); }
      { auto q31 = __builtin_rvtt_sfpxloadi (nullptr, 0x400001f8, 0, 0, 31); ST (q31, 208); }
      { auto q32 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000200, 0, 0, 31); ST (q32, 208); }
      { auto q33 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000208, 0, 0, 31); ST (q33, 208); }
      { auto q34 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000210, 0, 0, 31); ST (q34, 208); }
      { auto q35 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000218, 0, 0, 31); ST (q35, 208); }
      { auto q36 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000220, 0, 0, 31); ST (q36, 208); }
      { auto q37 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000228, 0, 0, 31); ST (q37, 208); }
      { auto q38 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000230, 0, 0, 31); ST (q38, 208); }
      { auto q39 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000238, 0, 0, 31); ST (q39, 208); }
      { auto q40 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000240, 0, 0, 31); ST (q40, 208); }
      { auto q41 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000248, 0, 0, 31); ST (q41, 208); }
      { auto q42 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000250, 0, 0, 31); ST (q42, 208); }
      { auto q43 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000258, 0, 0, 31); ST (q43, 208); }
      { auto q44 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000260, 0, 0, 31); ST (q44, 208); }
      { auto q45 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000268, 0, 0, 31); ST (q45, 208); }
      { auto q46 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000270, 0, 0, 31); ST (q46, 208); }
      { auto q47 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000278, 0, 0, 31); ST (q47, 208); }
      { auto q48 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000280, 0, 0, 31); ST (q48, 208); }
      { auto q49 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000288, 0, 0, 31); ST (q49, 208); }
      { auto q50 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000290, 0, 0, 31); ST (q50, 208); }
      { auto q51 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000298, 0, 0, 31); ST (q51, 208); }
      { auto q52 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002a0, 0, 0, 31); ST (q52, 208); }
      { auto q53 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002a8, 0, 0, 31); ST (q53, 208); }
      { auto q54 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002b0, 0, 0, 31); ST (q54, 208); }
      { auto q55 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002b8, 0, 0, 31); ST (q55, 208); }
      { auto q56 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002c0, 0, 0, 31); ST (q56, 208); }
      { auto q57 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002c8, 0, 0, 31); ST (q57, 208); }
      { auto q58 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002d0, 0, 0, 31); ST (q58, 208); }
      { auto q59 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002d8, 0, 0, 31); ST (q59, 208); }
      { auto q60 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002e0, 0, 0, 31); ST (q60, 208); }
      { auto q61 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002e8, 0, 0, 31); ST (q61, 208); }
      { auto q62 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002f0, 0, 0, 31); ST (q62, 208); }
      { auto q63 = __builtin_rvtt_sfpxloadi (nullptr, 0x400002f8, 0, 0, 31); ST (q63, 208); }
      { auto q64 = __builtin_rvtt_sfpxloadi (nullptr, 0x40000300, 0, 0, 31); ST (q64, 208); }
    }
}
