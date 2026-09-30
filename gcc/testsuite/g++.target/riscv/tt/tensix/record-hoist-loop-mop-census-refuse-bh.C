// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// MopCfg template-census refusal: as the census-fire twin
// but MopCfg[8] (Loop1Last, consumed by both templates) is never
// programmed in-function -- a caller-armed slot could hold a REPLAY
// record word (MopCfg is per-thread state that outlives calls), so the
// census fails coverage and the MOP dispatch refuses the audit; the
// record stays in the body.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-loop-opaque: MopCfg slots not all covered by dominating stores" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
static volatile int census_refuse_sink;
void rerecord_mop_census_uncovered (unsigned n)
{
  volatile unsigned *mopcfg = (volatile unsigned *) 0xFFB80000u;
  mopcfg[0] = 4u;		// OuterCount
  mopcfg[1] = 2u;		// InnerCount
  mopcfg[2] = 0x02000000u;	// StartOp = NOP
  mopcfg[3] = 0xA2008040u;	// EndOp0 = STALLWAIT
  mopcfg[4] = 0x02000000u;	// EndOp1 = NOP
  mopcfg[5] = 0xA4000008u;	// LoopOp = SEMPOST
  mopcfg[6] = 0x02000000u;	// LoopOp1 = NOP
  mopcfg[7] = 0xA4000008u;	// Loop0Last (slot 8 deliberately unprogrammed)
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
      census_refuse_sink = (int) ix;
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
