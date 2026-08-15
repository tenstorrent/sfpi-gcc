// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "SFPMUL24" 4 } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

namespace ckernel {
unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

// Baseline for D4 formation.  This is the exact typed radix-23 identity used
// by TT-Metal's MulInt32 A/B, with constant rows to keep this target regression
// independent of the LLK runtime.  Once load-macro formation is legal and
// enabled, this test should move to the default-off option and assert the
// formed descriptor/output instead of the current four explicit multiplies.
void mul_int32_rows() {
#pragma GCC unroll 8
  for (int d = 0; d < 8; ++d) {
    vUInt a = dst_reg[0].mode<DataLayout::U32>();
    vUInt b = dst_reg[32].mode<DataLayout::U32>();
    vUInt a_hi = a >> 23;
    vUInt b_hi = b >> 23;
    vUInt lo = fractional_mul(a, b, FractionalHalf::Low);
    vUInt hi = fractional_mul(a, b, FractionalHalf::High);
    hi += fractional_mul(a_hi, b, FractionalHalf::Low);
    hi += fractional_mul(a, b_hi, FractionalHalf::Low);
    vUInt result = lo + (hi << 23);
    dst_reg[64].mode<DataLayout::U32>() = result;
    dst_reg++;
  }
}
