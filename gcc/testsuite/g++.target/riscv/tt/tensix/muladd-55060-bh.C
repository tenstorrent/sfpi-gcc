// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }
// { dg-final { check-function-bodies "**" "" } }

extern volatile unsigned long iptr[];

void muladd1 () {
  // Default contraction must not discard matched immediate folds and
  // silently replace separate multiply/add rounding with fused rounding.
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfploadi (nullptr, 0xbf88, 0, 0, 0);
  auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f88, 0, 0, 0);
  auto p = __builtin_rvtt_sfpmul (a, b, 0);
  auto r = __builtin_rvtt_sfpadd (p, c, 0);

  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_Z7muladd1v:
**	# READ L0
**	SFPMULI	L0, 49032, 0
**	SFPMOV	L3, L0, 2
**	SFPADDI	L3, 16264, 0
**	# WRITE L3
**	ret
*/

void muladd3 () {
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfploadi (nullptr, 0xbf84, 0, 0, 0);
  auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f84, 0, 0, 0);
  auto p = __builtin_rvtt_sfpmul (a, b, 0);
  auto r = __builtin_rvtt_sfpmul (p, c, 0);

  __builtin_rvtt_sfpwritelreg (r, 3);
}
/*
**_Z7muladd3v:
**	# READ L0
**	SFPMULI	L0, 49028, 0
**	SFPMOV	L3, L0, 2
**	SFPMULI	L3, 16260, 0
**	# WRITE L3
**	ret
*/
