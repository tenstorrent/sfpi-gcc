// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Renamed-varied twin of record-hoist-runtime-loop-fire-bh.C:
// different names, LRegs and operand roles, a STALLWAIT sync word, and
// a masked-counter field in the computed push.  Same structural facts,
// so the same admission: per_trip = 6*123 - 70 = 668; record_once =
// 7*123 + 300 = 1161; 2-trip benefit 175 >= 60; exposure 493.
// { dg-final { scan-rtl-dump "record-hoist: loop \\d+ replay-state audit admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: runtime-trip re-record window admitted .structural trips>=1, words 6, 2-trip benefit 175, single-trip exposure 493." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
void tile_walk_varied (unsigned count)
{
  auto p = __builtin_rvtt_sfpreadlreg (4);
  auto q = __builtin_rvtt_sfpreadlreg (5);
  auto r = __builtin_rvtt_sfpreadlreg (6);
  volatile unsigned *push = (volatile unsigned *) 0xFFE40000u;
  for (unsigned t = 0; t != count; ++t)
    {
      __asm__ volatile (".ttinsn %0" :: "n" (0xA2008040u)); // TT_OP_STALLWAIT(1,64)
      p = __builtin_rvtt_sfpmul (p, q, 0);
      q = __builtin_rvtt_sfpmul (q, r, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      p = __builtin_rvtt_sfpmul (p, p, 0);
      r = __builtin_rvtt_sfpmul (r, p, 0);
      *push = 0xB2010000u + ((t & 3u) << 2);
      p = __builtin_rvtt_sfpmul (p, q, 0);
      q = __builtin_rvtt_sfpmul (q, r, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      p = __builtin_rvtt_sfpmul (p, p, 0);
      r = __builtin_rvtt_sfpmul (r, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (q, 5);
  __builtin_rvtt_sfpwritelreg (r, 6);
}
