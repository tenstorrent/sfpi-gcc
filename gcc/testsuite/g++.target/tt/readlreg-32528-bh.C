// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }


using __v64sf_t [[gnu::vector_size(64 * sizeof(float))]] = float;

namespace cst {
void foo () {
  auto x =  __builtin_rvtt_sfpreadlreg(9);
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, x, 0);
}
/*
**_ZN3cst3fooEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L9, L9, 0
**	ret
*/

void bar (int i) {
  auto x =  __builtin_rvtt_sfpreadlreg(9);
  if (i) {
    auto g = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
    __builtin_rvtt_bh_sfpmul (g, g, 0);
  }
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, x, 0);
}
/*
**_ZN3cst3barEi:
**	beq	a0,zero,.L[0-9]+
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, L9, 0
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L9, L9, 0
**	ret
*/

void baz () {
  auto x =  __builtin_rvtt_sfpreadlreg(9);
  __builtin_rvtt_bh_sfpstore (nullptr, x, 0, 0, 0, 0, 0);
}
/*
**_ZN3cst3bazEv:
**	SFPSTORE	0, L9, 0, 0
**	ret
*/
void sub5() {
  auto a = __builtin_rvtt_sfpassignlreg(0);
  auto b = __builtin_rvtt_sfpassignlreg(1);

  auto neg1 = __builtin_rvtt_sfpreadlreg (11);
  b = __builtin_rvtt_bh_sfpassign_lv (b, __builtin_rvtt_bh_sfpmad (neg1, a, b, 0));
  b = __builtin_rvtt_bh_sfpassign_lv (b, __builtin_rvtt_bh_sfpmad (neg1, a, b, 0));

  __builtin_rvtt_sfppreservelreg (b, 3);
}
/*
**_ZN3cst4sub5Ev:
**	SFPMAD	L1, L11, L0, L1, 0
**	SFPNOP
**	SFPMAD	L3, L11, L0, L1, 0
**	ret
*/

void swap () {
  auto x = __builtin_rvtt_sfpreadlreg(9);
  auto y = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  __builtin_rvtt_bh_sfpswap (x, y, 0);
  auto z = __builtin_rvtt_bh_sfpmul (x, y, 0);
}
/*
**_ZN3cst4swapEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPSWAP	L9, L0, 0
**	SFPNOP
**	SFPMUL	L0, L9, L0, L9, 0
**	ret
*/
}

namespace vol {
void foo () {
  auto x =  __builtin_rvtt_sfpreadlreg(1);
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, x, 0);
}
/*
**_ZN3vol3fooEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L1, L9, 0
**	ret
*/

void bar (int i) {
  auto x =  __builtin_rvtt_sfpreadlreg(1);
  if (i) {
    auto g = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
    __builtin_rvtt_bh_sfpmul (g, g, 0);
  }
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, x, 0);
}
/*
**_ZN3vol3barEi:
**	beq	a0,zero,.L[0-9]+
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, L9, 0
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L1, L9, 0
**	ret
*/
}

namespace lv {
void foo () {
  auto x =  __builtin_rvtt_sfpreadlreg(9);
  auto f = __builtin_rvtt_bh_sfpload_lv (nullptr, x, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul (f, x, 0);
}
/*
**_ZN2lv3fooEv:
**	SFPMOV	L0, L9, 2
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L9, L9, 0
**	ret
*/

void bar (int i) {
  auto x =  __builtin_rvtt_sfpreadlreg(9);
  if (i) {
    auto g = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
    __builtin_rvtt_bh_sfpmul_lv (x, g, g, 0);
  }
  auto f = __builtin_rvtt_bh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_bh_sfpmul_lv (x, f, x, 0);
}
/*
**_ZN2lv3barEi:
**	beq	a0,zero,.L[0-9]+
**	SFPLOAD	L0, 0, 0, 0
**	SFPMOV	L1, L9, 2
**	SFPMUL	L1, L0, L0, L9, 0
**	SFPLOAD	L0, 0, 0, 0
**	SFPMOV	L1, L9, 2
**	SFPMUL	L1, L0, L1, L9, 0
**	ret
*/
}
