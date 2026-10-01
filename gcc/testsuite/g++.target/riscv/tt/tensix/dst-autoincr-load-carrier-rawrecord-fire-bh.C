// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-autoincr-load-carrier -fdump-rtl-rvtt_dst_autoincr-details" }
// Load-carrier unlock: the function's no-exec replay
// recording has a RAW `.ttinsn' word shadow (the LLK envelope record
// shape -- TTI_ macro words, not typed builtins).  Without the knob the
// recording-shadow scan counts raw words as zero slots, overruns the
// block, and refuses the WHOLE function ("replay capture crosses
// block"): dst-autoincr-load-carrier-rawrecord-bail-refuse-bh.C is the
// knob-off half of this pair.  With the knob the audited extraction
// (rvtt_raw_ttinsn_word) counts each canonical single-constant word as
// exactly one replay slot, the shadow closes, and the LOAD-terminated
// rows (load/compute, no store -- the reduction-walk class of the lane
// IE useq probe) fire exactly as the record-free function already does
// at the same pin.  The raw words stay classification-refused: the
// capture never becomes a rewritable payload.
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
