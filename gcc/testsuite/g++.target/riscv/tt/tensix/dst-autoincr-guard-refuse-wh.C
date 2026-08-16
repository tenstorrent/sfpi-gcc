// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: configuration-to-consume distance 1 below guard 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using vec_t = __xtt_vector;

static inline void
tight_row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, 3);
  __builtin_rvtt_sfpstore (nullptr, a, addr, 0, 0, 0, 3);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
tight_rows ()
{
  tight_row (0); tight_row (0); tight_row (0); tight_row (0);
  tight_row (0); tight_row (0); tight_row (0); tight_row (0);
}
