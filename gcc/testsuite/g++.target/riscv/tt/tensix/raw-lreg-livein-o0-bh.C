// { dg-options "-mcpu=tt-bh-tensix -O0 -fno-exceptions -fno-rtti" }

/* The ownership pass is a correctness pass and must run at -O0.  Keep a raw
   value in L1 across a compiler-generated load, then make the raw value visible
   to RTL with its typed consumer.  */
void
raw_lreg_reserves_l1_at_o0 ()
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  (void) __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  (void) __builtin_rvtt_sfpreadlreg (1);
}

// { dg-final { scan-assembler "# RAWLREG 0, 2" } }
// { dg-final { scan-assembler "SFPLOAD\\t(L0|L2|L3|L4|L5|L6|L7)," } }
// { dg-final { scan-assembler-not "SFPLOAD\\tL1," } }
