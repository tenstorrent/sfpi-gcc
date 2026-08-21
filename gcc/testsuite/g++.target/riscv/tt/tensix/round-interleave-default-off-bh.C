// Default-off proof: without -mtt-tensix-optimize-round-interleave the
// request pass never runs, the self-loop deferral keeps its original
// capture-rotation wording under the plain list scheduler, and the
// repeated-row deferral keeps its wording -- the round-interleave
// machinery is invisible at every default.  Same fire-shaped kernels
// as the fire twin and the iso-pair twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+ \\(capture rotation owns the backedge seam\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 2 "rvtt_schedule" } }
// (The scan-not keys on the machinery's "(round-interleave" spelling --
// fire tag and deferral names -- because this file's own NAME appears in
// the dump's source-location strings.)
// { dg-final { scan-rtl-dump-not "\\(round-interleave" "rvtt_schedule" } }

void rci_rounds_off ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (0);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, x, x, 0);
      acc = __builtin_rvtt_sfpand (acc, t2);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}

void iso_pair_off ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto b = __builtin_rvtt_sfpreadlreg (3);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  __builtin_rvtt_sfppopc (0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
