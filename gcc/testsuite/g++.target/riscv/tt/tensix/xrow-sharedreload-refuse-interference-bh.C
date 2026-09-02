// The direct unsound-shape near-miss: the two
// halves interact through registers BEYOND the shared reload one -- a
// loop-carried accumulator is live into the row (the renamer refuses
// round-interleave-rename-live-in), and the row's pressure (four
// pinned invariants) starves the copy's remaining webs after one
// rename, so the y chain stays shared too.  The epoch merge would move
// copy-half words that write those shared registers ahead of
// first-half words that read or write them: value flow outside the
// epoch mechanism would break.  The dedupe refuses
// crossrow-shared-reload-crossrow-interference by name and the
// duplicated materializations stay.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -mtt-tensix-optimize-crossrow-shared-reload -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump {crossrow-shared-reload-crossrow-interference reg \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow shared-reload: reg" "rvtt_schedule" } }

void carried_accumulator_row (void)
{
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, 31);
  auto k5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  auto k6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e800000, 0, 0, 31);
  auto k7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000000, 0, 0, 31);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto y = __builtin_rvtt_sfpmad (x, k5, k6, 0);
      y = __builtin_rvtt_sfpmad (y, x, k7, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0xbefa66db, 0, 0, 31);
      acc = __builtin_rvtt_sfpmad (acc, x, c3, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7fbec0, 0, 0, 31);
      acc = __builtin_rvtt_sfpmad (acc, x, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 8, 7);
}
