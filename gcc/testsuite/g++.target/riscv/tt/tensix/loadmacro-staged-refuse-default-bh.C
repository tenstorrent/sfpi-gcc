// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSHFT" 6 } }
// { dg-final { scan-assembler-times "SFPCAST" 6 } }
// { dg-final { check-function-bodies "**" "" } }

/* This file compiles the same refused functions without the opt-in.  The
   exact bodies below intentionally match loadmacro-staged-refuse-bh.C.  */

#include "loadmacro-staged-refuse-bh.C"

/*
**_Z10unknown_ccv:
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

/*
**_Z18dst_counter_updatev:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 1
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 1
**	ret
*/

/*
**_Z11odd_dst_rowv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 1, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 1, 0, 7
**	ret
*/

/*
**_Z19cast_value_live_outv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	SFPSTORE	L0, 2, 0, 7
**	ret
*/

/*
**_Z19opaque_config_ownerv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

/*
**_Z13lreg_pressurev:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPLOAD	L1, 2, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	SFPSTORE	L1, 2, 0, 7
**	ret
*/
