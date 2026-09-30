// Interlock-stall shadow fill (D3): on BH the mad family's one-cycle
// result latency is a transparent hardware stall (scoreboarded, no NOP
// in the stream).  A deep independent instruction with an audited
// latency of zero moves into the stall slot.  The second function is
// the renamed, constant-varied twin (different producer opcode,
// filler opcode, registers): the decision keys only on proven
// independence and the audited latency facts.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-interlock-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Interlock-fill moved uid=\\d+ into the stall after uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void interlock_fill_deep ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto q = __builtin_rvtt_sfpmul (p, p, 0);
  auto f = __builtin_rvtt_sfpand (c, c);
  auto g = __builtin_rvtt_sfpor (d, d);
  __builtin_rvtt_sfpwritelreg (q, 0);
  __builtin_rvtt_sfpwritelreg (f, 2);
  __builtin_rvtt_sfpwritelreg (g, 3);
}

void renamed_scaled_accumulate ()
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto east = __builtin_rvtt_sfpreadlreg (5);
  auto west = __builtin_rvtt_sfpreadlreg (6);
  auto scaled = __builtin_rvtt_sfpmuli (nullptr, north, 0x3f81, 0, 0, 0);
  auto doubled = __builtin_rvtt_sfpadd (scaled, scaled, 0);
  auto blended = __builtin_rvtt_sfpxor (east, east);
  auto folded = __builtin_rvtt_sfpand (west, west);
  __builtin_rvtt_sfpwritelreg (doubled, 4);
  __builtin_rvtt_sfpwritelreg (blended, 5);
  __builtin_rvtt_sfpwritelreg (folded, 6);
}
