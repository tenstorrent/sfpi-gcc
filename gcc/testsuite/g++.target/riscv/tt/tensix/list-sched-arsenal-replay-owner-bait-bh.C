// Lane DT arsenal: formed-replay-region and opaque-word motion baits.
//
// fn 1 (replay owner): an explicit TTREPLAY owner word records/plays a
// FIXED range of following delivered words by POSITION.  Reordering
// any word across (or within reach of) the owner changes what the
// replay buffer captures -- unsound at a distance, invisible to any
// value DAG.  Constructed bait: the pre-owner chain carries modeled
// mad stalls and the only independent fillers sit after the owner.
// Contract (matching the established phase discipline): the owner ends
// scheduling eligibility for the REST of the block, named
// "replay-owner"; nothing before it may be filled from beyond it.
//
// fn 2 (opaque word): a raw .ttinsn word -- the stand-in for any
// already-formed calendar the effect table cannot see -- is
// effect-opaque and must be a named barrier; the same bait shape
// around it must not move.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// MEASURED CONTRACT: the owner prints its named barrier and ends
// block eligibility (fn 1 shows exactly ONE evaluated region -- the
// pre-owner chain -- and nothing after the owner is even considered).
// The raw .ttinsn word is an asm insn: it bounds the region but the
// barrier printer only names recognized Tensix insns, so fn 2's
// witness is its two INDEPENDENTLY refused sub-regions plus the
// absence of any fire (an asm word is never crossed or moved).
// { dg-final { scan-rtl-dump-times "List-schedule barrier: replay-owner uid=\\d+" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule refused: no modeled makespan decrease" 3 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void replay_owner_bait ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* Owner word: plays 3 recorded slots; everything after it is
     positional payload -- end of eligibility.  */
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}

void opaque_word_bait ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* Raw word of an SFPCONFIG-class encoding: effect-opaque.  */
  asm volatile (".ttinsn %0" :: "n" (0x91000000));
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
