// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist" }
// Recording-epoch closure over a RAW-word payload: a user fixed capture
// whose payload is canonical `.ttinsn' constant words (the LLK TTI_ macro
// shape) closes by word count -- each canonical word is exactly one
// delivered instruction word, an architectural fact independent of the
// word's effect audit -- so formation downstream is no longer poisoned:
// the counted compute loop's payload records once (into the slots after
// the user's) and every trip becomes a playback launch.
// { dg-final { scan-assembler-times "TTREPLAY\t8, 4, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t8, 4, 0, 0" 4 } }
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

  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (int i = 0; i != 4; ++i)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
