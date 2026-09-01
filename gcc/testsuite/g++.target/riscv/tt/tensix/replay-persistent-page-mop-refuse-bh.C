// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// MOP and REPLAY share the replay buffer: a MOP template may contain a
// REPLAY word.  Therefore a fixed raw MOP in the page shell is a replay
// consumer, not a replay-transparent raw word, and outer promotion refuses.
// { dg-final { scan-rtl-dump "Persistent counted ownership blocker: raw-mop-replay-consumer" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Persistent counted hoist refused: outer-loop-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Persistent counted hoist:" "rvtt_replay" } }

#define ROW_BODY                                                       \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0)

extern "C" void
persistent_page_mop_refuse ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned page = 0; page != 8; ++page)
    {
      // Architectural opcode 0x01 is a type-0 MOP launch.
      asm volatile (".ttinsn %0" :: "n" (0x01000000));
      for (unsigned row = 0; row != 8; ++row)
        { ROW_BODY; }
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
