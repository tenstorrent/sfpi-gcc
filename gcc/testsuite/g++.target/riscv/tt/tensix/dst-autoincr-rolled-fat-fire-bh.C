// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The covered-regime silicon witness class (lane EP finding F1): a one-row
// rolled loop whose FAT body's own slot-occupying words cover the audited
// drained-frontend window (the threshold/hardshrink shape -- 10-slot
// iterations measure the crossing at ~0.06 cycles, while refusing them
// cost +26.95/+27.06 booked at pin 16).  Same rows-per-crossing as the
// skinny refusal twin; only the iteration's covering distance differs --
// the verdict prices the covering walk, not "one row".
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing covered .rows 1, iteration slot words 10 >= drain window 7, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }

using vec_t = __xtt_vector;

void
rolled_fat_rows ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
