// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-peel -mtt-tensix-optimize-post-autoincr-window -fdump-rtl-rvtt_dst_autoincr -fdump-rtl-rvtt_replay_reform-details" }
// Check that a carried Dst-store row loop refuses the record-hoist peel by name and forms in-block instead.
// { dg-final { scan-rtl-dump "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "post-autoincr-window-carried-peel-launch-arithmetic-unproven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "noexec-rerecord-dststore-composition-unaudited" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "post-autoincr-window: carried payload launch arithmetic proven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay_reform" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

using vec_t = __xtt_vector;

void
carried_store_rows_nested (int outer, int inner)
{
  for (int m = 0; m < outer; ++m)
    {
      vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
      acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);
      for (int i = 0; i < inner; ++i)
	{
	  // STORE-terminated carried rows: fold -> stores mode 6.  Two
	  // mads per row cover the SETC16-to-consume distance guard on
	  // the loop-entry path.
#define SROW \
	  { acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0); \
	    acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0); \
	    __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7); \
	    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
	  SROW SROW SROW SROW
	  SROW SROW SROW SROW
	}
    }
}
