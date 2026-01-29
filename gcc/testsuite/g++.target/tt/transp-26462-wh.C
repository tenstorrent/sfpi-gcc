// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

namespace tng {
void one ()
{
  auto v0 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v1 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v2 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v3 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);

  auto r = __builtin_rvtt_sfptransp (v0, v1, v2, v3);
  v0 = __builtin_rvtt_sfpselect4 (r, 0);
  v1 = __builtin_rvtt_sfpselect4 (r, 1);
  v2 = __builtin_rvtt_sfpselect4 (r, 2);
  v3 = __builtin_rvtt_sfpselect4 (r, 3);

  __builtin_rvtt_wh_sfpstore (nullptr, v0, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v1, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v2, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v3, 0, 0, 0, 0, 0);
}
/*
**_ZN3tng3oneEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPLOAD	L1, 0, 0, 0
**	SFPLOAD	L2, 0, 0, 0
**	SFPLOAD	L3, 0, 0, 0
**	SFPTRANSP
**	SFPSTORE	0, L0, 0, 0
**	SFPSTORE	0, L1, 0, 0
**	SFPSTORE	0, L2, 0, 0
**	SFPSTORE	0, L3, 0, 0
**	ret
*/

void two ()
{
  auto v0 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v1 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v2 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v3 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);

  auto r = __builtin_rvtt_sfptransp (v0, v1, v2, v3);
  v0 = __builtin_rvtt_sfpselect4 (r, 0);
  v1 = __builtin_rvtt_sfpselect4 (r, 1);

  __builtin_rvtt_wh_sfpstore (nullptr, v0, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v1, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v2, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v3, 0, 0, 0, 0, 0);
}
/*
**_ZN3tng3twoEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPLOAD	L1, 0, 0, 0
**	SFPLOAD	L4, 0, 0, 0
**	SFPLOAD	L5, 0, 0, 0
**	SFPMOV	L2, L4, 2
**	SFPMOV	L3, L5, 2
**	SFPTRANSP
**	SFPSTORE	0, L0, 0, 0
**	SFPSTORE	0, L1, 0, 0
**	SFPSTORE	0, L4, 0, 0
**	SFPSTORE	0, L5, 0, 0
**	ret
*/

void three ()
{
  auto v0 = __builtin_rvtt_sfpreadlreg (8);
  auto v1 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v2 = __builtin_rvtt_sfpreadlreg (8);
  auto v3 = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);

  auto r = __builtin_rvtt_sfptransp (v0, v1, v2, v3);
  v0 = __builtin_rvtt_sfpselect4 (r, 0);
  v1 = __builtin_rvtt_sfpselect4 (r, 1);
  v2 = __builtin_rvtt_sfpselect4 (r, 2);
  v3 = __builtin_rvtt_sfpselect4 (r, 3);

  __builtin_rvtt_wh_sfpstore (nullptr, v0, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v1, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v2, 0, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, v3, 0, 0, 0, 0, 0);
}
/*
**_ZN3tng5threeEv:
**	SFPLOAD	L1, 0, 0, 0
**	SFPLOAD	L3, 0, 0, 0
**	SFPMOV	L0, L8, 2
**	SFPMOV	L2, L0, 2
**	SFPTRANSP
**	SFPSTORE	0, L0, 0, 0
**	SFPSTORE	0, L1, 0, 0
**	SFPSTORE	0, L2, 0, 0
**	SFPSTORE	0, L3, 0, 0
**	ret
*/
}

