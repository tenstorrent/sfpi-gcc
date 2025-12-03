// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void fnuvv () {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a == b) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
/*
**_Z5fnuvvv:
**	SFPIADD	L1, L0, 0, 6
**	SFPSETCC	L1, 0, 6
**	SFPCOMPC
**	SFPMOV	L2, L3, 0
**	SFPENCC	3, 10
**	SFPMOV	L0, L2, 2
**	ret
*/

void fnuvi (int i) {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a == i) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
/*
**_Z5fnuvii:
**	lui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)
**	zext.h	a4,a0
**	li	a5, 1897529344	# 2:711a0000
**	lw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)
**	add	a4,a4,a5
**	li	a5, 1897398272	# 3:71180000
**	sw	a4, 0\(a3\)	# 2:711a0000 L1 :=
**	srli	a0,a0,16
**	add	a0,a0,a5
**	sw	a0, 0\(a3\)	# 3:71180000 L1 := LV
**	SFPIADD	L1, L0, 0, 6
**	SFPSETCC	L1, 0, 6
**	SFPCOMPC
**	SFPMOV	L2, L3, 0
**	SFPENCC	3, 10
**	SFPMOV	L0, L2, 2
**	ret
*/

void fnuvc () {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a == 5) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
/*
**_Z5fnuvcv:
**	SFPIADD	L0, L0, -5, 5
**	SFPSETCC	L0, 0, 6
**	SFPCOMPC
**	SFPMOV	L2, L3, 0
**	SFPENCC	3, 10
**	SFPMOV	L0, L2, 2
**	ret
*/

void fns () {
    vInt a = l_reg[LRegs::LReg0];
    vInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a == b) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
/*
**_Z3fnsv:
**	SFPIADD	L1, L0, 0, 6
**	SFPSETCC	L1, 0, 6
**	SFPCOMPC
**	SFPMOV	L2, L3, 0
**	SFPENCC	3, 10
**	SFPMOV	L0, L2, 2
**	ret
*/

