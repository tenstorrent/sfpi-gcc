// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

extern volatile unsigned iptr[];

namespace dyn {
void one () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmuli (iptr, f, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
}
/*
**_ZN3dyn3oneEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMULI	L0, 0, 0
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void two () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmuli (iptr, f, 0, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN3dyn3twoEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMOV	L1, L0, 0
**	SFPMULI	L1, 0, 0
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void one (unsigned i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmuli (iptr, f, i, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
}
/*
**_ZN3dyn3oneEj:
**	SFPLOAD	L0, 0, 0, 0
**	slli	a0,a0,8
**	li	a5,16777216
**	addi	a5,a5,-256
**	and	a0,a0,a5
**	li	a5, 1946157056	# 2:74000000
**	add	a0,a0,a5
**	lui	a5,%hi\(iptr\)
**	sw	a0, %lo\(iptr\)\(a5\)	# 2:74000000 L0 := LV
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void two (unsigned i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmuli (iptr, f, i, 0, 0, 0);
  __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN3dyn3twoEj:
**	SFPLOAD	L0, 0, 0, 0
**	li	a5, 1946157072	# 2:74000010
**	SFPMOV	L1, L0, 0
**	slli	a0,a0,8
**	li	a4,16777216
**	addi	a4,a4,-256
**	and	a0,a0,a4
**	add	a0,a0,a5
**	lui	a5,%hi\(iptr\)
**	sw	a0, %lo\(iptr\)\(a5\)	# 2:74000010 L1 := LV
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void three () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmuli (iptr, f, 0, 0, 0, 0);
  __builtin_rvtt_sfpwritelreg (y, 3);
  __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN3dyn5threeEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMOV	L3, L0, 0
**	SFPMULI	L3, 0, 0
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

}

namespace stat {
void one () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpshft2_e (f, 3);
  __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN4stat3oneEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPSHFT2	L1, L0, 0, 3
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

}

namespace nop {
void one () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpshft2_e (f, 3);
  __builtin_rvtt_sfpnop ();
  __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN3nop3oneEv:
**	SFPLOAD	L0, 0, 0, 0
**	SFPSHFT2	L1, L0, 0, 3
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void three () {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpshft2_e (f, 3);
  __builtin_rvtt_sfpnop ();
  __builtin_rvtt_sfpwritelreg (f, 3);
}
/*
**_ZN3nop5threeEv:
**	SFPLOAD	L3, 0, 0, 0
**	SFPSHFT2	L0, L3, 0, 3
**	SFPNOP
**	ret
*/

}

namespace block {

void one (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmul (f, f, 0);
  if (i)
    __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
  else
    __builtin_rvtt_wh_sfpstore (nullptr, y, 2, 0, 0, 0, 0);
}
/*
**_ZN5block3oneEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L0, L0, L0, L9, 0
**	SFPNOP
**	beq	a0,zero,.L[0-9]+
**	SFPSTORE	0, L0, 0, 0
**	ret
**	SFPSTORE	0, L0, 2, 0
**	ret
*/

void two (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmul (f, f, 0);
  if (i)
    __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
  else
    __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
}
/*
**_ZN5block3twoEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L1, L0, L0, L9, 0
**	SFPNOP
**	beq	a0,zero,.L[0-9]+
**	SFPSTORE	0, L1, 0, 0
**	ret
**	SFPSTORE	0, L0, 0, 0
**	ret
*/

void three (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmul (f, f, 0);
  if (i)
    __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
  else
    __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
}
/*
**_ZN5block5threeEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L1, L0, L0, L9, 0
**	SFPNOP
**	beq	a0,zero,.L[0-9]+
**	SFPSTORE	0, L0, 0, 0
**	ret
**	SFPSTORE	0, L1, 0, 0
**	ret
*/

void four (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpmul (f, f, 0);
  if (i)
    __builtin_rvtt_wh_sfpstore (nullptr, f, 0, 0, 0, 0, 0);
  else
    __builtin_rvtt_wh_sfpstore (nullptr, f, 2, 0, 0, 0, 0);
}
/*
**_ZN5block4fourEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPMUL	L1, L0, L0, L9, 0
**	beq	a0,zero,.L[0-9]+
**	SFPSTORE	0, L0, 0, 0
**	ret
**	SFPSTORE	0, L0, 2, 0
**	ret
*/

void five (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpshft2_e (f, 3);
  if (i) {
    __builtin_rvtt_sfpnop ();
    __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
  }
  else
    __builtin_rvtt_wh_sfpstore (nullptr, y, 2, 0, 0, 0, 0);
}
/*
**_ZN5block4fiveEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPSHFT2	L0, L0, 0, 3
**	SFPNOP
**	beq	a0,zero,.L[0-9]+
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
**	SFPSTORE	0, L0, 2, 0
**	ret
*/

int six (int i) {
  auto f = __builtin_rvtt_wh_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto y = __builtin_rvtt_wh_sfpshft2_e (f, 3);
  if (i) {
    __builtin_rvtt_sfpnop ();
    __builtin_rvtt_wh_sfpstore (nullptr, y, 0, 0, 0, 0, 0);
  }
  else {
    i++;
    __builtin_rvtt_sfpnop ();
    __builtin_rvtt_wh_sfpstore (nullptr, y, 2, 0, 0, 0, 0);
  }
  return i;
}
/*
**_ZN5block3sixEi:
**	SFPLOAD	L0, 0, 0, 0
**	SFPSHFT2	L0, L0, 0, 3
**	beq	a0,zero,.L[0-9]+
**	SFPNOP
**	SFPSTORE	0, L0, 0, 0
**	ret
**	SFPNOP
**	SFPSTORE	0, L0, 2, 0
**	li	a0,1
**	ret
*/

}
