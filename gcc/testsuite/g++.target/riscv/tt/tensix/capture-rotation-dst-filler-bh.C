// Capture rotation, widened Dst-filler class (D3 follow-up):
// the audited SFPSTORE (result latency 0, no RWC step of its own) moves
// into a mad-family stall as a plain in-row reorder, because every
// crossed word is proven Dst-, RWC-, CC-, and configuration-inert.  One
// move heals both modeled stalls: the vacated mad->store adjacency and
// the gap it fills.  The row-step word refuses to cross the store by
// name (fail-closed Dst/RWC crossing discipline).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Capture rotation moved uid=\\d+ into the in-row stall after uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: filler uid=\\d+ cannot cross uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void dst_filler_fires ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  for (int i = 0; i < 16; i++)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto y = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
      acc = __builtin_rvtt_sfpmuli (nullptr, acc, 0x3fc0, 0, 0, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, acc, 0x3e80, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}

void dst_filler_fires_renamed ()
{
  auto carry = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  for (int trip = 0; trip < 12; trip++)
    {
      auto u = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto w = __builtin_rvtt_sfpmul (u, u, 0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      carry = __builtin_rvtt_sfpmuli (nullptr, carry, 0x4020, 0, 0, 0);
      carry = __builtin_rvtt_sfpaddi (nullptr, carry, 0x3d80, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, carry, 0, 0, 0, 0, 7);
}
