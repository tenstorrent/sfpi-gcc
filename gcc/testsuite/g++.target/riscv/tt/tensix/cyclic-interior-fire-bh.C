// Cyclic-interior FIRE: a Dst row loop whose body is chopped by CC and
// Dst barrier words into regions -- the shape the one-region cyclic
// extension refuses (round-interleave-seam-barrier-word) and the plain
// list scheduler defers whole.  The interior region carries a serial
// latency chain followed by an independent mad pair: re-list-scheduling
// the region interleaves the pair into the chain's shadows, and the
// candidate commits on a strict decrease of the WHOLE row's modeled
// steady-state II (the wrapped cyclic issue model over every issued
// word of the block).  Barrier words never move; the reorder never
// crosses the backedge; per-iteration semantics are bit-exact.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule \\(cyclic-interior\\): bb \\d+ region at uid=\\d+ nodes=6 row II \\d+ -> \\d+ target=bh" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule slot-order=0 uid=\\d+" "rvtt_schedule" } }

void cis_fire ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t3, u1, u2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
