// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

void foo () {
    auto a = __builtin_rvtt_sfpreadlreg (0);
    auto r = __builtin_rvtt_sfpstochrnd_i (nullptr, 0, 4, 0, 0, a, 0);
    __builtin_rvtt_sfpwritelreg (r, 0);
}
/*
**_Z3foov:
**	SFPSTOCHRND	L0, L0, L0, 0, 0, 4
**	ret
*/
void bar () {
    auto a = __builtin_rvtt_sfpreadlreg (0);
    auto b = __builtin_rvtt_sfpreadlreg (1);
    auto r = __builtin_rvtt_sfpstochrnd_v (0, a, b, 0);
    __builtin_rvtt_sfpwritelreg (r, 0);
}
/*
**_Z3barv:
**	SFPSTOCHRND	L0, L0, L1, 0, 0, 0
**	ret
*/
