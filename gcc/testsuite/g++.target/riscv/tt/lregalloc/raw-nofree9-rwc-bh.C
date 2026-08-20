// Refusal edge: 9 simultaneously live values where NO Dst row is
// provably free across the pressure region.  The RWC window moves
// (TTINCRWC) inside the loop and the kernel stores through the moving
// window every iteration: Dst-row identity is only provable within one
// rwc epoch (dst-ownership discipline), so no scratch row can be proven
// safe for a spill slot anywhere in the live range.
// TODAY: refuses lreg-pressure-exceeded (no spiller exists).
// FUTURE-VERDICT (LREG allocator): NAMED REFUSAL lreg-spill-no-free-dst
// -- compiling this by spilling into an unproven row is an arsenal FAIL
// (it can clobber live tile data at an unknowable address).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-no-free-dst" "" { target *-*-* } 0 }

void nofree9_rwc (void)
{
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
      /* Store through the moving window, then advance it: every Dst row
	 is potentially written; none is provably free.  */
      __builtin_rvtt_sfpstore (nullptr, a0, 32, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  auto r = __builtin_rvtt_sfpxor (a0, a1);
  r = __builtin_rvtt_sfpxor (r, a2);
  r = __builtin_rvtt_sfpxor (r, a3);
  r = __builtin_rvtt_sfpxor (r, a4);
  r = __builtin_rvtt_sfpxor (r, a5);
  r = __builtin_rvtt_sfpxor (r, a6);
  r = __builtin_rvtt_sfpxor (r, a7);
  r = __builtin_rvtt_sfpxor (r, a8);
  __builtin_rvtt_sfpstore (nullptr, r, 62, 0, 0, 4, 7);
}
