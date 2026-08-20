// Capture rotation, interior fill with the widened filler pool (lane
// DL, D3 follow-up): in a capturable counted row whose payload is a
// single dependence chain, the ONLY semantically valid stall-closing
// move is the typed trailing TTINCRWC stepping backward into a
// mad-family stall -- the audited RWC counter step commutes with the
// crossed pure-LREG word.  This is laneDG1's refusing twin
// r2a-rotate.C (its sfpi source fires verbatim under the same flags;
// archived in laneDL-evidence-20260820): every filler used to refuse on
// the wholesale Dst/RWC pool exclusion or the unaudited store/row-step
// result latency.  The second function is the renamed, constant-varied
// twin.  The mad-family members still refuse by name (nonzero latency),
// and the Dst-touching words refuse to cross each other by name.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Capture rotation moved uid=\\d+ into the in-row stall after uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: filler uid=\\d+ carries an unaudited or nonzero result latency" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: filler uid=\\d+ cannot cross uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void twin_rot ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  for (int i = 0; i < 16; i++)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto y = __builtin_rvtt_sfpmul (x, acc, 0);
      y = __builtin_rvtt_sfpaddi (nullptr, y, 0x3f00, 0, 0, 0);
      __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
      acc = __builtin_rvtt_sfpmuli (nullptr, y, 0x3fc0, 0, 0, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, acc, 0x3e80, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}

void twin_rot_renamed ()
{
  auto carry = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  for (int trip = 0; trip < 12; trip++)
    {
      auto u = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto v = __builtin_rvtt_sfpmul (u, carry, 0);
      v = __builtin_rvtt_sfpaddi (nullptr, v, 0x3e00, 0, 0, 0);
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 0, 7);
      carry = __builtin_rvtt_sfpmuli (nullptr, v, 0x4020, 0, 0, 0);
      carry = __builtin_rvtt_sfpaddi (nullptr, carry, 0x3d80, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, carry, 0, 0, 0, 0, 7);
}
