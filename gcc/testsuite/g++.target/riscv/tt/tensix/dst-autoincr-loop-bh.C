// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Counted-loop shape: replay hoisting leaves a launch plus a typed TTINCRWC
// per trip, and the launch-loop unroll then replicates that delivery back
// to back.  The pass proves whole-group ownership over the straight-line
// eight-row run and absorbs every per-row increment into the payload store.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 2, 6" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 17 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

using vec_t = __xtt_vector;

// Two interleaved dependence chains keep the row delivery-bound under
// the interlock-aware reissue pricing (18 words, zero modeled stalls:
// 8 * (18*23 - 70) - 19*123 - 300 = +115).
void
counted_rows ()
{
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);
      vec_t a0 = __builtin_rvtt_sfpmul (a, a, 0);
      vec_t b0 = __builtin_rvtt_sfpmul (b, b, 0);
      vec_t a1 = __builtin_rvtt_sfpmul (a0, a0, 0);
      vec_t b1 = __builtin_rvtt_sfpmul (b0, b0, 0);
      vec_t a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
      vec_t b2 = __builtin_rvtt_sfpmul (b1, b1, 0);
      vec_t a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
      vec_t b3 = __builtin_rvtt_sfpmul (b2, b2, 0);
      vec_t a4 = __builtin_rvtt_sfpmul (a3, a3, 0);
      vec_t b4 = __builtin_rvtt_sfpmul (b3, b3, 0);
      vec_t a5 = __builtin_rvtt_sfpmul (a4, a4, 0);
      vec_t b5 = __builtin_rvtt_sfpmul (b4, b4, 0);
      vec_t a6 = __builtin_rvtt_sfpmul (a5, a5, 0);
      vec_t b6 = __builtin_rvtt_sfpmul (b5, b5, 0);
      __builtin_rvtt_sfpstore (nullptr, a6, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfpstore (nullptr, b6, 0, 0, 0, 2, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
}
