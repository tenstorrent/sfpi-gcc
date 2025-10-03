// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

template<unsigned ADDR>
void load_imm () {
    vFloat r = __builtin_rvtt_sfpload (7, 3, ADDR);
    l_reg[LRegs::LReg3] = r;
}
template void load_imm<0> ();
/*
**_Z8load_immILj0EEvv:
**	SFPLOAD	L3, 0, 7, 3
**	ret
*/

template void load_imm<0x3fff> ();
/*
**_Z8load_immILj16383EEvv:
**	SFPLOAD	L3, 16383, 7, 3
**	ret
*/

void load_var (unsigned addr) {
    vFloat r = __builtin_rvtt_sfpload (7, 3, addr);
    l_reg[LRegs::LReg3] = r;
}
/*
**_Z8load_varj:
**	slli	a0,a0,18
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 1882701824	# 2:7037c000
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	srli	a0,a0,18
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7037c000 L3 :=
**	ret
*/

template<unsigned ADDR>
void store_imm () {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 7, 3, ADDR);
}
template void store_imm<0> ();
/*
**_Z9store_immILj0EEvv:
**	SFPSTORE	0, L3, 7, 3
**	ret
*/

template void store_imm<0x3fff> ();
/*
**_Z9store_immILj16383EEvv:
**	SFPSTORE	16383, L3, 7, 3
**	ret
*/

void store_var (unsigned addr) {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 7, 3, addr);
}
/*
**_Z9store_varj:
**	slli	a0,a0,18
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 1916256256	# 2:7237c000
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	srli	a0,a0,18
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7237c000 L3
**	ret
*/
