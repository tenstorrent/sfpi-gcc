// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#include <lltt.h>

void foo (uint32_t v) {
  lltt::insn(0x12345678);
  lltt::insn(v);
}
/*
**_Z3foom:
**	.ttinsn	305419896
**	lui	a5,%hi\(__instrn_buffer\)
**	sw	a0,%lo\(__instrn_buffer\)\(a5\)
**	ret
*/

void baz (uint32_t v) {
  (__builtin_rvtt_ttinsn)(lltt::__instrn_buffer, true, v); // { dg-warning "ttinsn is not statically known" }
}
