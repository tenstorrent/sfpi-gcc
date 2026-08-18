// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist" }
// Recording-epoch closure over a RAW-word payload: a user fixed capture
// whose payload is canonical `.ttinsn' constant words (the LLK TTI_ macro
// shape) closes by word count -- each canonical word is exactly one
// delivered instruction word, an architectural fact independent of the
// word's effect audit -- so formation downstream is no longer poisoned:
// the delivery-bound counted compute loop's payload records once (into
// the slots after the user's) and every trip becomes a playback launch.
// { dg-final { scan-assembler-times "TTREPLAY\t8, 8, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t8, 8, 0, 0" 32 } }
#define RAW_WORD(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

void raw_payload_epoch ()
{
  // User capture of 8 raw single-word instructions into slots [0,+8):
  // SFPNOP-class words (opcode 0x8f), the canonical TTI_ shape.
  __builtin_rvtt_ttreplay (nullptr, 8, 0, 0, 0, 0, 1);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);

  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (int i = 0; i != 32; ++i)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
