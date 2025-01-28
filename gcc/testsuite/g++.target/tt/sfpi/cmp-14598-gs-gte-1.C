// { dg-options "-mcpu=tt-gs -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

// gs defaults to the incorrect code sequence
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
    
    v_if(a >= b) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z5fnuvvv:\n\tSFPIADD	L1, L0, 0, 10\n\tTTNOP\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPPUSHC\n\tSFPENCC	3, 2\n\tTTNOP\n\tSFPMOV	L0, L2, 0\n\tTTNOP\n\tTTNOP\n\tSFPPOPC\n\tTTNOP\n\tSFPMOV	L0, L3, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }

void fnuvi (int i) {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a >= i) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z5fnuvii:\n\tslli	a5,a0,16\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 1897005056	# 71120000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a5,a5,16\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# Op\(0x71\) - d\(lr1\)\n\tSFPIADD	L1, L0, 0, 10\n\tTTNOP\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPPUSHC\n\tSFPENCC	3, 2\n\tTTNOP\n\tSFPMOV	L0, L2, 0\n\tTTNOP\n\tTTNOP\n\tSFPPOPC\n\tTTNOP\n\tSFPMOV	L0, L3, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }

void fnuvc () {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a >= 5) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z5fnuvcv:\n\tSFPIADD	L0, L0, -5, 9\n\tTTNOP\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPPUSHC\n\tSFPENCC	3, 2\n\tTTNOP\n\tSFPMOV	L0, L2, 0\n\tTTNOP\n\tTTNOP\n\tSFPPOPC\n\tTTNOP\n\tSFPMOV	L0, L3, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }

void fns () {
    vInt a = l_reg[LRegs::LReg0];
    vInt b = l_reg[LRegs::LReg1];
    vFloat c = l_reg[LRegs::LReg2];
    vFloat d = l_reg[LRegs::LReg3];
    vFloat r;
    
    v_if(a >= b) {
      r = c;
    } v_else {
      r = d;
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z3fnsv:\n\tSFPIADD	L1, L0, 0, 10\n\tTTNOP\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPPUSHC\n\tSFPENCC	3, 2\n\tTTNOP\n\tSFPMOV	L0, L2, 0\n\tTTNOP\n\tTTNOP\n\tSFPPOPC\n\tTTNOP\n\tSFPMOV	L0, L3, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }
