// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void frob () {
  vConstIntPrgm0 = -31;

  vFloat inp = 0;
  vFloat den = vConst1 - inp;
}
