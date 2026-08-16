// Full reduced pipeline on the post-migration shape: the invariant loadi
// hoists out of the row loop and then above the face loop; the row loop is
// completely unrolled by the replay request; replay formation captures one
// row (record-only, itself hoisted as loop-invariant) and launches all
// eight; the Dst auto-increment pass absorbs every per-row TTINCRWC into
// the store's implicit advance and places the owned three-word SETC16
// program once in the face-loop preheader (dominating placement over the
// typed face-advance body).  No scalar row backedge remains.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-dst-autoincr -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }
// { dg-final { scan-assembler-times "SFPLOADI" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }

using vec_t = __xtt_vector;

void face_pipeline ()
{
  for (unsigned face = 0; face != 4; ++face)
    {
      for (unsigned row = 0; row != 8; ++row)
	{
	  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
	  vec_t c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  vec_t t = __builtin_rvtt_sfpmul (a, c, 0);
	  vec_t p = __builtin_rvtt_sfpmul (t, t, 0);
	  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
      __builtin_rvtt_ttdstface ();
    }
}
