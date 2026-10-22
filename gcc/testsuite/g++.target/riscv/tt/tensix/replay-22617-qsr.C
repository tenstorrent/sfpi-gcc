// { dg-options "-mcpu=tt-qsr32-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void one ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 0, 0);
  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}
/*
**_Z3onev:
**	TTREPLAY	4 >> 10, 4 & 0x3ff, 0, 0, 0, 1
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	4100 >> 10, 4100 & 0x3ff, 0, 0, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	4100 >> 10, 4100 & 0x3ff, 0, 0, 0, 0
**	TTREPLAY	4100 >> 10, 4100 & 0x3ff, 0, 0, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void two (volatile unsigned *ptr)
{
  if (*ptr)
    {
      __builtin_rvtt_ttreplay (nullptr, (2 << 10) | 2, 0, 0, 0, 1, 0, 0);
      __builtin_rvtt_sfpnop ();
      __builtin_rvtt_sfpnop ();
    }

  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  (*ptr)++;
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}
/*
**_Z3twoPVj:
**	lw	a5,0\(a0\)
**	beq	a5,zero,.L[0-9]+
**	TTREPLAY	2050 >> 10, 2050 & 0x3ff, 0, 0, 0, 1
**	SFPNOP
**	SFPNOP
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	TTREPLAY	4101 >> 10, 4101 & 0x3ff, 0, 0, 0, 1
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	TTREPLAY	4101 >> 10, 4101 & 0x3ff, 0, 0, 0, 0
**	lw	a5,0\(a0\)
**	addi	a5,a5,1
**	sw	a5,0\(a0\)
**	TTREPLAY	4101 >> 10, 4101 & 0x3ff, 0, 0, 0, 0
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void three (volatile unsigned *ptr)
{
  if (*ptr)
    {
      __builtin_rvtt_ttreplay (nullptr, (2 << 10) | 4, 0, 0, 0, 1, 0, 0);
      __builtin_rvtt_sfpnop ();
    }

  auto x = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (0, x, 0, 0, 0, 0, 0);
}
/*
**_Z5threePVj:
**	lw	a5,0\(a0\)
**	beq	a5,zero,.L9
**	TTREPLAY	2052 >> 10, 2052 & 0x3ff, 0, 0, 0, 1
**	SFPNOP
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPMUL	L0, L0, L0, 0
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/
