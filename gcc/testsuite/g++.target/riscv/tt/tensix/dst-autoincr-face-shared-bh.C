// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Straight-line: the typed face advance separates the rows into two groups
// (it changes RWC state, so it is never gap-legal), but it cannot write the
// modifier slots, so the second group shares the first group's program
// across the face boundary -- one three-word program, four absorbed rows.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 3 words\n" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 shared config" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }

using vec_t = __xtt_vector;

void face_shared ()
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  vec_t b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t q = __builtin_rvtt_sfpmul (b, b, 0);
  __builtin_rvtt_sfpstore (nullptr, q, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttdstface ();
  vec_t c = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t r = __builtin_rvtt_sfpmul (c, c, 0);
  __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  vec_t d = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t s = __builtin_rvtt_sfpmul (d, d, 0);
  __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
