// { dg-options "-mcpu=tt-qsr32-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void all_different ()
{
  __builtin_rvtt_ttincrwc (1, 3, 5, 7);
}
/*
**_Z13all_differentv:
**	TTINCRWC	1, 7, 5, 3
**	ret
*/
