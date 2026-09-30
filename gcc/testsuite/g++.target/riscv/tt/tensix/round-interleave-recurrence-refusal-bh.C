// DEPENDENT RECURRENCE refusals, by name.  fn 1: the Stein-round
// class -- a loop-carried value whose recurrence circuit is a
// multi-statement chain (x feeds a two-op chain that redefines x).
// Interleaving cannot overlap work each iteration serially consumes
// from its predecessor: two-ahead execution of copy 2's circuit would
// wait on copy 1's circuit exactly as the rolled loop does.  fn 2: a
// body that is ALL circuit (single-op recurrences, but no off-circuit
// word to interleave) refuses with the same name.  Refusal =
// byte-identical: no annotation, no unroll, and the self-loop keeps
// its deferral downstream.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "refused \\(round-interleave-dependent-recurrence\\) loop \\d+: multi-statement recurrence circuit" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump "refused \\(round-interleave-dependent-recurrence\\) loop \\d+: no off-circuit work to interleave" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump-not "round-interleave cyclic" "rvtt_schedule" } }

void stein_like_rounds ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (1);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t = __builtin_rvtt_sfpmad (x, x, c, 0);	// on x's circuit
      x = __builtin_rvtt_sfpmul (t, t, 0);		// redefines x
      acc = __builtin_rvtt_sfpand (acc, c);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (acc, 2);
}

void all_circuit_rounds ()
{
  auto a = __builtin_rvtt_sfpreadlreg (3);
  auto b = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned r = 0; r != 24; ++r)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);	// a's whole circuit
      b = __builtin_rvtt_sfpadd (b, b, 0);	// b's whole circuit
    }
  __builtin_rvtt_sfpwritelreg (a, 3);
  __builtin_rvtt_sfpwritelreg (b, 4);
}
