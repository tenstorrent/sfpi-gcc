// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-shrink-wrap" }
// { dg-final { check-function-bodies "**" "" } }

namespace ands {

void foo () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpand (a, b);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN4ands3fooEv:
** 	# READ L1
**	# READ L2
** 	SFPAND	L3, L1, L2, 1
**	# WRITE L3
**	ret
*/

void bar () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto c = __builtin_rvtt_sfpreadlreg (4);
  __builtin_rvtt_sfppushc (0);
  auto i = __builtin_rvtt_sfpxvif ();
  auto cond = __builtin_rvtt_sfpxfcmpv (a, b, 2);
  __builtin_rvtt_sfpxcondb (cond, i);
  auto r = __builtin_rvtt_sfpand (a, b);
  r = __builtin_rvtt_sfpassign_lv (c, r);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN4ands3barEv:
**	# READ L1
**	# READ L2
**	# READ L4
**	SFPMAD	L0, L2, L11, L1, 0
**	SFPNOP
**	SFPSETCC	L0, 0, 6
**	SFPMOV	L3, L4, 2
**	SFPAND	L3, L1, L2, 1	# LV:L3
**	SFPENCC	3, 10
**	# WRITE L3
**	ret
*/
}

namespace ors {

void foo () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpor (a, b);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN3ors3fooEv:
**	# READ L1
**	# READ L2
** 	SFPOR	L3, L1, L2, 1
**	# WRITE L3
**	ret
*/

void bar () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto c = __builtin_rvtt_sfpreadlreg (4);
  __builtin_rvtt_sfppushc (0);
  auto i = __builtin_rvtt_sfpxvif ();
  auto cond = __builtin_rvtt_sfpxfcmpv (a, b, 2);
  __builtin_rvtt_sfpxcondb (cond, i);
  auto r = __builtin_rvtt_sfpor (a, b);
  r = __builtin_rvtt_sfpassign_lv (c, r);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN3ors3barEv:
**	# READ L1
**	# READ L2
**	# READ L4
**	SFPMAD	L0, L2, L11, L1, 0
**	SFPNOP
**	SFPSETCC	L0, 0, 6
**	SFPMOV	L3, L4, 2
**	SFPOR	L3, L1, L2, 1	# LV:L3
**	SFPENCC	3, 10
**	# WRITE L3
**	ret
*/
}

namespace xors {

void foo () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpxor (a, b);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN4xors3fooEv:
**	# READ L1
**	# READ L2
** 	SFPXOR	L1, L2
**	SFPMOV	L3, L1, 2
**	# WRITE L3
**	ret
*/

void bar () {
  auto a = __builtin_rvtt_sfpreadlreg (1);
  auto b = __builtin_rvtt_sfpreadlreg (2);
  auto c = __builtin_rvtt_sfpreadlreg (4);
  __builtin_rvtt_sfppushc (0);
  auto i = __builtin_rvtt_sfpxvif ();
  auto cond = __builtin_rvtt_sfpxfcmpv (a, b, 2);
  __builtin_rvtt_sfpxcondb (cond, i);
  auto r = __builtin_rvtt_sfpxor (a, b);
  r = __builtin_rvtt_sfpassign_lv (c, r);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_ZN4xors3barEv:
**	# READ L1
**	# READ L2
**	# READ L4
**	SFPMAD	L0, L2, L11, L1, 0
**	SFPNOP
**	SFPSETCC	L0, 0, 6
**	SFPXOR	L1, L2
**	SFPMOV	L3, L4, 2
**	SFPMOV	L3, L1, 0	# LV:L3
**	SFPENCC	3, 10
**	# WRITE L3
**	ret
*/
}
