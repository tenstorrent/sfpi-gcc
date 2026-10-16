// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-autoincr-load-carrier -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a raw-word replay shadow counts one slot per word and load-terminated rows fire.
// { dg-final { scan-rtl-dump "Dst-autoincr: raw-word capture shadow counted .4 raw words of 4, bb \[0-9\]+; load-carrier." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "replay capture crosses block" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 6" 16 } }

using vec_t = __xtt_vector;

void
load_rows_raw_record (unsigned gate)
{
  if (gate)
    {
      // No-exec recording whose shadow is four raw constant words (the
      // envelope-record shape).  Recorded, never executed here; the
      // block is not forward-reachable from the rows.
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
    }

  // Covering prefix for the SETC16-to-consume distance guard.
  vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
  vec_t bcc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 2, 0);
  vec_t ccc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 3, 0);
  acc = __builtin_rvtt_sfpmad (acc, bcc, ccc, 0);
  acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);

  // LOAD-terminated rows: the row's final Dst access is the load itself
  // (reduction walk; no store).
#define ROW \
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}
