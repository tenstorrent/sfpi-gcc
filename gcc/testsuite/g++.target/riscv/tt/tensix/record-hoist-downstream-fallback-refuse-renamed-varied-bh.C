// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Same as record-hoist-downstream-fallback-refuse-bh.C with different names, window size, trip count and stride.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 4 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
void zeta_face_walk_epochs ()
{
  auto qw = __builtin_rvtt_sfpreadlreg (1);
  auto qx = __builtin_rvtt_sfpreadlreg (2);
  auto qy = __builtin_rvtt_sfpreadlreg (5);
  auto qz = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned epoch = 0; epoch != 3; ++epoch)
    {
      for (unsigned face = 0; face != 13; ++face)
	{
	  qw = __builtin_rvtt_sfpmul (qw, qw, 0);
	  qx = __builtin_rvtt_sfpmul (qx, qx, 0);
	  qy = __builtin_rvtt_sfpmul (qy, qy, 0);
	  qz = __builtin_rvtt_sfpmul (qz, qz, 0);
	  qw = __builtin_rvtt_sfpmul (qw, qx, 0);
	  qx = __builtin_rvtt_sfpmul (qx, qy, 0);
	  qy = __builtin_rvtt_sfpmul (qy, qz, 0);
	  qz = __builtin_rvtt_sfpmul (qz, qw, 0);
	  qw = __builtin_rvtt_sfpmul (qy, qz, 0); /* clone separator */
	  qw = __builtin_rvtt_sfpmul (qw, qw, 0);
	  qx = __builtin_rvtt_sfpmul (qx, qx, 0);
	  qy = __builtin_rvtt_sfpmul (qy, qy, 0);
	  qz = __builtin_rvtt_sfpmul (qz, qz, 0);
	  qw = __builtin_rvtt_sfpmul (qw, qx, 0);
	  qx = __builtin_rvtt_sfpmul (qx, qy, 0);
	  qy = __builtin_rvtt_sfpmul (qy, qz, 0);
	  qz = __builtin_rvtt_sfpmul (qz, qw, 0);
	  __builtin_rvtt_sfpstore (nullptr, qz, 0, 0, 0, 2, 7);
	  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
	}
    }
  __builtin_rvtt_sfpwritelreg (qw, 1);
  __builtin_rvtt_sfpwritelreg (qx, 2);
  __builtin_rvtt_sfpwritelreg (qy, 5);
  __builtin_rvtt_sfpwritelreg (qz, 6);
}
