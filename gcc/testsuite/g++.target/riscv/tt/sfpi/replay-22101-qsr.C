// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#include <lltt.h>

void record () {
  lltt::record(16, 4);
}
/*
**_Z6recordv:
**	TTREPLAY	16388 >> 10, 16388 & 0x3ff, 0, 0, 0, 1
**	ret
*/

std::uint32_t replay () {
  lltt::replay(16, 4);
  return lltt::replay_insn(20, 4);
}
/*
**_Z6replayv:
**	TTREPLAY	16388 >> 10, 16388 & 0x3ff, 0, 0, 0, 0
**	li	a0,67436544
**	addi	a0,a0,64
**	ret
*/
