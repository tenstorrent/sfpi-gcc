// Renamed/varied fire twin: different symbols, a different aligned Dst
// address, a different (even) trip count -- the structural decision is
// identical.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=24->12" "rvtt_schedule" } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 6, 0, 7} } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

void arbitrary_packet_transform ()
{
  for (unsigned packet = 0; packet != 24; ++packet)
    {
      auto north = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
      auto east = __builtin_rvtt_sfpabs (north, 1);
      auto west = __builtin_rvtt_sfpmul (east, north, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      east = __builtin_rvtt_sfpadd (east, north, 0);
      __builtin_rvtt_sfppopc (0);
      west = __builtin_rvtt_sfpmad (east, west, north, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      east = __builtin_rvtt_sfpxor (east, west);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, east, 4, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
