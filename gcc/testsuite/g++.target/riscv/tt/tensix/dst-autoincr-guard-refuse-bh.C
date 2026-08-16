// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// SETC16-to-consume distance guard (independent-review carry-forward):
// rows with no compute between the load and the terminator store give the
// slot program only one issued word of settle distance, below the
// two-word guard from the capability table.  No legal earlier anchor
// exists (block start), so the group refuses byte-identically.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: configuration-to-consume distance 1 below guard 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 4 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

using vec_t = __xtt_vector;

static inline void
tight_row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, a, addr, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
tight_rows ()
{
  tight_row (0);
  tight_row (0);
  tight_row (0);
  tight_row (0);
}
