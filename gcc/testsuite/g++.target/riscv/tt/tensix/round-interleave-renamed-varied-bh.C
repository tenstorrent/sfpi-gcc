// Renamed, opcode- and register-varied fire twin: different function
// name, different LREGs, different chain opcodes (add/mul instead of
// mul/mad), a different reduction opcode (xor), and a different trip
// count (24, still even).  The decision keys only on the typed loop
// shape, the recurrence-circuit proof, and the pressure bound -- so
// the request still fires and the cyclic scheduler still commits.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "round-interleave: requested unroll 2 of loop" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump "List-schedule \\(round-interleave cyclic\\): bb \\d+ nodes=6 II \\d+ -> \\d+ renames=\\d+ target=bh" "rvtt_schedule" } }

void some_other_round_engine ()
{
  auto v = __builtin_rvtt_sfpreadlreg (5);
  auto s = __builtin_rvtt_sfpreadlreg (7);
  for (int k = 0; k < 24; ++k)
    {
      auto u1 = __builtin_rvtt_sfpadd (v, v, 0);
      auto u2 = __builtin_rvtt_sfpmul (u1, v, 0);
      s = __builtin_rvtt_sfpxor (s, u2);
    }
  __builtin_rvtt_sfpwritelreg (s, 7);
}
