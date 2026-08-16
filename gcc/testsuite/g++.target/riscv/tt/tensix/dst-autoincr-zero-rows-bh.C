// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Zero rows: accesses without any typed TTINCRWC give the pass nothing to
// absorb.  No configuration may be emitted.
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 2 } }

using vec_t = __xtt_vector;

void
no_rows ()
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  vec_t b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  vec_t q = __builtin_rvtt_sfpmul (b, b, 0);
  __builtin_rvtt_sfpstore (nullptr, q, 0, 0, 0, 0, 7);
}
