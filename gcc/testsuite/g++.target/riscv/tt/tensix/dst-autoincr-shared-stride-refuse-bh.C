// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The scratch slot is global state: groups requiring different programs
// (strides 2 and 4) may not share a dominating placement.  Each profitable
// group re-programs the slot immediately before its own rows.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "shared config" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 8 } }

using vec_t = __xtt_vector;

static inline void
row (unsigned addr, unsigned which)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, 7);
  if (which)
    __builtin_rvtt_ttincrwc (0, 4, 0, 0);
  else
    __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
mixed_rows ()
{
  row (0, 0); row (0, 0); row (0, 0); row (0, 0);
  __builtin_rvtt_ttincrwc (0, 0, 1, 0);
  row (0, 1); row (0, 1); row (0, 1); row (0, 1);
}
