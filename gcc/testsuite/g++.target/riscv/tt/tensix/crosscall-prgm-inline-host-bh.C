// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -fno-early-inlining -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// The callee is called from an inline helper that the IPA inliner (not
// the early one) folds into the host's tile loop: when the callee is
// compiled, the call still sits in the helper's clone body, outside any
// loop.  The clone body joins the scanned epoch whole, and the
// programming is placed in the host's loop entry.  The second host's
// helper carries vector dataflow of its own, so that move refuses.
// { dg-final { scan-tree-dump-times "crosscall-prgm: moved 1 programming pair" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "placed config pair .creg 1\\d. in void cph_host" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .crosscall-caller-stmt-unproven." "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

__attribute__((noinline)) void
cph_callee ()
{
  for (int row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

static inline void
cph_once ()
{
  cph_callee ();
}

void
cph_host (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cph_once ();
}

__attribute__((noinline)) void
renamed_vec_callee ()
{
  for (int row = 0; row != 4; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      v = __builtin_rvtt_sfpmul (v, k, 0);
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

static inline void
renamed_vec_once ()
{
  auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  y = __builtin_rvtt_sfpabs (y, 1);
  __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 6, 7);
  renamed_vec_callee ();
}

void
renamed_vec_host (int count)
{
  for (int i = 0; i != count; ++i)
    renamed_vec_once ();
}
