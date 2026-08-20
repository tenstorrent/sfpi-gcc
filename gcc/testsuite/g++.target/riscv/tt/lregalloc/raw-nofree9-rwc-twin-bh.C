// Generality twin of raw-nofree9-rwc-bh.C: renamed, different window
// increment and store address, reversed ring.  Same verdicts.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-no-free-dst" "" { target *-*-* } 0 }

void nofree9_rwc_rev (void)
{
  auto b0 = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  auto b1 = __builtin_rvtt_sfpload (nullptr, 66, 0, 0, 4, 7);
  auto b2 = __builtin_rvtt_sfpload (nullptr, 68, 0, 0, 4, 7);
  auto b3 = __builtin_rvtt_sfpload (nullptr, 70, 0, 0, 4, 7);
  auto b4 = __builtin_rvtt_sfpload (nullptr, 72, 0, 0, 4, 7);
  auto b5 = __builtin_rvtt_sfpload (nullptr, 74, 0, 0, 4, 7);
  auto b6 = __builtin_rvtt_sfpload (nullptr, 76, 0, 0, 4, 7);
  auto b7 = __builtin_rvtt_sfpload (nullptr, 78, 0, 0, 4, 7);
  auto b8 = __builtin_rvtt_sfpload (nullptr, 80, 0, 0, 4, 7);
  for (unsigned ix = 0; ix != 6; ++ix)
    {
      b0 = __builtin_rvtt_sfpxor (b0, b8);
      b1 = __builtin_rvtt_sfpxor (b1, b0);
      b2 = __builtin_rvtt_sfpxor (b2, b1);
      b3 = __builtin_rvtt_sfpxor (b3, b2);
      b4 = __builtin_rvtt_sfpxor (b4, b3);
      b5 = __builtin_rvtt_sfpxor (b5, b4);
      b6 = __builtin_rvtt_sfpxor (b6, b5);
      b7 = __builtin_rvtt_sfpxor (b7, b6);
      b8 = __builtin_rvtt_sfpxor (b8, b7);
      __builtin_rvtt_sfpstore (nullptr, b4, 96, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  auto r = __builtin_rvtt_sfpxor (b0, b1);
  r = __builtin_rvtt_sfpxor (r, b2);
  r = __builtin_rvtt_sfpxor (r, b3);
  r = __builtin_rvtt_sfpxor (r, b4);
  r = __builtin_rvtt_sfpxor (r, b5);
  r = __builtin_rvtt_sfpxor (r, b6);
  r = __builtin_rvtt_sfpxor (r, b7);
  r = __builtin_rvtt_sfpxor (r, b8);
  __builtin_rvtt_sfpstore (nullptr, r, 126, 0, 0, 4, 7);
}
