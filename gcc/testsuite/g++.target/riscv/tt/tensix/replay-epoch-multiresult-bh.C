// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Ownership-epoch scoping of user recordings (TOP3-2 layer 2).
//
// Capture 1's payload is opaque asm: its recording epoch is unprovable and
// formation is refused only until the next explicit replay owner -- not for
// the whole function (the legacy behaviour without the hoist flag).
// Capture 2's payload is entirely typed multi-result work (one indexed
// SFPSWAP between two eight-definition SFPTRANSPs): the typed instruction
// lengths prove the epoch closed and the capture model retains the
// multi-result members.  The launch loop after both epochs is therefore
// still eligible for the launch-loop unroll, like everything else.
//
// { dg-final { scan-rtl-dump "User capture .0,\\+2.: recording epoch unprovable .opaque payload at insn \\d+.; refusing formation until the next replay owner" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "User capture .8,\\+3.: typed epoch closed at insn \\d+; payload retains 3 multi-result insn.s." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Unrolled launch loop bb \\d+: 6 trips x 1 delivered words" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "User capturing across basic block" "rvtt_replay" } }

void epoch_scoped ()
{
  // Unprovable epoch: two raw instruction words recorded (opaque to the
  // typed stream; both are SFPNOP encodings, content irrelevant).
  __builtin_rvtt_ttreplay (nullptr, 2, 0, 0, 0, 1, 1);
  asm volatile (".ttinsn 0x91800000");
  asm volatile (".ttinsn 0x91800000");

  // Typed multi-result epoch: TRANSP, indexed SWAP, TRANSP = 3 words.
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 8, 1, 1);
  {
    auto v0 = __builtin_rvtt_sfpreadlreg (0);
    auto v1 = __builtin_rvtt_sfpreadlreg (1);
    auto v2 = __builtin_rvtt_sfpreadlreg (2);
    auto v3 = __builtin_rvtt_sfpreadlreg (3);
    auto i0 = __builtin_rvtt_sfpreadlreg (4);
    auto i1 = __builtin_rvtt_sfpreadlreg (5);
    auto i2 = __builtin_rvtt_sfpreadlreg (6);
    auto i3 = __builtin_rvtt_sfpreadlreg (7);
    auto t = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, i0, i1, i2, i3);
    v0 = __builtin_rvtt_sfpselect4 (t, 0);
    v1 = __builtin_rvtt_sfpselect4 (t, 1);
    v2 = __builtin_rvtt_sfpselect4 (t, 2);
    v3 = __builtin_rvtt_sfpselect4 (t, 3);
    i0 = __builtin_rvtt_sfpreadlreg (4);
    i1 = __builtin_rvtt_sfpreadlreg (5);
    i2 = __builtin_rvtt_sfpreadlreg (6);
    i3 = __builtin_rvtt_sfpreadlreg (7);
    auto s = __builtin_rvtt_sfpswap_indexed (v0, v1, i0, i1, 1);
    v0 = __builtin_rvtt_sfpselect4 (s, 0);
    v1 = __builtin_rvtt_sfpselect4 (s, 1);
    i0 = __builtin_rvtt_sfpselect4 (s, 2);
    i1 = __builtin_rvtt_sfpselect4 (s, 3);
    auto u = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, i0, i1, i2, i3);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (u, 0), 0);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (u, 1), 1);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (u, 2), 2);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (u, 3), 3);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (4), 4);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (5), 5);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (6), 6);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (7), 7);
  }

  // The bitonic-stage launch loop shape: pure playback delivery under a
  // proven trip count unrolls to straight-line launches.
  for (unsigned i = 0; i != 6; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 8, 0, 0);
}
