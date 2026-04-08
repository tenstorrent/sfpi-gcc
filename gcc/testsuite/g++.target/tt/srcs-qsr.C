// { dg-options "-mcpu=tt-qsr32-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

void foo() {
  auto v0 = __builtin_rvtt_sfpreadlreg(0);

  __builtin_rvtt_sfpstoresrcs (nullptr, v0, 0, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstoresrcs (nullptr, v0, 0, 0, 0, 0, 0, 1);
}
/* 
**_Z3foov:
**	# READ L0
**	SFPSTORE	L0, 0, 0, 0, 1, 0
**	SFPSTORE	L0, 0, 0, 0, 1, 1
**	ret
*/