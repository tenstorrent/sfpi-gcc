// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-times "Lane merge: insn \[0-9\]+ keeps the old value of LREG mask 0x02" 2 "rvtt_lreg_livein" } }
// { dg-final { scan-rtl-dump-times "Lane merge:" 2 "rvtt_lreg_livein" } }
// { dg-final { scan-rtl-dump-not "Lane merge: insn \[0-9\]+ keeps the old value of LREG mask 0x04" "rvtt_lreg_livein" } }

#ifndef __riscv_xtt_sfprawlreg_merge
#error "missing raw-LREG lane merge capability macro"
#endif

/* A raw write changes only the lanes enabled when it issues.  The old value
   of its destination is an input only where some lane may be disabled and a
   later read may see that lane enabled again.  */

#define TTI(word) asm volatile (".ttinsn %0" : : "n" (word))
#define SFPLOADI_L1(imm) TTI (0x71120000 | (imm))
#define SFPSETCC_L0 TTI (0x7b000000)
#define SFPENCC_ALL_LANES TTI (0x8a00300a)
#define SFPMAD_L3_L1 TTI (0x84011130)
#define SFPMOV_L2_L1 TTI (0x7c000120)

/* Every lane is enabled at the second write, so it replaces the old value
   in every lane.  */
void
full_lane_rewrite ()
{
  SFPLOADI_L1 (1);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPLOADI_L1 (2);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPMOV_L2_L1;
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x04);
}

/* The second write issues under SFPSETCC, and SFPENCC re-enables the lanes
   before L1 is read: the disabled lanes still hold the first value.  */
void
partial_write_read_after_reenable ()
{
  SFPLOADI_L1 (1);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPSETCC_L0;
  __builtin_rvtt_sfprawlreg_effect (0x01, 0);
  SFPLOADI_L1 (2);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPENCC_ALL_LANES;
  __builtin_rvtt_sfprawlreg_effect (0, 0);
  SFPMOV_L2_L1;
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x04);
}

/* The same partial write, read only while the same lanes are enabled.  The
   SFPMAD in between leaves lane state alone.  */
void
partial_write_read_under_same_lanes ()
{
  SFPLOADI_L1 (1);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPSETCC_L0;
  __builtin_rvtt_sfprawlreg_effect (0x01, 0);
  SFPLOADI_L1 (2);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPMAD_L3_L1;
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x08);
  SFPMOV_L2_L1;
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x04);
  SFPENCC_ALL_LANES;
  __builtin_rvtt_sfprawlreg_effect (0, 0);
}

/* A typed read is not followed any further, so it may see every lane.  */
void
partial_write_typed_read ()
{
  SFPLOADI_L1 (1);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPSETCC_L0;
  __builtin_rvtt_sfprawlreg_effect (0x01, 0);
  SFPLOADI_L1 (2);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4);
}

/* A TT_ issue builds its word from runtime operands, but the constant part
   still names an SFPLOAD, which leaves lanes alone: the partial write of L2
   is read only under the same lanes.  */
void
runtime_word_keeps_lanes (volatile unsigned *issue, unsigned addr)
{
  TTI (0x71220000);
  __builtin_rvtt_sfprawlreg_effect (0, 0x04);
  SFPSETCC_L0;
  __builtin_rvtt_sfprawlreg_effect (0x01, 0);
  TTI (0x71220001);
  __builtin_rvtt_sfprawlreg_effect (0, 0x04);
  *issue = 0x70300000 + (addr & 0x1fff);
  __builtin_rvtt_sfprawlreg_effect (0, 0x08);
  TTI (0x7c000240);
  __builtin_rvtt_sfprawlreg_effect (0x04, 0x10);
  SFPENCC_ALL_LANES;
  __builtin_rvtt_sfprawlreg_effect (0, 0);
}

/* Nothing in the function defined L1 before the partial write, so its
   disabled lanes hold nothing a later read can rely on.  */
void
partial_write_of_undefined_lreg ()
{
  SFPSETCC_L0;
  __builtin_rvtt_sfprawlreg_effect (0x01, 0);
  SFPLOADI_L1 (2);
  __builtin_rvtt_sfprawlreg_effect (0, 0x02);
  SFPENCC_ALL_LANES;
  __builtin_rvtt_sfprawlreg_effect (0, 0);
  SFPMOV_L2_L1;
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x04);
}
