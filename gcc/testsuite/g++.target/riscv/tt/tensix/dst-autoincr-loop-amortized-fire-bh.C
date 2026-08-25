// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed counterpart to the straight-callee refusal: the same eight-row
// body executes four times under one proven loop-preheader configuration.
// Its 32 dynamic row steps pay the generic configuration-resource cost, so
// the profitable amortized form must continue to engage.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr refusal: unprofitable group" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 8 } }

using lane_bundle_t = __xtt_vector;

static inline void
advance_bundle ()
{
  lane_bundle_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  lane_bundle_t y = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
amortize_bundle ()
{
  for (unsigned i = 0; i != 4; ++i)
    {
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
      advance_bundle ();
    }
}
