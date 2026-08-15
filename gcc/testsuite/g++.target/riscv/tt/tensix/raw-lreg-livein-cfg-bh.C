// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }

/* L1 is produced by opaque raw LLK code and consumed after a control-flow
   merge.  Each branch ends in an unused, volatile SFPU definition.  The
   fallthrough definition must not reuse L1 merely because the block-end
   sentinel was placed before that final instruction.  */
void
raw_lreg_live_across_edge (int choose)
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto l4 = __builtin_rvtt_sfpreadlreg (4);
  auto l5 = __builtin_rvtt_sfpreadlreg (5);
  auto l6 = __builtin_rvtt_sfpreadlreg (6);
  if (choose)
    (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  auto l1 = __builtin_rvtt_sfpreadlreg (1);
  auto result = __builtin_rvtt_sfpmad (l1, l0, l2, 0);
  result = __builtin_rvtt_sfpmad (result, l3, l4, 0);
  result = __builtin_rvtt_sfpmad (result, l5, l6, 0);
  __builtin_rvtt_sfpwritelreg (result, 4);
}

/* Ordinary raw reads do not release ownership.  The first marker starts L1;
   only the marker after the last opaque raw use releases L1 and starts the
   newly produced L7 value.  */
void
raw_lreg_last_use_contract ()
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto l4 = __builtin_rvtt_sfpreadlreg (4);
  auto l5 = __builtin_rvtt_sfpreadlreg (5);
  auto l6 = __builtin_rvtt_sfpreadlreg (6);
  asm volatile ("# RAW READ L1 first");
  (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  asm volatile ("# RAW READ L1 last");
  __builtin_rvtt_sfprawlreg_access (0x02, 0x80);
  auto l7 = __builtin_rvtt_sfpreadlreg (7);
  auto result = __builtin_rvtt_sfpmad (l7, l0, l2, 0);
  result = __builtin_rvtt_sfpmad (result, l3, l4, 0);
  result = __builtin_rvtt_sfpmad (result, l5, l6, 0);
  __builtin_rvtt_sfpwritelreg (result, 4);
}

void
raw_lreg_live_across_loop (int count)
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto l4 = __builtin_rvtt_sfpreadlreg (4);
  auto l5 = __builtin_rvtt_sfpreadlreg (5);
  auto l6 = __builtin_rvtt_sfpreadlreg (6);
  while (count-- > 0)
    (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  auto l1 = __builtin_rvtt_sfpreadlreg (1);
  auto result = __builtin_rvtt_sfpmad (l1, l0, l2, 0);
  result = __builtin_rvtt_sfpmad (result, l3, l4, 0);
  result = __builtin_rvtt_sfpmad (result, l5, l6, 0);
  __builtin_rvtt_sfpwritelreg (result, 4);
}

void
raw_lreg_live_across_multi_pred (int outer, int inner)
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto l4 = __builtin_rvtt_sfpreadlreg (4);
  auto l5 = __builtin_rvtt_sfpreadlreg (5);
  auto l6 = __builtin_rvtt_sfpreadlreg (6);
  if (outer)
    {
      if (inner)
        (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
    }
  else
    (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  auto l1 = __builtin_rvtt_sfpreadlreg (1);
  auto result = __builtin_rvtt_sfpmad (l1, l0, l2, 0);
  result = __builtin_rvtt_sfpmad (result, l3, l4, 0);
  result = __builtin_rvtt_sfpmad (result, l5, l6, 0);
  __builtin_rvtt_sfpwritelreg (result, 4);
}

// { dg-final { scan-assembler "# RAWLREG 0, 2" } }
// { dg-final { scan-assembler "# RAWLREG 2, 128" } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL[0234567]," 4 } }
// { dg-final { scan-assembler-not "SFPLOAD\\tL1," } }
