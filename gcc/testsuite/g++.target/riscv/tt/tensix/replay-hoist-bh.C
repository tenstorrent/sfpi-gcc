// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0" }
// { dg-final { check-function-bodies "**" "" } }

void hoist ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
/*
**_Z5hoistv:
**	# READ L0
**	li	a5,4
**	TTREPLAY	0, 4, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	li	a5,0
**	# WRITE L0
**	ret
*/

void slots ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
/*
**_Z5slotsv:
**	# READ L0
**	li	a5,4
**	TTREPLAY	0, 4, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	li	a5,0
**	li	a5,4
**	TTREPLAY	4, 4, 0, 1
**	SFPADD	L0, L0, L0, 0
**	SFPADD	L0, L0, L0, 0
**	SFPADD	L0, L0, L0, 0
**	SFPADD	L0, L0, L0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	4, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	li	a5,0
**	# WRITE L0
**	ret
*/

void opaque ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      asm volatile (".ttinsn %0" :: "n" (0x70000000));
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
/*
**_Z6opaquev:
**	# READ L0
**	li	a5,4
**	TTREPLAY	0, 4, 1, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 4, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	addi	a5,a5,-1
**	bne	a5,zero,.L[0-9]+
**	# WRITE L0
**	ret
*/
