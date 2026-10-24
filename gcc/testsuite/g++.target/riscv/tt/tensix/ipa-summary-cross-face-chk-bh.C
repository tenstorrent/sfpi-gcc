// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-mop-form -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_mop_form-details -fdump-rtl-rvtt_dst_autoincr-details" }
// Exercise ADDR_MOD caller edits and subsequent MOP summary queries under
// checking. The private ADDR_MOD callee has already expanded by then: do not
// bypass its missing compositional proof by pretending it is an external call.
// The paired control retains actual MOP formation without that unavailable body.
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "ipa-summary: mop-face digest built \\(void kernel\\(int\\)/\\d+, \\d+ blocks\\)" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "mop-caller-template-live-unproven.: unanalyzable callee" 2 "rvtt_mop_form" } }

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

__attribute__((noinline, noclone, noipa)) static void
cam_callee ()
{
  cam_row (0); cam_row (0); cam_row (0); cam_row (0);
  cam_row (0); cam_row (0); cam_row (0); cam_row (0);
}

void
kernel (int tiles)
{
#ifndef IPA_MOP_CONTROL
  for (int t = 0; t != tiles; ++t)
    cam_callee ();
#endif
  mop_a ();
  mop_b ();
}
