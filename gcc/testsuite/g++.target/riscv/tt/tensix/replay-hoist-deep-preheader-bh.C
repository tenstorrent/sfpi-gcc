// A six-block, single-predecessor preheader chain is representation noise:
// it must not hide the constant counter initializer from replay hoisting.
// Each asm-goto has only its named successor, so the loop has six real RTL
// predecessor blocks rather than source-only empty labels.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }

void deep_preheader_replay ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  unsigned ix = 0;
  asm goto ("# replay split 1" : : "r" (ix) : : s1);
  __builtin_unreachable ();
s1:
  asm goto ("# replay split 2" : : "r" (ix) : : s2);
  __builtin_unreachable ();
s2:
  asm goto ("# replay split 3" : : "r" (ix) : : s3);
  __builtin_unreachable ();
s3:
  asm goto ("# replay split 4" : : "r" (ix) : : s4);
  __builtin_unreachable ();
s4:
  asm goto ("# replay split 5" : : "r" (ix) : : s5);
  __builtin_unreachable ();
s5:
  asm goto ("# replay split 6" : : "r" (ix) : : s6);
  __builtin_unreachable ();
s6:
  for (; ix != 32; ++ix)
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
