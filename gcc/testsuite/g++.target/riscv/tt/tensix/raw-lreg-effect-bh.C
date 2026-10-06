// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "# RAWLREG_EFFECT" 12 } }
// { dg-final { scan-assembler "# RAWLREG_EFFECT 255, 255" } }
// { dg-final { scan-assembler "# READ L1.*# RAW INPUT L1.*# RAWLREG_EFFECT 2, 0" } }
// { dg-final { scan-assembler "# RAW WRITE L2.*# RAWLREG_EFFECT 0, 4.*# READ L2.*# RAW READ L2.*# RAWLREG_EFFECT 4, 0" } }
// { dg-final { scan-assembler "# RAW WRITE L3.*# RAWLREG_EFFECT 0, 8.*# READ L3.*# RAW RMW L3.*# RAWLREG_EFFECT 8, 8" } }
// { dg-final { scan-assembler "# WRITE L4.*# READ L4.*SFPLOAD.*# RAW INPUT L4.*# RAWLREG_EFFECT 16, 0" } }
// { dg-final { scan-assembler "# READ L5.*SFPLOAD.*# RAW ENTRY INPUT L5.*# RAWLREG_EFFECT 32, 0" } }
// { dg-final { scan-assembler-times "# READ L6" 2 } }
// { dg-final { scan-assembler "# READ L6.*SFPMOV.*L6.*# RAW CLOBBER L6.*# RAWLREG_EFFECT 0, 64.*SFPADD" } }
// { dg-final { scan-assembler-times "# READ L7" 2 } }

#ifndef __riscv_xtt_sfprawlreg_effect
#error "missing raw-LREG effect capability macro"
#endif

void
raw_effect_dynamic_masks (unsigned reads, unsigned writes)
{
  __builtin_rvtt_sfprawlreg_effect (reads, writes);
}

/* A previously unowned input must be reserved before the opaque instruction,
   not merely from the marker onwards.  The typed read ends ownership.  */
void
raw_effect_input ()
{
  asm volatile ("# RAW INPUT L1");
  __builtin_rvtt_sfprawlreg_effect (0x02, 0);
  (void) __builtin_rvtt_sfpreadlreg (1);
}

/* A demanded output remains reserved from its raw definition through the
   later raw read; its last use is derived from the read effect.  */
void
raw_effect_output_to_read ()
{
  asm volatile ("# RAW WRITE L2");
  __builtin_rvtt_sfprawlreg_effect (0, 0x04);
  asm volatile ("# RAW READ L2");
  __builtin_rvtt_sfprawlreg_effect (0x04, 0);
}

/* A register read and rewritten by one raw instruction needs the old
   reservation through the instruction and a fresh reservation afterwards.  */
void
raw_effect_read_write ()
{
  asm volatile ("# RAW WRITE L3");
  __builtin_rvtt_sfprawlreg_effect (0, 0x08);
  asm volatile ("# RAW RMW L3");
  __builtin_rvtt_sfprawlreg_effect (0x08, 0x08);
  (void) __builtin_rvtt_sfpreadlreg (3);
}

/* Backward demand starts immediately after a typed definition, protecting the
   value from compiler temporaries all the way to the later raw consumer.  */
void
raw_effect_typed_definition_gap ()
{
  auto seed = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpwritelreg (seed, 4);
  auto temporary = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  asm volatile ("# RAW INPUT L4");
  __builtin_rvtt_sfprawlreg_effect (0x10, 0);
  __builtin_rvtt_sfpwritelreg (temporary, 0);
}

/* With no in-function definition, demand reaches function entry.  */
void
raw_effect_function_entry_gap ()
{
  auto temporary = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  asm volatile ("# RAW ENTRY INPUT L5");
  __builtin_rvtt_sfprawlreg_effect (0x20, 0);
  __builtin_rvtt_sfpwritelreg (temporary, 0);
}

/* Dead per-instruction outputs are definitions with no uses, not implicit
   live-outs.  They must not reserve L6 across the rest of the function.  */
void
raw_effect_dead_outputs ()
{
  asm volatile ("# RAW DEAD WRITE L6 A");
  __builtin_rvtt_sfprawlreg_effect (0, 0x40);
  asm volatile ("# RAW DEAD WRITE L6 B");
  __builtin_rvtt_sfprawlreg_effect (0, 0x40);
  asm volatile ("# RAW DEAD WRITE L6 C");
  __builtin_rvtt_sfprawlreg_effect (0, 0x40);
  auto value = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

/* A dead raw output still clobbers its architectural register.  VALUE is live
   across that point and must not remain allocated to L6.  */
void
raw_effect_dead_output_clobber ()
{
  auto value = __builtin_rvtt_sfpreadlreg (6);
  asm volatile ("# RAW CLOBBER L6");
  __builtin_rvtt_sfprawlreg_effect (0, 0x40);
  value = __builtin_rvtt_sfpadd (value, value, 0);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

/* A plain typed read is already visible to RTL.  A later ownership marker for
   another register must not manufacture an entry sentinel for L7.  */
void
plain_typed_read_before_raw_access ()
{
  auto value = __builtin_rvtt_sfpreadlreg (7);
  __builtin_rvtt_sfprawlreg_access (0, 0x02);
  __builtin_rvtt_sfpwritelreg (value, 0);
}
