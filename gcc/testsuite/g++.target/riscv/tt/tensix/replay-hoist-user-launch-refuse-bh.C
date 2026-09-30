// USER-written launch twin of replay-hoist-planner-launch-fire-bh.C:
// launches spelled as raw `.ttinsn' words (the LLK TTI_ macro shape)
// never acquire a planner emission record -- they are asm, refused
// UPSTREAM of any effect query: the loop containing them is not even
// eligible for record hoisting (loop_preserves_replay_p), and the raw
// words themselves keep the refusing opaque default of the effect
// vocabulary.  The audited mad-family repeat sequence beside them
// would otherwise be a hoist candidate (the execbound fire witnesses),
// so the refusal keys on the user-written words alone.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Not hoisting: loop contains call, opaque asm, or replay owner" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "planner-derived launch effects" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }

void user_launch_loop ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      // The user-written launch pair (alternating-VD SFPLOADMACRO
      // words, the handwritten typecast TTI_ shape).
      asm volatile (".ttinsn %0" :: "n" (0x9306c000u));
      asm volatile (".ttinsn %0" :: "n" (0x9316c000u));
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
