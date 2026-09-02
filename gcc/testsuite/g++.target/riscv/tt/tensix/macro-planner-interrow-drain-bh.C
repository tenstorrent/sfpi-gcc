// P0 wrong-code adjudication: a multi-row region
// formed on the frozen whole-word unary shift/cast program pins the
// launch VD (fixed_vd) -- the conservative VD policy's own rule says
// back-to-back rows are sound only under VD alternation, so emission
// must place the FULL derived drain between consecutive rows.  The
// unfixed emission issued the 8 launches back-to-back with a single
// trailing drain: three launches' hosted events in flight on one LReg,
// device corr FAIL (recurring end-to-end hardware runs) reproduced on the
// pinned simulator.  Fixed shape: 8 launches, each followed by the
// derived drain (8 x 3 SFPNOPs), byte-for-byte the proven rolled
// per-row calendar.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 24 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 11 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);          \
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);\
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);                   \
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);            \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void signbit_rows ()
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
