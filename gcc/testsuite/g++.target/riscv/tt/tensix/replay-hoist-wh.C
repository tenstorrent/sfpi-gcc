// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0" }
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
**	TTREPLAY	0, 7, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPNOP
**	SFPMUL	L0, L0, L0, 0
**	SFPNOP
**	SFPMUL	L0, L0, L0, 0
**	SFPNOP
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	TTREPLAY	0, 7, 0, 0
**	TTINCRWC	0, 4, 0, 0
**	li	a5,0
**	# WRITE L0
**	ret
*/
