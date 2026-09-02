// MAD-PAIR vocabulary fire (smoothstep shape): the pair's mul
// feeds the add through a single-use SFPMOV complement wrapper --
// -(a*b) + c, exactly the shape the combine's -a+b rule reduces before
// the mad rule fires.  The base discovery walks only a direct
// mul->add edge, so the EL-hoisted fold-vulnerable mul constant was
// never re-claimed: the muli immediate fold consumed it "in preference
// to mul,add->mad" and the row decayed to a per-iteration copy + MULI
// + ADD.  Under -mtt-tensix-optimize-madpair-vocabulary the discovery
// mirrors the combine's own operand vocabulary, re-claims the constant
// into a PRGM register, the fold no longer matches, and the
// pre-existing rewrite chain fuses the pair.  RECOGNITION-ONLY: no
// sfpmad appears in this pass's own gimple output.  The second
// function is the renamed, constant-varied twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-madpair-vocabulary -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair loop bb \\d+ candidate: hoisted constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-not "SFPMULI" } }

void vocab_compl_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto twice = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, twice, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      x = __builtin_rvtt_sfpadd (lift, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_edge_ramp (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto slope = __builtin_rvtt_sfpxloadi (nullptr, 0x3f400000, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 8; ++row)
    {
      auto scaled = __builtin_rvtt_sfpmul (acc, slope, 0);
      auto flipped = __builtin_rvtt_sfpmov (scaled, 1);
      acc = __builtin_rvtt_sfpadd (bias, flipped, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (acc, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
