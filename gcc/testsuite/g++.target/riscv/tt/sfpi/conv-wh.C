// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void one () {
  vFloat a = dst_reg[0];

  dst_reg[1] = convert<vFloat16a> (a);
  dst_reg[2] = convert<vFloat16b> (a);
  dst_reg[3] = convert<vSMag16> (a);
  dst_reg[4] = convert<vUInt16> (a);
}
/*
**_Z3onev:
**	SFPLOAD	L0, 0, 0, 3
**	SFPSTOCHRND	L1, L0, L0, 0, 0, 1
**	SFPSTORE	L1, 2, 0, 3
**	SFPSTOCHRND	L1, L0, L0, 0, 1, 1
**	SFPSTORE	L1, 4, 0, 3
**	SFPSTOCHRND	L1, L0, L0, 0, 7, 1
**	SFPSTORE	L1, 6, 8, 3
**	SFPSTOCHRND	L0, L0, L0, 0, 6, 1
**	SFPSTORE	L0, 8, 6, 3
**	ret
*/

void two () {
  vInt a = dst_reg[0];

  dst_reg[3] = convert<vSMag> (a);
}
/*
**_Z3twov:
**	SFPLOAD	L0, 0, 12, 3
**	SFPSETCC	L0, 0, 0
**	SFPLOADI	L1, 65535, 2
**	SFPLOADI	L1, 32767, 8	# LV:L1
**	SFPMOV	L2, L0, 2
**	SFPXOR	L2, L1
**	SFPMOV	L0, L2, 0	# LV:L0
**	SFPMOV	L2, L1, 2
**	SFPIADD	L2, L0, 0, 6
**	SFPMOV	L0, L2, 0	# LV:L0
**	SFPSETCC	L0, 0, 6
**	SFPMOV	L0, L1, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 6, 4, 3
**	ret
*/

void three () {
  vSMag a = dst_reg[0];

  dst_reg[3] = reinterpret<vUInt> (convert<vInt> (a));
  dst_reg[4] = convert<vFloat> (a);
}
/*
**_Z5threev:
**	SFPLOAD	L0, 0, 4, 3
**	SFPSETCC	L0, 0, 0
**	SFPLOADI	L1, 65535, 2
**	SFPLOADI	L1, 32767, 8	# LV:L1
**	SFPMOV	L3, L0, 2
**	SFPXOR	L3, L1
**	SFPMOV	L2, L0, 2
**	SFPMOV	L2, L3, 0	# LV:L2
**	SFPIADD	L1, L2, 0, 6
**	SFPMOV	L2, L1, 0	# LV:L2
**	SFPENCC	3, 10
**	SFPSTORE	L2, 6, 4, 3
**	SFPCAST	L0, L0, 1
**	SFPSTORE	L0, 8, 0, 3
**	ret
*/
