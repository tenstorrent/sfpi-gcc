// Near miss for the SFPIADD immediate-form audit: a RUNTIME immediate
// selects the runtime-synthesized alternatives, which patch and push
// an instruction word through memory -- effects the audit deliberately
// does not cover (rvtt-cost.md).  The identical body with constant
// immediates fires (replay-hoist-iadd-imm-audit-bh.C); here no payload
// forms at all and nothing is captured.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "0 candidates" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

extern unsigned int __instrn_buffer[];

void iadd_synth_nearmiss (int n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpiadd_i (__instrn_buffer, c, n, 0, 0, 4);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpiadd_i (__instrn_buffer, c, n, 0, 0, 4);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
