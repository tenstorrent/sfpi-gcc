// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mtt-tensix-optimize-rename-temporal -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// R1 cyclic-interior consumer UNDO: the region's collision chains
// rename through the service, but the interleave is already optimal
// (the independent mad sits in the serial chain's shadow in original
// order), so no candidate proves a strict whole-row II decrease --
// the consumer undoes every requested rename exactly and the named
// refusal stands.  The transactional identity (undo leg byte-equal to
// the flag-off leg) is proven in the lane evidence; this twin pins
// the undo channel and the refusal.
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): chain L\\d+ -> L\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): undid \\d+ chain rename" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "cyclic-interior-no-ii-decrease" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule \\(interior-rename\\): committed" "rvtt_schedule" } }
void irn_undo ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto w  = __builtin_rvtt_sfpmad (t2, u1, v, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
