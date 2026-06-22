// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

void addv () {
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto zero = __builtin_rvtt_sfpreadlreg (9);

  __builtin_rvtt_sfppushc (0);

  __builtin_rvtt_sfpiadd_v (one, a, 2);
  a = __builtin_rvtt_sfpassign_lv (a, zero);

  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
}
/*
**_Z4addvv:
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPIADD	L10, L0, 0, 2
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void addi () {
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto zero = __builtin_rvtt_sfpreadlreg (9);

  __builtin_rvtt_sfppushc (0);

  __builtin_rvtt_sfpiadd_i (nullptr, a, 10, 0, 0, 0);
  a = __builtin_rvtt_sfpassign_lv (a, zero);

  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
}
/*
**_Z4addiv:
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPIADD	L15, L0, 10, 1
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void addi (unsigned val) {
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto zero = __builtin_rvtt_sfpreadlreg (9);

  __builtin_rvtt_sfppushc (0);

  __builtin_rvtt_sfpiadd_i (nullptr, a, val, 0, 0, 0);
  a = __builtin_rvtt_sfpassign_lv (a, zero);

  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
}

/*
**_Z4addij:
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	slli	a5,a0,16
**	li	a4, 1897005056	# 2:71120000
**	srli	a5,a5,16
**	add	a5,a5,a4
**	sw	a5, 0\(zero\)	# 2:SFPLOADI	L1, a5, 2
**	srli	a0,a0,16
**	li	a5, 1897398272	# 4:71180000
**	add	a0,a0,a5
**	sw	a0, 0\(zero\)	# 4:SFPLOADI	L1, a0, 8	# LV:L1
**	SFPMOV	L2, L0, 2
**	SFPIADD	L2, L1, 0, 0
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void exexp () {
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto zero = __builtin_rvtt_sfpreadlreg (9);

  __builtin_rvtt_sfppushc (0);

  __builtin_rvtt_sfpexexp (a, 2);
  a = __builtin_rvtt_sfpassign_lv (a, zero);

  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
}
/*
**_Z5exexpv:
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPEXEXP	L1, L0, 2
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/

void lz () {
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto zero = __builtin_rvtt_sfpreadlreg (9);

  __builtin_rvtt_sfppushc (0);

  __builtin_rvtt_sfplz (a, 2);
  a = __builtin_rvtt_sfpassign_lv (a, zero);

  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 0);
}
/*
**_Z2lzv:
**	SFPLOAD	L0, 0, 0, 0, 0, 0
**	SFPLZ	L15, L0, 2
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 0, 0, 0, 0, 0
**	ret
*/
