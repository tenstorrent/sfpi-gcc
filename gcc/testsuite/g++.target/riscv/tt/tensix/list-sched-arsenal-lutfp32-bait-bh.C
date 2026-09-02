// Scheduler arsenal: unaudited-latency motion bait (SFPLUTFP32).
//
// The follow-up latency audit (rvtt-cost.md) DELIBERATELY
// left SFPLUTFP32 unaudited, by name: its Mod1/Mod1Mirror scheduling
// split (SFPLUTFP32.md keys the stalling logic on Mod1Mirror, not
// Mod1) and its per-mode register envelopes (3-entry/6-entry/FP32
// tables read different LReg sets) need their own per-mod audit; the
// SFPLUT audit does NOT transfer.  A scheduler must therefore never
// GUESS a latency for it: the instruction is a named barrier
// ("unaudited-latency"), scheduled conservatively in place.
//
// Constructed bait: the mad chain before the LUT word carries modeled
// one-slot stalls, and the SFPLUTFP32 -- whose operands are all
// chain-independent -- is exactly the filler a naive scheduler would
// sink into them.  (Its expansion also materializes the L7 indirection
// word, an audited loadi the scheduler may only treat as the LUT
// word's own dependence, never as a movable filler across it.)  Expect
// the named barrier and byte-identical order.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// MEASURED NAMING: SFPLUTFP32 prints "effect-opaque", the STRONGER
// refusal -- an earlier deferral left its whole effect record
// unaudited (per-mode register envelopes included), so admission
// refuses at the effect check before reaching the latency check.  The
// "unaudited-latency" name is reserved for effect-audited classes
// whose latency alone is missing (SFPSHFT2, covered by the scheduler
// lane's own twin).  Either name is a correct conservative refusal;
// both are accepted so a later effect audit that leaves latency
// unaudited keeps this twin green.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:effect-opaque|unaudited-latency) uid=\\d+" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule refused: no modeled makespan decrease" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void lutfp32_bait ()
{
  auto t0 = __builtin_rvtt_sfpreadlreg (0);
  auto t1 = __builtin_rvtt_sfpreadlreg (1);
  auto t2 = __builtin_rvtt_sfpreadlreg (2);
  auto v  = __builtin_rvtt_sfpreadlreg (3);
  auto p  = __builtin_rvtt_sfpreadlreg (4);
  auto x  = __builtin_rvtt_sfpreadlreg (5);
  /* Serial mad chain: bait shadows.  */
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* Chain-independent LUT word: the tempting filler.  Latency
     unaudited by name -- must not move, must not be guessed.  */
  auto r = __builtin_rvtt_sfplutfp32_3r (t0, t1, t2, v, 0);
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
