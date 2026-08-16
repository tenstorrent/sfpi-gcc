// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// The typed face advance is an architectural Dst/RWC state boundary: it must
// never be copied into an automatically formed replay recording.  The two
// four-MUL groups share one recording; the face advance stays explicit at
// its source position between the recording and the replay.
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// { dg-final { scan-assembler-times "SFPMUL" 4 } }

void face_replay_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_ttdstface ();
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
