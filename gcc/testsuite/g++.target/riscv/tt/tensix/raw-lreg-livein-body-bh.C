// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

/* Keep L1 raw-owned across the load.  The other explicit live values make L1
   attractive to IRA unless the ownership marker is converted into a visible
   interval.  */
void
raw_lreg_reserves_l1 ()
{
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto l4 = __builtin_rvtt_sfpreadlreg (4);
  auto l5 = __builtin_rvtt_sfpreadlreg (5);
  auto l6 = __builtin_rvtt_sfpreadlreg (6);
  auto value = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  value = __builtin_rvtt_sfpmad (value, l0, l2, 0);
  value = __builtin_rvtt_sfpmad (value, l3, l4, 0);
  value = __builtin_rvtt_sfpmad (value, l5, l6, 0);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

/*
**_Z20raw_lreg_reserves_l1v:
**	# RAWLREG 0, 2
**...
**	SFPLOAD	L[0234567], 0, 0, 0
**...
**	# WRITE L0
**	ret
*/

// { dg-final { scan-assembler-not "SFPLOAD\\tL1," } }
