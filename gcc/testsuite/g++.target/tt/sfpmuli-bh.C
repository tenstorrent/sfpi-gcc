// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void foo ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x123456, 0, 0, 18);
  auto c = __builtin_rvtt_sfpmul (a, b, 0);
  __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 0);
}
/*
**_Z3foov:
**	SFPLOAD	L0, 0, 0, 0
**	SFPLOADI	L1, 18, 8
**	SFPLOADI	L1, 13398, 10	# LV:L1
**	SFPMUL	L0, L0, L1, L9, 0
**	SFPNOP
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/

void bar ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x48000000, 0, 0, 18);
  auto c = __builtin_rvtt_sfpmul (a, b, 0);
  __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 0);
}
/*
**_Z3barv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMULI	L0, 18432, 0
**	SFPNOP
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/

void baz ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x4800, 0, 0, 0);
  auto c = __builtin_rvtt_sfpmul (a, b, 0);
  __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 0);
}
/*
**_Z3bazv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMULI	L0, 18432, 0
**	SFPNOP
**	SFPSTORE	L0, 0, 0, 0
**	ret
*/
