// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
  extern volatile unsigned instrn_buffer[];
}
#include <sfpi.h>

using namespace sfpi;

void bug1() {
  vFloat val = l_reg[LRegs::LReg3];
  vFloat result = 0.0f;

  v_if(val < 1.0f) {
    result = 1.0f;
  }
  v_elseif(val <= 2.0f) {
    result = 2.0f;
  }
  v_endif;

  l_reg[LRegs::LReg3] = result;
}
// { dg-final { scan-assembler {\n_Z4bug1v:\n\tSFPLOADI	L0, 0, 0\n\tSFPMAD	L1, L10, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16256, 0\n\tSFPCOMPC\n\tSFPPUSHC	0\n\tSFPLOADI	L1, 16384, 0\n\tSFPMAD	L3, L1, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L3, 0, 4\n\tSFPSETCC	L3, 0, 2\n\tSFPCOMPC\n\tSFPLOADI	L0, 16384, 0\n\tSFPPOPC	0\n\tSFPENCC	3, 10\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void bug2() {
  vFloat val = l_reg[LRegs::LReg3];
  vFloat result = 0.0f;

  v_if(val < 1.0f) {
    result = 1.0f;
  }
  v_elseif(!(val > 2.0f)) {
    result = 2.0f;
  }
  v_endif;

  l_reg[LRegs::LReg3] = result;
}
// { dg-final { scan-assembler {\n_Z4bug2v:\n\tSFPLOADI	L0, 0, 0\n\tSFPMAD	L1, L10, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16256, 0\n\tSFPCOMPC\n\tSFPPUSHC	0\n\tSFPLOADI	L1, 16384, 0\n\tSFPMAD	L3, L1, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L3, 0, 4\n\tSFPSETCC	L3, 0, 2\n\tSFPCOMPC\n\tSFPLOADI	L0, 16384, 0\n\tSFPPOPC	0\n\tSFPENCC	3, 10\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void good1() {
  vFloat val = l_reg[LRegs::LReg3];
  vFloat result = 0.0f;

  v_if(val < 1.0f) {
    result = 1.0f;
  }
  v_elseif(val < 2.0f) {
    result = 2.0f;
  }
  v_endif;

  l_reg[LRegs::LReg3] = result;
}
// { dg-final { scan-assembler {\n_Z5good1v:\n\tSFPLOADI	L0, 0, 0\n\tSFPMAD	L1, L10, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16256, 0\n\tSFPCOMPC\n\tSFPLOADI	L1, 16384, 0\n\tSFPMAD	L1, L1, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16384, 0\n\tSFPENCC	3, 10\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void good2() {
  vFloat val = l_reg[LRegs::LReg3];
  vFloat result = 0.0f;

  v_if(val < 1.0f) {
    result = 1.0f;
  }
  v_elseif(!(val >= 2.0f)) {
    result = 2.0f;
  }
  v_endif;

  l_reg[LRegs::LReg3] = result;
}
// { dg-final { scan-assembler {\n_Z5good2v:\n\tSFPLOADI	L0, 0, 0\n\tSFPMAD	L1, L10, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16256, 0\n\tSFPCOMPC\n\tSFPLOADI	L1, 16384, 0\n\tSFPMAD	L1, L1, L11, L3, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPLOADI	L0, 16384, 0\n\tSFPENCC	3, 10\n\tSFPMOV	L3, L0, 2\n\tret\n} } }
