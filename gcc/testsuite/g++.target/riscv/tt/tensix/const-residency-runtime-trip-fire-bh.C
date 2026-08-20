// LOOP-class runtime trips (laneDM widening; re-record of the former
// trip-count-unproven refusal): correctness of the entry-edge
// programming is trip-independent -- the point is never speculated
// (rotated do-while entry), the register is established before any
// replaced use, and the admitted body cannot clobber it -- so a
// runtime trip count admits instead of refusing.  Worst case is one
// extra pushed word on a single-trip entry (rvtt-cost.md); a loop
// PROVEN single-trip still refuses by name (trip-count-single-trip),
// and the CC-canonical peel class still requires proven trips
// (const-residency-peel-trip-refuse-bh.C).  The second function is the
// renamed, bound-type-varied twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "admits runtime trips" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "trip-count" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void residency_runtime_trips (unsigned n)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_scaled_rows (int rows)
{
  auto acc = __builtin_rvtt_sfpreadlreg (4);
  for (int r = 0; r < rows; ++r)
    {
      auto w = __builtin_rvtt_sfpxloadi (nullptr, 0x3f4ccccd, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, w, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 4);
}
