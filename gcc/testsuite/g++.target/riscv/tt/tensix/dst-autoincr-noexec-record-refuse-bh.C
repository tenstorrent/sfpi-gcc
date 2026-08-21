// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Silicon-refuted composition (lane ES 2x2, rvtt-cost.md "no-exec record
// composition"): a replay capture recorded WITHOUT execution (TTREPLAY
// load=1 exec=0) placed INSIDE the group's loop nest -- the inner loop's
// preheader inside the face loop, the lcm-fresh hang shape -- re-executes
// while the previous face group's final mod-write is still inside its
// unaudited retirement window; on silicon the composition hangs Tensix
// (TENSIX TIMED OUT, reset required) while either transform alone passes
// on the same TU.  The rows are the fat covered-fire shape, so the
// verdict tests the composition, not the shape (see the exec-record and
// preamble-record fire twins).
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .no-exec replay capture within the drained-frontend window of the group.s stores, bb \[0-9\]+, capture bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16\t34" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 1 } }

using vec_t = __xtt_vector;

void
noexec_record_inside_nest ()
{
  for (unsigned face = 0; face != 4; ++face)
    {
      // Re-recorded per face: slots 0..2, never executed from the
      // frontend.  Re-execution follows the previous face's final
      // mod-write store within its retirement window.
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 1);
      auto x = __builtin_rvtt_sfpreadlreg (0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfpwritelreg (x, 0);
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);

      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
	  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
