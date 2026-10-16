// Check that MOP template slots programmed with audited words let MOP launches pass the TU freedom proof.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "mop-template" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

typedef volatile unsigned int vu32;
extern volatile unsigned long __instrn_buffer[];

// The ckernel_template::program shape: constant-address volatile stores
// at TENSIX_MOP_CFG_BASE + 4*i.
static inline void program_elw_template (unsigned outer, unsigned inner)
{
  ((vu32 *) 0xFFB80000)[0] = outer;	    // loop lengths: any value
  ((vu32 *) 0xFFB80000)[1] = inner;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;    // Tensix NOP start op
  ((vu32 *) 0xFFB80000)[3] = 0x37020044;    // SETRWC end op
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = 0x28008000;    // ELWADD loop op
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
}

void datacopy_then_math (unsigned rows)
{
  program_elw_template (rows, 4);
  asm volatile (".ttinsn %0" :: "i" (0x01800000));   // TTI_MOP (1, 0, 0)

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// Same shape with the MOVA2D word class, a runtime-composed SETRWC slot
// value and a runtime-composed MOP push through the instruction buffer
// (the TT_MOP macro shape).
static inline void arm_gather_template (unsigned reps, unsigned stride)
{
  ((vu32 *) 0xFFB80000)[0] = reps;
  ((vu32 *) 0xFFB80000)[1] = 1;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;
  ((vu32 *) 0xFFB80000)[3] = 0x37000000 + (stride & 0x3f);
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = 0x1200a000;    // MOVA2D loop op
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
}

void gather_rows_then_blend (unsigned reps, unsigned zmask)
{
  arm_gather_template (reps, 8);
  __instrn_buffer[0] = 0x03000000 + (zmask >> 16);	  // TT_MOP_CFG
  __instrn_buffer[0] = 0x01000000 + ((reps - 1) << 16)
		       + (zmask & 0xffff);		  // TT_MOP (0, ...)

  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto gain = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto blended = __builtin_rvtt_sfpmul (acc, gain, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, blended, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
