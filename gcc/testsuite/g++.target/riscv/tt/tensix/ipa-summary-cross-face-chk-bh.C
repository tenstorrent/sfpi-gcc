// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-mop-form -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_mop_form-details -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that an ADDR_MOD contract commit invalidates the caller's IPA summary consulted by two MOP formations under -fchecking.
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "ipa-summary: mop-face digest built \\(void kernel\\(int\\)/\\d+, \\d+ blocks\\)" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, counted loop\\): 20 iterations of launch \\\[0,\\+3\\) -> TTMOP 0, 19, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump "mop-caller-template-live-unproven.: unanalyzable callee" "rvtt_mop_form" } }

using vec_t = __xtt_vector;

__attribute__ ((noinline, noclone)) static void
mop_a ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}

__attribute__ ((noinline, noclone)) static void
mop_b ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (1);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 1);
  for (unsigned i = 0; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}

static inline void
cam_row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

__attribute__((noinline)) void
cam_callee ()
{
  cam_row (0); cam_row (0); cam_row (0); cam_row (0);
  cam_row (0); cam_row (0); cam_row (0); cam_row (0);
}

void
kernel (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cam_callee ();
  mop_a ();
  mop_b ();
}
