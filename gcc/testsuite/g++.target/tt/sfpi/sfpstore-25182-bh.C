// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

#include <cstdint>
namespace ckernel{
  extern volatile uint32_t instrn_buffer[];
}
#include <sfpi.h>

void frob () {
  sfpi::vFloat tmp = sfpi::vConstFloatPrgm0;
  sfpi::dst_reg[0] = tmp;
}
// { dg-final { scan-assembler {\n_Z4frobv:\n\tSFPMOV	L0, L12, 2\n\tSFPSTORE	0, L0, 0, 7\n\tret\n} } }

void frob (int i) {
  sfpi::vFloat tmp = sfpi::vConstFloatPrgm0;
  sfpi::dst_reg[i] = tmp;
}
// { dg-final { scan-assembler {\n_Z4frobi:\n\tli	a5,8192\n\taddi	a5,a5,-1\n\tslli	a0,a0,1\n\tand	a0,a0,a5\n\tli	a5, 1912659968	# 2:7200e000\n\tadd	a0,a0,a5\n\tSFPMOV	L0, L12, 2\n\tlui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a0, %lo\(_ZN7ckernel13instrn_bufferE\)\(a5\)	# 2:7200e000 L0\n\tret\n} } }
