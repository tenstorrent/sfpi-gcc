// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check dst-autoincr-rolled-tiny-refuse-bh.C with renamed identifiers and varied constants.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-dominates-rolled-body .rows 1, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using vec_t = __xtt_vector;

void
completely_different_kernel_name (unsigned n)
{
  for (unsigned zz = 0; zz != n; ++zz)
    {
      vec_t v = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 0, 7);
      vec_t w = __builtin_rvtt_sfpmul (v, v, 0);
      vec_t x = __builtin_rvtt_sfpadd (w, v, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 6, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
}
