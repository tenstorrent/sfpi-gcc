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
**	# READ L3
**	SFPMAD	L0, L10, L10, L3, 1
**	SFPSETCC	L0, 0, 0
**	SFPMOV	L1, L9, 2
**	SFPMOV	L1, L10, 0	# LV:L1
**	SFPCOMPC
**	SFPLOADI	L0, 16384, 0
**	SFPMAD	L0, L10, L3, L0, 1
**	SFPSETCC	L0, 0, 4
**	SFPLOADI	L1, 16384, 0	# LV:L1
**	SFPMOV	L3, L1, 2
**	SFPENCC	3, 10
**	# WRITE L3
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
**	# READ L3
**	SFPMAD	L0, L10, L10, L3, 1
**	SFPSETCC	L0, 0, 0
**	SFPMOV	L1, L9, 2
**	SFPMOV	L1, L10, 0	# LV:L1
**	SFPCOMPC
**	SFPLOADI	L0, 16384, 0
**	SFPMAD	L0, L10, L3, L0, 1
**	SFPSETCC	L0, 0, 4
**	SFPLOADI	L1, 16384, 0	# LV:L1
**	SFPMOV	L3, L1, 2
**	SFPENCC	3, 10
**	# WRITE L3
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
**	# READ L3
**	SFPMAD	L0, L10, L10, L3, 1
**	SFPSETCC	L0, 0, 0
**	SFPMOV	L0, L9, 2
**	SFPMOV	L0, L10, 0	# LV:L0
**	SFPCOMPC
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L10, L1, L3, 1
**	SFPSETCC	L3, 0, 0
**	SFPLOADI	L0, 16384, 0	# LV:L0
**	SFPMOV	L3, L0, 2
**	SFPENCC	3, 10
**	# WRITE L3
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
**	# READ L3
**	SFPMAD	L0, L10, L10, L3, 1
**	SFPSETCC	L0, 0, 0
**	SFPMOV	L0, L9, 2
**	SFPMOV	L0, L10, 0	# LV:L0
**	SFPCOMPC
**	SFPLOADI	L1, 16384, 0
**	SFPMAD	L3, L10, L1, L3, 1
**	SFPSETCC	L3, 0, 0
**	SFPLOADI	L0, 16384, 0	# LV:L0
**	SFPMOV	L3, L0, 2
**	SFPENCC	3, 10
**	# WRITE L3
**	ret
*/
