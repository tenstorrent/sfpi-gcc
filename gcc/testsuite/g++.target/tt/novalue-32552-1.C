// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }


using __v64sf_t [[gnu::vector_size(64 * sizeof(float))]] = float;

void foo () {
  auto x =  __builtin_rvtt_sfpnovalue();
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, f, 0);
  x = __builtin_rvtt_bh_sfpmov_lv (x, y, 0);
}
/*
**_Z3foov:
**	;no value L1
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, L9, 0
**	SFPNOP
**	SFPMOV	L1, L0, 0
**	ret
*/

void bar (bool opt) {
  if (opt) {
    auto x1 =  __builtin_rvtt_sfpnovalue();
    auto f1 = __builtin_rvtt_bh_sfpload (nullptr, 1, 0, 0, 0, 0);
    auto y1 = __builtin_rvtt_bh_sfpmul (f1, f1, 0);
    x1 = __builtin_rvtt_bh_sfpmov_lv (x1, y1, 0);
  } else {
    auto x2 =  __builtin_rvtt_sfpnovalue();
    auto f2 = __builtin_rvtt_bh_sfpload (nullptr, 2, 0, 0, 0, 0);
    auto y2 = __builtin_rvtt_bh_sfpmul (f2, f2, 0);
    x2 = __builtin_rvtt_bh_sfpmov_lv (x2, y2, 0);
  }
}
/*
**_Z3barb:
**	;no value L0
**	beq	a0,zero,.L4
**	SFPLOAD	L1, 0, 1, 0
**	SFPMUL	L1, L1, L1, L9, 0
**	SFPNOP
**	SFPMOV	L0, L1, 0
**	ret
**	SFPLOAD	L1, 0, 2, 0
**	SFPMUL	L1, L1, L1, L9, 0
**	SFPNOP
**	SFPMOV	L0, L1, 0
**	ret
*/
