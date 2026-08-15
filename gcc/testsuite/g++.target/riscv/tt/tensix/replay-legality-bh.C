// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void typed_load_swap ()
{
#pragma GCC unroll 4
  for (unsigned ix = 0; ix != 4; ix++)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
      auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
      auto pair = __builtin_rvtt_sfpswap (a, b, 0);
      a = __builtin_rvtt_sfpselect2 (pair, 0);
      b = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
      __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 0, 0);
    }
}
/*
**_Z15typed_load_swapv:
**	TTREPLAY	0, 5, 1, 1
**	SFPLOAD	L1, 0, 0, 0
**	SFPLOAD	L0, 0, 0, 0
**	SFPSWAP	L1, L0, 0
**	SFPSTORE	L1, 0, 0, 0
**	SFPSTORE	L0, 0, 0, 0
**	TTREPLAY	0, 5, 0, 0
**	TTREPLAY	0, 5, 0, 0
**	TTREPLAY	0, 5, 0, 0
**	ret
*/

void config_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwriteconfig_v (x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
/*
**_Z14config_barrierv:
**	# READ L0
**	TTREPLAY	0, 4, 1, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPNOP
**	SFPCONFIG	0, 0, 0	# R:L0 CFG:0
**	TTREPLAY	0, 4, 0, 0
**	# WRITE L0
**	ret
*/

void opaque_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  asm volatile (".ttinsn %0" :: "n" (0x70000000));
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
/*
**_Z14opaque_barrierv:
**	# READ L0
**	TTREPLAY	0, 4, 1, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	0, 4, 0, 0
**	# WRITE L0
**	ret
*/
