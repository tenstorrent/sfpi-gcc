// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Genericity twin of record-hoist-fire-bh.C: different LRegs, different
// operand roles, different trip count and counter direction, different
// separator value (symbol-addressed volatile store: a named object is
// provably not the instruction FIFO).  The
// mechanism keys on structure only, never on a register calendar or
// constant fingerprint.
// 6 trips x (738 - 70) - 1161 = 2847.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 6, words 6, benefit 2847\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
static volatile int renamed_sink;
void rerecord_fire_renamed (volatile int *)
{
  auto p = __builtin_rvtt_sfpreadlreg (4);
  auto q = __builtin_rvtt_sfpreadlreg (5);
  auto r = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned k = 6; k != 0; --k)
    {
      p = __builtin_rvtt_sfpmul (p, q, 0);
      q = __builtin_rvtt_sfpmul (q, r, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      p = __builtin_rvtt_sfpmul (p, p, 0);
      r = __builtin_rvtt_sfpmul (r, p, 0);
      renamed_sink = (int) (k * 7u);	// volatile scalar separator
      p = __builtin_rvtt_sfpmul (p, q, 0);
      q = __builtin_rvtt_sfpmul (q, r, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      p = __builtin_rvtt_sfpmul (p, p, 0);
      r = __builtin_rvtt_sfpmul (r, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (q, 5);
  __builtin_rvtt_sfpwritelreg (r, 6);
}
