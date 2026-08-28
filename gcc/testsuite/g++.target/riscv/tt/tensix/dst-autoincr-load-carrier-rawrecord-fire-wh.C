// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-autoincr-load-carrier -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole twin of dst-autoincr-load-carrier-rawrecord-fire-bh.C: the
// raw-word counting fact is frontend-generic (a canonical
// single-constant `.ttinsn' word is one 32-bit Tensix word on either
// frontend; WormholeB0 REPLAY.md's Load loop stores every incoming
// word, one slot each).  Replay formation is disabled so the rows keep
// the explicit shape (default WH formation would reshape delivery).
// Wormhole scratch modifier 2 = physical slot 6: SETC16 19/29/54.
// { dg-final { scan-rtl-dump "Dst-autoincr: raw-word capture shadow counted .4 raw words of 4, bb \[0-9\]+; load-carrier." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "replay capture crosses block" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-not "TTSETC16\t25," } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 2" 16 } }

using vec_t = __xtt_vector;

void
load_rows_raw_record (unsigned gate)
{
  if (gate)
    {
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
      asm volatile (".ttinsn %0" :: "n" (0x7010e000));
      asm volatile (".ttinsn %0" :: "n" (0x8500a100));
    }

  vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
  vec_t bcc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 2, 0);
  vec_t ccc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 3, 0);
  acc = __builtin_rvtt_sfpmad (acc, bcc, ccc, 0);
  acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);

#define ROW \
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 3);
}
