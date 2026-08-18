// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist" }
// Near misses for the raw-word epoch accounting, refusing by name:
// 1: a raw word of the architectural replay-owner opcode inside the
//    payload -- the typed OWNER_DURING_CAPTURE refusal must not be
//    bypassable by spelling the owner raw;
// 2: a non-canonical (register-operand) raw word.
// Both leave the epoch unprovable: no compression forms after either
// capture (the only TTREPLAY words are the user records).
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }
#define RAW_WORD(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

void raw_owner_word ()
{
  __builtin_rvtt_ttreplay (nullptr, 2, 0, 0, 0, 0, 1);
  RAW_WORD (0x8f000000u);
  RAW_WORD (0x04000000u);	// raw replay-owner opcode: refuse
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

void raw_register_word ()
{
  unsigned w = 0x8f000000u;
  __builtin_rvtt_ttreplay (nullptr, 1, 0, 0, 0, 0, 1);
  __asm__ __volatile__ (".ttinsn %0" :: "r" (w));	// non-constant: refuse
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
