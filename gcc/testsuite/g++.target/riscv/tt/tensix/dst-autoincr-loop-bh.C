// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Counted-loop shape: replay hoisting leaves a launch plus a typed TTINCRWC
// in the loop body.  The pass proves whole-body ownership, places the owned
// configuration in the dedicated preheader, and absorbs the per-iteration
// increment into the payload store.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }

using vec_t = __xtt_vector;

void
counted_rows ()
{
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p0 = __builtin_rvtt_sfpmul (a, a, 0);
      vec_t p1 = __builtin_rvtt_sfpmul (p0, p0, 0);
      __builtin_rvtt_sfpstore (nullptr, p1, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
