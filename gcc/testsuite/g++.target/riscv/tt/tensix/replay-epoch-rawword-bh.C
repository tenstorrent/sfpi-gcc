// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist" }
// Check that a user capture of raw .ttinsn words closes its recording epoch by word count, so a later counted loop still forms a replay.
// { dg-final { scan-assembler-times "TTREPLAY\t8, 8, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t8, 8, 0, 0" 32 } }
#define RAW_WORD(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

void raw_payload_epoch ()
{
  // User capture of 8 raw single-word instructions into slots [0,+8):
  // SFPNOP-class words (opcode 0x8f).
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
