// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-autoincr-load-carrier -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed/varied twin of dst-autoincr-load-carrier-rawrecord-fire-bh.C
// (charter 0.3): different names, a SIX-word raw shadow with different
// word values and a nonzero buffer base, stride 4, twelve rows.  The
// counting fact is value- and name-independent: every canonical
// single-constant `.ttinsn' word counts as one slot, whatever it
// encodes.
// { dg-final { scan-rtl-dump "Dst-autoincr: raw-word capture shadow counted .6 raw words of 6, bb \[0-9\]+; load-carrier." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "replay capture crosses block" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 12 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 6" 12 } }

using v_t = __xtt_vector;

void
walker_rows_widened (unsigned tick)
{
  if (tick & 3)
    {
      __builtin_rvtt_ttreplay (nullptr, 6, 0, 8, 0, 0, 1);
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
    }

  v_t alpha = __builtin_rvtt_sfploadi (nullptr, 0, 0, 5, 0);
  v_t beta = __builtin_rvtt_sfploadi (nullptr, 0, 0, 6, 0);
  v_t gamma = __builtin_rvtt_sfploadi (nullptr, 0, 0, 7, 0);
  alpha = __builtin_rvtt_sfpmad (alpha, beta, gamma, 0);
  alpha = __builtin_rvtt_sfpmad (alpha, alpha, alpha, 0);

#define WROW \
  { v_t r = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    alpha = __builtin_rvtt_sfpmad (alpha, r, alpha, 0); \
    __builtin_rvtt_ttincrwc (0, 4, 0, 0); }
  WROW WROW WROW WROW WROW WROW
  WROW WROW WROW WROW WROW WROW
  __builtin_rvtt_sfpstore (nullptr, alpha, 0, 0, 0, 0, 7);
}
