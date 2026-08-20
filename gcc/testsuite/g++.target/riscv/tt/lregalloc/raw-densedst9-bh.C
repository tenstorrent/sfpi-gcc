// Refusal edge / clobber trap: a full 64-row Dst window (rows 0..63,
// bytes 0..126) is written with live kernel data BEFORE the 9-live
// pressure region and read back AFTER it.  Any spill slot chosen inside
// the kernel's own window clobbers checked data.
// TODAY: refuses lreg-pressure-exceeded.
// FUTURE-VERDICT (LREG allocator): either NAMED REFUSAL
// lreg-spill-no-free-dst, or compile with a spill slot PROVABLY outside
// rows 0..63 -- in which case the CRAQ golden checks all 64 rows
// bit-exactly and any clobber fails the gate (see ARSENAL.md).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-no-free-dst" "" { target *-*-* } 0 }

void densedst9 (void)
{
  /* Occupy all 64 rows with live data derived from row 0.  */
  auto seed = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  for (unsigned row = 1; row != 64; ++row)
    {
      seed = __builtin_rvtt_sfpxor (seed, seed);
      __builtin_rvtt_sfpstore (nullptr, seed, 2 * row, 0, 0, 4, 7);
    }
  /* The 9-live pressure region.  */
  auto a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto a1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
  auto a2 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  auto a3 = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 4, 7);
  auto a4 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  auto a5 = __builtin_rvtt_sfpload (nullptr, 10, 0, 0, 4, 7);
  auto a6 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 4, 7);
  auto a7 = __builtin_rvtt_sfpload (nullptr, 14, 0, 0, 4, 7);
  auto a8 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 4, 7);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      a0 = __builtin_rvtt_sfpxor (a0, a1);
      a1 = __builtin_rvtt_sfpxor (a1, a2);
      a2 = __builtin_rvtt_sfpxor (a2, a3);
      a3 = __builtin_rvtt_sfpxor (a3, a4);
      a4 = __builtin_rvtt_sfpxor (a4, a5);
      a5 = __builtin_rvtt_sfpxor (a5, a6);
      a6 = __builtin_rvtt_sfpxor (a6, a7);
      a7 = __builtin_rvtt_sfpxor (a7, a8);
      a8 = __builtin_rvtt_sfpxor (a8, a0);
    }
  /* Read every occupied row back and fold it into the result: all 64
     rows are live across the pressure region.  */
  auto r = __builtin_rvtt_sfpxor (a0, a1);
  r = __builtin_rvtt_sfpxor (r, a2);
  r = __builtin_rvtt_sfpxor (r, a3);
  r = __builtin_rvtt_sfpxor (r, a4);
  r = __builtin_rvtt_sfpxor (r, a5);
  r = __builtin_rvtt_sfpxor (r, a6);
  r = __builtin_rvtt_sfpxor (r, a7);
  r = __builtin_rvtt_sfpxor (r, a8);
  for (unsigned row = 1; row != 64; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 2 * row, 0, 0, 4, 7);
      r = __builtin_rvtt_sfpxor (r, v);
    }
  __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 4, 7);
}
