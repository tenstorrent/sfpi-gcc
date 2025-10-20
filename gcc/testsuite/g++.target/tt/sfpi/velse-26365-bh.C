// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

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
/*
**_Z4bug1v:
**	SFPLOADI	L0, 0, 0
**	SFPMAD	L1, L10, L11, L3, 0
**	SFPNOP\n\tSFPSETCC	L1, 0, 0
**	SFPLOADI	L0, 16256, 0
**	SFPCOMPC
**	SFPPUSHC	0
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L1, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L3, 0, 4
**	SFPSETCC	L3, 0, 2
**	SFPCOMPC
**	SFPLOADI	L0, 16384, 0
**	SFPMOV	L3, L0, 2
**	SFPPOPC	0
**	SFPENCC	3, 10
**	ret
*/

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
/*
**_Z4bug2v:
**	SFPLOADI	L0, 0, 0
**	SFPMAD	L1, L10, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPLOADI	L0, 16256, 0
**	SFPCOMPC
**	SFPPUSHC	0
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L1, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L3, 0, 4
**	SFPSETCC	L3, 0, 2
**	SFPCOMPC
**	SFPLOADI	L0, 16384, 0
**	SFPMOV	L3, L0, 2
**	SFPPOPC	0
**	SFPENCC	3, 10
**	ret
*/

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
/*
**_Z5good1v:
**	SFPLOADI	L0, 0, 0
**	SFPMAD	L1, L10, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPLOADI	L0, 16256, 0
**	SFPCOMPC
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L1, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L3, 0, 0
**	SFPLOADI	L0, 16384, 0
**	SFPMOV	L3, L0, 2
**	SFPENCC	3, 10
**	ret
*/

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
/*
**_Z5good2v:
**	SFPLOADI	L0, 0, 0
**	SFPMAD	L1, L10, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPLOADI	L0, 16256, 0
**	SFPCOMPC
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L1, L11, L3, 0
**	SFPNOP
**	SFPSETCC	L3, 0, 0
**	SFPLOADI	L0, 16384, 0
**	SFPMOV	L3, L0, 2
**	SFPENCC	3, 10
**	ret
*/
