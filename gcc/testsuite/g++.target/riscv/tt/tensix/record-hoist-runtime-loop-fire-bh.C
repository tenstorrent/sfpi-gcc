// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Runtime-trip tile-loop fire (the blaze sdpa_reduce_row
// RECORD-HOIST loss-class shape): a RUNTIME-counted loop whose body
// carries raw LLK sync words (constant .ttinsn SEMWAIT/SEMPOST) and a
// computed instruction-FIFO push (SETC16 base plus a bounded field --
// the LLK dest-offset flip) re-records an invariant 6-word window every
// trip.  The loop replay-preservation audit resolves every delivered
// word's opcode interval (0xa6/0xa4/0xb2: never REPLAY), the structural
// trips>=1 fact stands in for the unprovable count, and the 2-trip
// break-even clears the audited margin: the record hoists to the
// dedicated preheader, the body keeps one playback per former clone.
// Pricing: per_trip = 6*123 - 70 = 668; record_once = 7*123 + 300 =
// 1161; 2-trip benefit = 2*668 - 1161 = 175 >= 60; single-trip
// exposure = 1161 - 668 = 493 (about one record delivery, paid at most
// once per kernel entry).
// { dg-final { scan-rtl-dump "record-hoist: loop \\d+ replay-state audit admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: runtime-trip re-record window admitted .structural trips>=1, words 6, 2-trip benefit 175, single-trip exposure 493." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
void rerecord_runtime_tile_loop (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  volatile unsigned *fifo = (volatile unsigned *) 0xFFE40000u;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      __asm__ volatile (".ttinsn %0" :: "n" (0xA600800Au)); // TT_OP_SEMWAIT(1,2,2)
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      // Computed FIFO push: SETC16 base + bounded field (dest-offset flip).
      *fifo = 0xB2010000u + ((ix & 1u) << 9);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      __asm__ volatile (".ttinsn %0" :: "n" (0xA4000008u)); // TT_OP_SEMPOST(2)
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
