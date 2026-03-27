// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -Wunused-parameter" }

namespace ckernel{
    extern unsigned instrn_buffer[];
}
#include <sfpi.h>

// Nothing defined
// { dg-final { scan-assembler-not {\n_ZN4sfpi[0-9][_0-9a-zA-Z]*:} } }
