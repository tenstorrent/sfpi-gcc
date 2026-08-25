// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Silicon PASS witness twin (celu/eqz-class ON-set rows, 24 corpus rows,
// silicon-good across many pins): EXPLICIT mod-write rows composed with
// an in-loop no-exec wrapper record that is reachable from the group but
// separated by at least the audited W_drain frontend-word window.  The
// store is itself a frontend word (issue parity), so the frontend-word
// distance bounds its retirement and the audited window admits: the
// group keeps firing.  This is the boundary twin of the launch-row
// refusal (dst-autoincr-launchrow-noexec-reach-refuse-bh.C): identical
// placement, delivery flipped from launches to explicit rows.
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }

using vec_t = __xtt_vector;

volatile unsigned sink;

void
explicit_rows_reachable_noexec_record (unsigned faces)
{
  for (unsigned face = 0; face != faces; ++face)
    {
      // Re-recorded per face (no-exec): the payload carries no Dst
      // access -- the celu-class wrapper record shape.
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 1);
      auto x = __builtin_rvtt_sfpreadlreg (0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfpwritelreg (x, 0);

      if (face & 1)
	sink = face;

      // Fat explicit rows: the mod-write store is an inline frontend
      // word (issue parity holds).
      for (unsigned ix = 0; ix != 11; ++ix)
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

      // Unconditional covering block on every backedge path: at least
      // the audited W_drain window of frontend issue-slot words between
      // the group's stores and the next face's record.
      for (unsigned k = 0; k != 4; ++k)
	sink = sink + k * face;
    }
}
