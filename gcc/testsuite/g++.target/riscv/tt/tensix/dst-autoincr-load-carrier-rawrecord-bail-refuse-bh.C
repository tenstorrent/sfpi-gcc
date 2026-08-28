// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Knob-off half of dst-autoincr-load-carrier-rawrecord-fire-bh.C: the
// same function, WITHOUT -mtt-tensix-optimize-dst-autoincr-load-carrier.
// The no-exec recording's shadow is raw `.ttinsn' words, which the
// pin-38 shadow scan counts as zero replay slots; the scan overruns the
// recording's block and the whole function refuses byte-identically
// ("replay capture crosses block").  Every explicit increment survives
// and no slot program is emitted -- the preserved refusing baseline the
// load-carrier knob's corpus delta is adjudicated against.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: replay capture crosses block" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "raw-word capture shadow counted" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 16 } }
// { dg-final { scan-assembler-not "TTSETC16\t34," } }
// { dg-final { scan-assembler-not "SFPLOAD\tL., 0, 0, 6" } }

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
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}
