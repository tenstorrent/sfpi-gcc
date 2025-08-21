// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
extern unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void bad()
{
  vUInt i = l_reg[LRegs::LReg0];
  v_if (i > 0) {
  } v_endif;
}
// { dg-final { scan-assembler {\n_Z3badv:\n\tSFPSETCC	L0, 0, 4\n\tSFPSETCC	L0, 0, 2\n\tSFPENCC	3, 10\n\tret\n} } }
