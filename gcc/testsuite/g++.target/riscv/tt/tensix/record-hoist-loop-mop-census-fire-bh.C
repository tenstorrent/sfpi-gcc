// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// MopCfg template-census fire: the loop dispatches a type-1
// MOP (TT_OP_MOP(1,0,0) = 0x01800000, the LLK datacopy dispatch).  The
// MOP Expander may legally emit REPLAY words from its template (ISA
// MOPExpander.md), so the MOP admits only under the census: all nine
// MopCfg slots (the consumption set of BOTH templates) are programmed
// by dominating in-function constant stores whose opcode bytes are
// never REPLAY, and the function has no calls.  Census proven -> the
// audit admits, the runtime-trip pricing fires, the record hoists.
// { dg-final { scan-rtl-dump "record-hoist: loop \\d+ replay-state audit admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: runtime-trip re-record window admitted .structural trips>=1, words 6," "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
static volatile int census_fire_sink;
void rerecord_mop_census (unsigned n)
{
  volatile unsigned *mopcfg = (volatile unsigned *) 0xFFB80000u;
  mopcfg[0] = 4u;		// OuterCount
  mopcfg[1] = 2u;		// InnerCount
  mopcfg[2] = 0x02000000u;	// StartOp = NOP
  mopcfg[3] = 0xA2008040u;	// EndOp0 = STALLWAIT
  mopcfg[4] = 0x02000000u;	// EndOp1 = NOP
  mopcfg[5] = 0xA4000008u;	// LoopOp = SEMPOST
  mopcfg[6] = 0x02000000u;	// LoopOp1 = NOP
  mopcfg[7] = 0xA4000008u;	// Loop0Last
  mopcfg[8] = 0xA4000008u;	// Loop1Last
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      __asm__ volatile (".ttinsn %0" :: "n" (0x01800000u)); // TT_OP_MOP(1,0,0)
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      census_fire_sink = (int) ix;
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
