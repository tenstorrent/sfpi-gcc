/* A runtime immediate needs an instruction buffer to hold the synthesised
   word.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that dynamically-addressed stores, which cannot be retargeted, refuse.
// { dg-final { scan-rtl-dump "Dst-autoincr: not a candidate row .no owned terminator access before increment" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using vec_t = __xtt_vector;

static inline void
row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (ckernel::instrn_buffer, p, addr, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
dynamic_rows (unsigned addr)
{
  row (addr);
  row (addr);
  row (addr);
  row (addr);
  row (addr);
  row (addr);
  row (addr);
  row (addr);
}
