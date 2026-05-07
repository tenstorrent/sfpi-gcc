// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
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
**	SFPLOAD	L0, 0, 0, 7
**	SFPSTOCHRND	L1, L0, L0, 0, 0, 1
**	SFPSTORE	L1, 2, 0, 7
**	SFPSTOCHRND	L1, L0, L0, 0, 1, 1
**	SFPSTORE	L1, 4, 0, 7
**	SFPSTOCHRND	L1, L0, L0, 0, 7, 1
**	SFPSTORE	L1, 6, 8, 7
**	SFPSTOCHRND	L0, L0, L0, 0, 6, 1
**	SFPSTORE	L0, 8, 6, 7
**	ret
*/

void two () {
  vInt a = dst_reg[0];

  dst_reg[3] = convert<vSMag> (a);
}
/*
**_Z3twov:
**	SFPLOAD	L0, 0, 4, 7
**	SFPCAST	L0, L0, 2
**	SFPLOADI	L1, 0, 2
**	SFPLOADI	L1, 2048, 8	# LV:L1
**	SFPIADD	L1, L0, 0, 6
**	SFPSETCC	L1, 0, 6
**	SFPLOADI	L0, 65535, 4	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 6, 4, 7
**	ret
*/

void three () {
  vSMag a = dst_reg[0];

  dst_reg[3] = reinterpret<vUInt> (convert<vInt> (a));
  dst_reg[4] = convert<vFloat> (a);
}
/*
**_Z5threev:
**	SFPLOAD	L1, 0, 4, 7
**	SFPCAST	L2, L1, 3
**	SFPLOADI	L0, 0, 2
**	SFPLOADI	L0, 32768, 8	# LV:L0
**	SFPIADD	L0, L2, 0, 6
**	SFPSETCC	L0, 0, 6
**	SFPMOV	L2, L9, 0	# LV:L2
**	SFPENCC	3, 10
**	SFPSTORE	L2, 6, 4, 7
**	SFPCAST	L1, L1, 1
**	SFPSTORE	L1, 8, 0, 7
**	ret
*/
