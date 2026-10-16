// Check loadi hoisting, replay formation and Dst auto-increment together on a face loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-dst-autoincr -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_dst_autoincr-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 8 exec surplus 3816 >= record delivery 615" "rvtt_replay" } }
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
