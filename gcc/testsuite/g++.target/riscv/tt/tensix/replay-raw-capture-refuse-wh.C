// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fdump-rtl-rvtt_replay" }
// Check that a raw .ttinsn REPLAY word refuses all replay allocation in its function on Wormhole while a clean function still forms.
// { dg-final { scan-rtl-dump "replay-raw-capture-present: raw .ttinsn word 0x04000000" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 4 } }

void raw_replay_word ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __asm__ __volatile__ (".ttinsn %0" :: "n" (0x04000000u));
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// A raw word with a benign opcode is an ordinary boundary; replay still
// allocates (capture plus launches).
void clean_benign_word ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __asm__ __volatile__ (".ttinsn %0" :: "n" (0x70000000u));
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
