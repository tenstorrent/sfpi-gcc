// { dg-options "-mcpu=tt-bh-tensix -O0 -fdump-rtl-ira" }
// { dg-final { scan-assembler-times "# RAWLREG_EFFECT 255, 255" 3 } }
// { dg-final { scan-rtl-dump-not {REG_DEAD \(const_int} "ira" } }

/* At -O0 these ordinary constexpr calls still produce register operands.
   Normalizing the mask operands must rescan their DF use locations.  Without
   that rescan, the note problem emits REG_DEAD (const_int 255), and IRA can
   index register statistics with a constant interpreted as a register.  */
constexpr unsigned reads (unsigned x) { return (x >> 4) & 255; }
constexpr unsigned writes (unsigned x) { return x & 255; }

void raw_mask_rescan (volatile unsigned *issue)
{
  *issue = 1;
  __builtin_rvtt_sfprawlreg_effect (reads (0x101), writes (0x101));
  *issue = 2;
  __builtin_rvtt_sfprawlreg_effect (reads (0x203), writes (0x203));
  *issue = 3;
  __builtin_rvtt_sfprawlreg_effect (reads (0x304), writes (0x304));
}
