// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-min-benefit=0 -fdump-rtl-rvtt_mop_form-details" }
// Check that MOP outward-ownership proof uses the IPA summary's per-block digests and still proves the re-arm shape.
// { dg-final { scan-rtl-dump "ipa-summary: mop-face digest built \\(void rearming_kernel\\(unsigned int\\)/\\d+, \\d+ blocks\\)" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP outward ownership proven: every caller root re-arms the template before its next post-return MOP launch" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "mop-caller-template-live-unproven" "rvtt_mop_form" } }

#define MOP_TYPE1_WORD 0x01800000 // raw MOP, mop_type 1, counts from cfg

__attribute__ ((noinline, noclone)) static void
formed_callee ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}

void
rearming_kernel (unsigned n)
{
  volatile unsigned *mop_cfg = (volatile unsigned *) 0xFFB80000;
  for (unsigned i = 0; i != n; ++i)
    {
      mop_cfg[0] = 0x00000001;
      mop_cfg[1] = 0x00000001;
      mop_cfg[2] = 0x02000000;
      mop_cfg[3] = 0x02000000;
      mop_cfg[4] = 0x02000000;
      mop_cfg[5] = 0x02000000;
      mop_cfg[6] = 0x02000000;
      mop_cfg[7] = 0x02000000;
      mop_cfg[8] = 0x02000000;
      asm volatile (".ttinsn %0" :: "i" (MOP_TYPE1_WORD));
      formed_callee ();
    }
}
