// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#include <lltt.h>

/*
**_Z6recordj:
**	slli	a0,a0,4
**	li	a5, 67371009	# 2:4040001
**	lui	a4,%hi\(__instrn_buffer\)
**	add	a5,a0,a5
**	addi	a4,a4,%lo\(__instrn_buffer\)
**	sw	a5, 0\(a4\)	# 2:4040001
**	li	a5, 67436547	# 4:4050003
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 4:4050003
**	ret
*/
void record (unsigned length) {
  lltt::record(16, length);
  lltt::record<lltt::Exec>(20, length);
}

/*
**_Z6replayj:
**	slli	a0,a0,4
**	li	a5, 67371008	# 2:4040000
**	add	a5,a0,a5
**	lui	a4,%hi\(__instrn_buffer\)
**	sw	a5, %lo\(__instrn_buffer\)\(a4\)	# 2:4040000
**	li	a5,67436544
**	or	a0,a0,a5
**	ret
*/
std::uint32_t replay (unsigned length) {
  lltt::replay(16, length);
  return lltt::replay_insn(20, length);
}
