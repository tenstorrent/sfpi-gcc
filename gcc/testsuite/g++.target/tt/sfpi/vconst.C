// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void i () {
  vConstIntPrgm0 = -31;
}
void fp () {
  vConstFloatPrgm0 = 2.5f;

  vFloat inp = 0;
  vFloat den = vConst1 - inp;
  dst_reg[0] = vConst1;
}
