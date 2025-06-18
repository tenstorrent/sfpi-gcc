// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

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
// { dg-final { scan-assembler {\n_Z5fnuvvv:\n\tSFPIADD	L1, L0, 0, 6\n\tSFPSETCC	L1, 0, 6\n\tSFPCOMPC\n\tSFPMOV	L0, L2, 2\n\tSFPMOV	L0, L3, 0\n\tSFPENCC	3, 10\n\tret\n} } }

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
// { dg-final { scan-assembler {\n_Z5fnuvii:\n\tslli	a5,a0,16\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 1897529344	# 2:711a0000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a5,a5,16\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# 2:711a0000 L1 :=\n\tli	a5, 1897398272	# 3:71180000\n\tsrli	a0,a0,16\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 3:71180000 L1 := LV\n\tSFPIADD	L1, L0, 0, 6\n\tSFPSETCC	L1, 0, 6\n\tSFPCOMPC\n\tSFPMOV	L0, L2, 2\n\tSFPMOV	L0, L3, 0\n\tSFPENCC	3, 10\n\tret\n} } }

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
// { dg-final { scan-assembler {\n_Z5fnuvcv:\n\tSFPIADD	L0, L0, -5, 5\n\tSFPSETCC	L0, 0, 6\n\tSFPCOMPC\n\tSFPMOV	L0, L2, 2\n\tSFPMOV	L0, L3, 0\n\tSFPENCC	3, 10\n\tret\n} } }

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
// { dg-final { scan-assembler {\n_Z3fnsv:\n\tSFPIADD	L1, L0, 0, 6\n\tSFPSETCC	L1, 0, 6\n\tSFPCOMPC\n\tSFPMOV	L0, L2, 2\n\tSFPMOV	L0, L3, 0\n\tSFPENCC	3, 10\n\tret\n} } }

