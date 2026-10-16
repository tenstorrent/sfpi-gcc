// Check that a TTREPLAY owner or a raw .ttinsn word in the row refuses round-interleave by name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "refused \\(round-interleave-denied-class\\)" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump "refused \\(round-interleave-foreign-stmt\\)" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+ \\(round-interleave-replay-owner-in-row\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+ \\(round-interleave-seam-barrier-word\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "round-interleave cyclic" "rvtt_schedule" } }

void owner_in_row ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned r = 0; r != 32; ++r)
    {
      p = __builtin_rvtt_sfpand (p, x);
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, x, x, 0);
      p = __builtin_rvtt_sfpand (p, t2);
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (p, 1);
}

void raw_word_in_row ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, x, x, 0);
      q = __builtin_rvtt_sfpxor (q, t2);
      asm volatile (".ttinsn %0" :: "n" (0x91000000));
    }
  __builtin_rvtt_sfpwritelreg (q, 2);
}
