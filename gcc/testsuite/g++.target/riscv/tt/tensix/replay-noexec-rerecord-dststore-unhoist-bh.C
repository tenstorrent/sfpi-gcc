// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay" }
// Fail-closed no-exec re-record sweep (lane FJ; rvtt-cost.md "no-exec
// record composition", delivery-boundary paragraph): a hoisted NO-EXEC
// record placed inside an enclosing loop re-ingests its payload every
// outer iteration, and when the payload carries a Dst store the
// re-ingestion follows the previous iteration's launch-delivered stores
// at runtime pacing no static model prices (silicon: sparse_k_filter
// ON-25 wedges at runtime trip 32 with explicit TTINCRWC rows too).
// The witnessed-good exec-while-record conversion cannot fire here (the
// flag is off in this configuration), so the sweep un-hoists: launches
// are replaced by inline payload copies and the record is deleted.
// The row body is the delivery-bound two-chain shape of
// dst-autoincr-loop-bh.C, so the hoist itself is profitable and the
// verdict tests the sweep, not the pricing.
// { dg-final { scan-rtl-dump "Replay refusal: noexec-rerecord-dststore-composition-unaudited .capture bb \[0-9\]+ in loop \[0-9\]+ un-hoisted, \[0-9\]+ launches inlined." "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

using vec_t = __xtt_vector;

void
rerecord_dststore_in_outer_loop ()
{
  for (unsigned face = 0; face != 4; ++face)
    for (unsigned ix = 0; ix != 8; ++ix)
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
