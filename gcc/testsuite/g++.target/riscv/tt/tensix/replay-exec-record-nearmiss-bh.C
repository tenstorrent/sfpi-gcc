// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record -fdump-rtl-rvtt_replay" }
// Near miss: a non-empty asm in the loop preheader is an unclassified
// word between the record and the first launch -- the exchange refuses
// and the record stays no-exec with every launch intact.
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 4 } }
// { dg-final { scan-rtl-dump "Exec-while-record refused" "rvtt_replay" } }
void exec_record_asm_guard ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __asm__ __volatile__ ("nop");
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
