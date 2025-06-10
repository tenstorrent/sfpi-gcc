// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel {
  extern unsigned instrn_buffer[];
}
#include <lltt.h>

void foo (uint32_t v) {
  lltt::insn(0x12345678);
  lltt::insn(v);
}

// { dg-final { scan-assembler {\n_Z3foom:\n\t.ttinsn	305419896\n\tlui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a0,%lo\(_ZN7ckernel13instrn_bufferE\)\(a5\)\n\tret\n} } }
