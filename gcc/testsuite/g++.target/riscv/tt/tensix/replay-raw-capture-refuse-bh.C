// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fdump-rtl-rvtt_replay" }
// Fail-closed raw-capture census: a hand-authored raw ".ttinsn" word
// carrying the architectural REPLAY opcode (0x04......) is invisible
// to the typed slot pool -- without the census, the allocator would
// silently form TTREPLAY over slots the raw word already owns (the
// benign-opcode control is the pinned opaque_barrier body in
// replay-legality-bh.C, which still allocates slot 0 around a
// 0x70000000 word).  With the census, RAW_REPLAY_WORD's function
// refuses ALL replay allocation by name, while the clean function in
// the same TU still forms (its capture+launch = the only 2 TTREPLAY
// words in this file).
// { dg-final { scan-rtl-dump "replay-raw-capture-present: raw .ttinsn word 0x04000000" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }

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

// Byte-for-byte the opaque_barrier shape from replay-legality-bh.C: a
// benign-opcode raw word stays an ordinary boundary and the pass still
// allocates (TTREPLAY capture + one launch).
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
