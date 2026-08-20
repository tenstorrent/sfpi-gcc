// The replay-formation coupling for the widened row-step filler (lane
// DL): counted_loop_payload (rtl-rvtt-replay.cc) refuses any loop whose
// TTINCRWC is not the trailing word, so moving the row step inward
// would trade a whole capture for one issue slot.  While replay-hoist
// is enabled the row-step filler class therefore DEFERS by name and the
// row stays byte-identical for the capture machinery.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Capture rotation refused: row-step filler uid=\\d+ deferred to replay capture formation" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Capture rotation moved" "rvtt_schedule" } }

void twin_rot_defer ()
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
