// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#define TT_OP(opcode, params) ((opcode << 24) + params)
#define TT_OP_REPLAY(start_idx, len, execute_while_loading, load_mode) \
    TT_OP(0x04, (((start_idx) << 14) + ((len) << 4) + ((execute_while_loading) << 1) + ((load_mode) << 0)))

#include <lltt.h>

void record () {
  lltt::record(16, 4);
  lltt::record<lltt::Exec>(20, 4);
}
/*
**_Z6recordv:
**	TTREPLAY	16, 4, 0, 1
**	TTREPLAY	20, 4, 1, 1
**	ret
*/

std::uint32_t replay () {
  lltt::replay(16, 4);
  return lltt::replay_insn(20, 4);
}
/*
**_Z6replayv:
**	TTREPLAY	16, 4, 0, 0
**	li	a0,67436544
**	addi	a0,a0,64
**	ret
*/
