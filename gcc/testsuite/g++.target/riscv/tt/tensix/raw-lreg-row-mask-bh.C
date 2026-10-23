// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-times "Lane merge: insn \[0-9\]+ keeps the old value of LREG mask 0x02" 7 "rvtt_lreg_livein" } }
// { dg-final { scan-rtl-dump-times "Lane merge:" 7 "rvtt_lreg_livein" } }

#define RAW(word) asm volatile (".ttinsn %0" : : "n" (word))
#define WRITE_L1(value) do { RAW (0x71120000 | (value)); \
  __builtin_rvtt_sfprawlreg_effect (0, 2); } while (0)
#define READ_L1() __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4)
#define ENABLE_FLAGS() __builtin_rvtt_sfpencc (10, 3)
#define RESET_LANES() do { ENABLE_FLAGS (); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); } while (0)

/* The caller may leave ROW_MASK set.  SFPENCC alone cannot establish a
   full-lane overwrite, even when the function never writes LaneConfig.  */
void entry_state_is_unknown ()
{
  WRITE_L1 (1);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

/* An unpaired raw configuration write invalidates the clear-ROW_MASK fact.
   The following typed SFPENCC only changes LaneFlags.  */
void raw_row_mask_then_enable ()
{
  RESET_LANES ();
  WRITE_L1 (1);
  RAW (0x910001f1);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

/* Without IMM16_IS_VALUE, SFPCONFIG takes LaneConfig from L0.  */
void raw_register_lane_config ()
{
  RESET_LANES ();
  WRITE_L1 (1);
  RAW (0x910000f0);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

void typed_row_mask_then_enable ()
{
  RESET_LANES ();
  WRITE_L1 (1);
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

void row_mask_at_join (bool set_mask)
{
  RESET_LANES ();
  WRITE_L1 (1);
  if (set_mask)
    __builtin_rvtt_sfpconfig_i (1, 15, 1);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

/* A reset potentially swallowed by raw REPLAY capture establishes no fact.  */
void recorded_reset_is_unproved ()
{
  WRITE_L1 (1);
  RAW (0x04000401);
  RESET_LANES ();
  WRITE_L1 (2);
  READ_L1 ();
}

/* LaneConfig writes are predicated by column flags.  Clearing it before
   those flags are known all-enabled does not establish a whole reset.  */
void clear_before_flags_is_unproved ()
{
  WRITE_L1 (1);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  ENABLE_FLAGS ();
  WRITE_L1 (2);
  READ_L1 ();
}

/* Positive controls: an executed, all-column zero assignment establishes
   the second must-fact, so a later full write kills the old definition.  */
void explicit_typed_reset ()
{
  RESET_LANES ();
  WRITE_L1 (1);
  WRITE_L1 (2);
  READ_L1 ();
}

void explicit_raw_reset ()
{
  RAW (0x8a00300a);
  RAW (0x910000f1);
  WRITE_L1 (1);
  WRITE_L1 (2);
  READ_L1 ();
}
