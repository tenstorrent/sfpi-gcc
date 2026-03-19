// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

void foo () {
    auto a = __builtin_rvtt_sfpreadlreg (0);
    auto r = __builtin_rvtt_sfpstochrnd_i (nullptr, a, 0x1f, 0, 0, 4, 1);
    __builtin_rvtt_sfpwritelreg (r, 0);
}
/*
**_Z3foov:
**	SFPSTOCHRND	L0, L0, L0, 31, 12, 1
**	ret
*/
void bar () {
    auto a = __builtin_rvtt_sfpreadlreg (0);
    auto b = __builtin_rvtt_sfpreadlreg (1);
    auto r = __builtin_rvtt_sfpstochrnd_v (b, a, 4, 1);
    __builtin_rvtt_sfpwritelreg (r, 0);
}
/*
**_Z3barv:
**	SFPSTOCHRND	L0, L0, L1, 0, 4, 1
**	ret
*/
void baz () {
    auto a = __builtin_rvtt_sfpreadlreg (0);
    auto r = __builtin_rvtt_sfpstochrnd_i (nullptr, a, 0, 0, 0, 1, 2);
    __builtin_rvtt_sfpwritelreg (r, 0);
}
/*
**_Z3bazv:
**	SFPSTOCHRND	L0, L0, L0, 0, 1, 2
**	ret
*/
