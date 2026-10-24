// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-min-benefit=0 -fdump-rtl-rvtt_mop_form-details" }
// Check that a base-ISA store asm after the call to a forming callee refuses MOP formation as opaque assembly.
// { dg-final { scan-rtl-dump "mop-caller-template-live-unproven.: opaque assembly" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }

#define MOP_TYPE1_WORD 0x01800000

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

int scratch;

void
kernel (unsigned n)
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
      asm volatile ("sw %0, 0(%1)" :: "r" (scratch), "r" (&scratch) : "memory");
    }
}
