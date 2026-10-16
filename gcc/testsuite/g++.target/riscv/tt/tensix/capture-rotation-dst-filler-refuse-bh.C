// Check that capture rotation refuses an auto-incrementing store filler and a gap after an unaudited SFPSHFT2.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Capture rotation refused: filler uid=\\d+ carries a non-neutral or unaudited RWC mode on a Dst access" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: unaudited result latency after uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Capture rotation moved" "rvtt_schedule" } }

void autoinc_store_refuses ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  for (int i = 0; i < 16; i++)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto y = __builtin_rvtt_sfpmul (x, x, 0);
// Address mode 0 steps the RWC, so this store cannot be a filler.
      __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
      acc = __builtin_rvtt_sfpmuli (nullptr, acc, 0x3fc0, 0, 0, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, acc, 0x3e80, 0, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}

void shft2_neighbourhood_refuses ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto s = __builtin_rvtt_sfpreadlreg (5);
  auto t = __builtin_rvtt_sfpreadlreg (6);
  for (int i = 0; i < 16; i++)
    {
      s = __builtin_rvtt_sfpshft2_subvec_shfl1 (s, 3);
      t = __builtin_rvtt_sfpand (s, t);
      acc = __builtin_rvtt_sfpmuli (nullptr, acc, 0x3fc0, 0, 0, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, acc, 0x3e80, 0, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
  __builtin_rvtt_sfpwritelreg (s, 5);
  __builtin_rvtt_sfpwritelreg (t, 6);
}
