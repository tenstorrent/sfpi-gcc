// Full reduced pipeline on the post-migration shape: the invariant loadi
// hoists out of the row loop and then above the face loop; the row loop is
// completely unrolled by the replay request; replay formation captures the
// first row WITH execution in the face-loop body and launches the other
// seven; the Dst auto-increment pass absorbs every per-row TTINCRWC into
// the store's implicit advance and places the owned three-word SETC16
// program once in the face-loop preheader (dominating placement over the
// typed face-advance body).  No scalar row backedge remains.
//
// The record-only preheader hoist REFUSES here by the execution-saturation
// context term (silicon: the unary-max/min +2.06% flip root-cause): with
// the per-row increments absorbed, the eight sibling launches of the same
// buffer are contiguous in the final stream, their execution surplus
// 8 * (400 - 123) = 2216 centislots hides the in-loop record pass's
// (1+4) * 123 = 615-centislot delivery, and the modeled benefit
// degenerates to -615 -- below any non-negative threshold, so even the
// =0 override cannot force the hoist.  The capture therefore stays in the
// face-loop body as a record-with-execution first row (TTREPLAY count 8:
// one recording plus seven launches), the silicon-measured winning form
// for this shape class.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-dst-autoincr -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_dst_autoincr-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 8 x length 4 exec surplus 2216 >= record delivery 615" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }
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
