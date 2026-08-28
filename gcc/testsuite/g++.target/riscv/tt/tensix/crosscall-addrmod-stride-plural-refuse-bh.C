// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Two strides in one callee: the contract would need two slot programs
// in one owned slot -- the single-program condition refuses by name and
// both groups keep the per-execution refusal byte-identically.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .stride-plural." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using cam_vec_t = __xtt_vector;

static inline void
cam_row_s (unsigned stride)
{
  cam_vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  cam_vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, stride, 0, 0);
}

__attribute__((noinline)) void
cam_callee_mixed ()
{
  cam_row_s (2); cam_row_s (2); cam_row_s (2); cam_row_s (2);
  cam_row_s (2); cam_row_s (2); cam_row_s (2); cam_row_s (2);
  cam_row_s (4); cam_row_s (4); cam_row_s (4); cam_row_s (4);
  cam_row_s (4); cam_row_s (4); cam_row_s (4); cam_row_s (4);
}

void
cam_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cam_callee_mixed ();
}
