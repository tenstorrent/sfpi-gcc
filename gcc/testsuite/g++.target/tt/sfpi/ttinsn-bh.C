// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

#define TT_OP(opcode, params) ((opcode << 24) + params)
#define TT_OP_REPLAY(start_idx, len, execute_while_loading, load_mode) \
    TT_OP(0x04, (((start_idx) << 14) + ((len) << 4) + ((execute_while_loading) << 1) + ((load_mode) << 0)))

#include <lltt.h>

void foo (uint32_t v) {
  lltt::insn(0x12345678);
  lltt::insn(v);
}

// { dg-final { scan-assembler {\n_Z3foom:\n\t.ttinsn	305419896\n\tlui	a5,%hi\(__instrn_buffer\)\n\tsw	a0,%lo\(__instrn_buffer\)\(a5\)\n\tret\n} } }
