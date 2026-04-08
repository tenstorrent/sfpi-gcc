// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -Wall -Wextra -Wunused-parameter" }

namespace ckernel{
    extern unsigned instrn_buffer[];
}
#include <sfpi.h>

void foo ()
{
  sfpi::sFloat16b (1.5); // { dg-warning "deprecated" }
  sfpi::sFloat16b (1.5f);
  sfpi::s2vFloat16a (1); // { dg-warning "deprecated" }
  sfpi::s2vFloat16b (1); // { dg-warning "deprecated" }
}
