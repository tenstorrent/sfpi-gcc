// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Even textually inert inline asm has no replay-preservation contract.  It
// remains opaque rather than making source-text assumptions in an RTL pass.
// { dg-final { scan-rtl-dump "Persistent counted ownership blocker: opaque-asm" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Persistent counted hoist refused: outer-loop-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Persistent counted hoist:" "rvtt_replay" } }

extern "C" void
persistent_page_opaque_refuse ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned page = 0; page != 8; ++page)
    {
      asm volatile ("# replay ownership opaque" ::: "memory");
      for (unsigned row = 0; row != 8; ++row)
        {
          a = __builtin_rvtt_sfpmul (a, b, 0);
          b = __builtin_rvtt_sfpmul (b, c, 0);
          c = __builtin_rvtt_sfpmul (c, a, 0);
          a = __builtin_rvtt_sfpmul (a, a, 0);
          b = __builtin_rvtt_sfpmul (b, b, 0);
          c = __builtin_rvtt_sfpmul (c, c, 0);
          a = __builtin_rvtt_sfpmul (a, b, 0);
          b = __builtin_rvtt_sfpmul (b, c, 0);
          c = __builtin_rvtt_sfpmul (c, a, 0);
          a = __builtin_rvtt_sfpmul (a, a, 0);
          b = __builtin_rvtt_sfpmul (b, b, 0);
          c = __builtin_rvtt_sfpmul (c, c, 0);
          a = __builtin_rvtt_sfpmul (a, b, 0);
          b = __builtin_rvtt_sfpmul (b, c, 0);
          c = __builtin_rvtt_sfpmul (c, a, 0);
          a = __builtin_rvtt_sfpmul (a, a, 0);
          b = __builtin_rvtt_sfpmul (b, b, 0);
          c = __builtin_rvtt_sfpmul (c, c, 0);
        }
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
