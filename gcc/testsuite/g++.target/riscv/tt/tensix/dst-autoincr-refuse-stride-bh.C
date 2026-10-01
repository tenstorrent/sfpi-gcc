// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Near miss: rows whose strides disagree.  The group splits at the mismatch
// and neither half can pay for its configuration.  Byte-identical refusal.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: stride mismatch between rows" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 3 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 3 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using vec_t = __xtt_vector;

static inline void
row (unsigned stride)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  if (stride == 2)
    __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  else
    __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

void
mismatched_rows ()
{
  row (2);
  row (2);
  row (2);
  row (4);
  row (4);
  row (4);
}
