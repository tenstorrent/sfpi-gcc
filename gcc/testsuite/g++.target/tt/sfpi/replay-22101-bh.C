// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void foo () {
  __builtin_rvtt_sfprecord(16, 4, false);
  __builtin_rvtt_sfprecord(20, 4, true);
}
// { dg-final { scan-assembler {\n_Z3foov:\n\tTTREPLAY	16, 4, 0, 1\n\tTTREPLAY	20, 4, 1, 1\n\tret\n} } }
