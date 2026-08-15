extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
typed_invariant_loadi ()
{
  vFloat x = l_reg[LRegs::LReg0];
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      vFloat a = vFloat (0.19833094f);
      vFloat b = vFloat (-0.00447352f);
      x = x * a + b;
    }
  l_reg[LRegs::LReg0] = x;
}
