// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

template<unsigned ADDR>
void load_imm () {
    vFloat r = __builtin_rvtt_sfpload (3, 7, ADDR);
    l_reg[LRegs::LReg3] = r;
}
template void load_imm<0> ();
/*
**_Z8load_immILj0EEvv:
**	SFPLOAD	L3, 0, 3, 7
**	ret
*/

template void load_imm<0x1fff> ();
/*
**_Z8load_immILj8191EEvv:
**	SFPLOAD	L3, 8191, 3, 7
**	ret
*/

void load_var (unsigned addr) {
    vFloat r = __builtin_rvtt_sfpload (3, 7, addr);
    l_reg[LRegs::LReg3] = r;
}
/*
**_Z8load_varj:
**	slli	a0,a0,19
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 1882447872	# 2:7033e000
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	srli	a0,a0,19
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7033e000 L3 :=
**	ret
*/

template<unsigned ADDR>
void store_imm () {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 3, 7, ADDR);
}
template void store_imm<0> ();
/*
**_Z9store_immILj0EEvv:
**	SFPSTORE	0, L3, 3, 7
**	ret
*/

template void store_imm<0x1fff> ();
/*
**_Z9store_immILj8191EEvv:
**	SFPSTORE	8191, L3, 3, 7
**	ret
*/

void store_var (unsigned addr) {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 3, 7, addr);
}
/*
**_Z9store_varj:
**	slli	a0,a0,19
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 1916002304	# 2:7233e000
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	srli	a0,a0,19
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7233e000 L3
**	ret
*/
