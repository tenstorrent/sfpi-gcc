// Check that the list scheduler treats a TTREPLAY owner and a raw .ttinsn word as barriers.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// The raw .ttinsn word bounds the region but is not named as a barrier.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: replay-owner uid=\\d+" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule refused: no modeled makespan decrease" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void replay_owner_row ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* The replay plays the following instructions by position, so this
     ends scheduling eligibility for the rest of the block.  */
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}

void opaque_word_row ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* Raw SFPCONFIG-class word: effect-opaque.  */
  asm volatile (".ttinsn %0" :: "n" (0x91000000));
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
