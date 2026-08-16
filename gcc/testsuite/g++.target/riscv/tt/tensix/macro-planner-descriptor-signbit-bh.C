// Unary shift/cast synthesis reproduces the four frozen words: the SHFT2
// template's imm12 packed from the typed shift immediate, the cast
// template field-packed from the admitted source, the proven sequence and
// misc words, with the table-forced macro VD 1 (the SHFT2 immediate form
// aliases its source selector to L1).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=2 seq=1 misc=0x00000110 setc16=3 launches=1 drain=3 planned-lregs=0x3 prefix=all-lanes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x94fe10c6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x900000d0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=4: 0x5384004d" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=8: 0x00000110" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=0 vd=1 word=0x9310c000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "descriptor-refusal" "rvtt_macro_planner" } }

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
