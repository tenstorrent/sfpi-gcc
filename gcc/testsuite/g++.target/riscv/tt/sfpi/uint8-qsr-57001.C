// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void one () {
  { vInt a = dst_reg[0].mode<DataLayout::U8> ();
    dst_reg[1].mode<DataLayout::I32> () = a; }
  { vInt a = dst_reg[0].mode<DataLayout::SM8> ();
    dst_reg[1].mode<DataLayout::SM8> () = a; }
}
/*
**_Z3onev:
**	SFPLOAD	L0, 0, 11, 7, 0, 0
**	SFPSTORE	L0, 2, 4, 7, 0, 0
**	SFPLOAD	L0, 0, 5, 7, 0, 0
**	SFPSTORE	L0, 2, 5, 7, 0, 0
**	ret
*/

void two () {
  { vUInt a = dst_reg[0].mode<DataLayout::U8> ();
    dst_reg[1].mode<DataLayout::U8> () = a; }
  { vSMag a = dst_reg[0].mode<DataLayout::U8> ();
    dst_reg[1].mode<DataLayout::SM32> () = a; }
  { vSMag a = dst_reg[0].mode<DataLayout::SM8> ();
    dst_reg[1].mode<DataLayout::SM8> () = a; }
}
/*
**_Z3twov:
**	SFPLOAD	L0, 0, 11, 7, 0, 0
**	SFPSTORE	L0, 2, 11, 7, 0, 0
**	SFPLOAD	L0, 0, 11, 7, 0, 0
**	SFPSTORE	L0, 2, 4, 7, 0, 0
**	SFPLOAD	L0, 0, 5, 7, 0, 0
**	SFPSTORE	L0, 2, 5, 7, 0, 0
**	ret
*/
