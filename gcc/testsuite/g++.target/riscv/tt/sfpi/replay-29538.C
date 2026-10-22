// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#include <lltt.h>

void record (unsigned length) {
  lltt::record(16, length);
  lltt::record<lltt::Exec>(20, length);
}
/*
**_Z6recordj:
**	li	a5,16777216
**	addi	a5,a5,-16
**	slli	a0,a0,4
**	and	a0,a0,a5
**	li	a5,262144
**	or	a5,a0,a5
**	li	a3, 67108865	# 1:4000001
**	lui	a4,%hi\(__instrn_buffer\)
**	add	a5,a5,a3
**	addi	a4,a4,%lo\(__instrn_buffer\)
**	sw	a5, 0\(a4\)	# 1:TTREPLAY	a5 >> 10, a5 & 0x3ff, 0, 1
**	li	a5,327680
**	or	a0,a0,a5
**	li	a5, 67108867	# 2:4000003
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:TTREPLAY	a0 >> 10, a0 & 0x3ff, 1, 1
**	ret
*/

std::uint32_t replay (unsigned length) {
  lltt::replay(16, length);
  return lltt::replay_insn(20, length);
}
/*
**_Z6replayj:
**	li	a5,16777216
**	slli	a0,a0,4
**	addi	a5,a5,-16
**	and	a5,a0,a5
**	li	a4,262144
**	or	a5,a5,a4
**	li	a4, 67108864	# 1:4000000
**	add	a5,a5,a4
**	lui	a4,%hi\(__instrn_buffer\)
**	sw	a5, %lo\(__instrn_buffer\)\(a4\)	# 1:TTREPLAY	a5 >> 10, a5 & 0x3ff, 0, 0
**	li	a5,67436544
**	or	a0,a0,a5
**	ret
*/
