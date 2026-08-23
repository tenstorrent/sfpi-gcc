// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Named refusal (lane FW): a volatile instruction-FIFO push of a
// constant REPLAY-opcode word (TT_OP_REPLAY(0,3,1,1) = 0x04000033)
// inside the loop refuses the loop replay-preservation audit -- its
// recorded slot content is unknowable and a record form could
// re-record the hoisted slots outright.  (A raw `.ttinsn' REPLAY word
// never gets this far: the whole-function raw-capture census refuses
// all replay allocation first; the store-delivered word is exactly the
// path that census cannot see.)  The record stays in the body.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-loop-opaque: delivered REPLAY word .recorded slot content unprovable." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
void rerecord_raw_replay_word (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  volatile unsigned *fifo = (volatile unsigned *) 0xFFE40000u;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *fifo = 0x04000033u;	// TT_OP_REPLAY(0,3,1,1) pushed through the FIFO
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
