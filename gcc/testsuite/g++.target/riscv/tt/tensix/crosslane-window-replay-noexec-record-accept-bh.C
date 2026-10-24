// Check that a no-exec capture inside an open window is accepted when every launch
// plays outside it (REPLAY.md: Load=1/Exec=0 issues nothing).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 1 } }

using vec_t = __xtt_vector;

void noexec_record_in_window ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);              // OPEN
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);  // no-exec capture [0,4)
  vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t y = __builtin_rvtt_sfpand (x, x);
  __builtin_rvtt_sfpwritelreg (y, 5);                   // swallowed, not executed
  vec_t z = __builtin_rvtt_sfpmul (y, y, 0);
  __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 0, 7);
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);              // CLOSE
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);  // plays outside: legal
}
