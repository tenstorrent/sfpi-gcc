// Outward ownership discharges: formation proceeds (under force; the
// rows are execution-bound) exactly when the caller side is proven
// safe.
// - rearming_kernel re-programs every MOP config word between the call
//   and its next type-1 launch (the production per-epoch
//   ckernel_template::program protocol, the correctness-harness
//   shape): every caller root re-arms, the callee forms;
// - main is the kernel entry: outermost by the crt0-benign axiom, its
//   in-body run forms even though the TU also contains other MOP
//   traffic (main stays call-free: the inward mop-config-unowned proof
//   is a separate obligation).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP outward ownership proven: every caller root re-arms the template before its next post-return MOP launch" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP outward ownership proven: kernel entry \\(crt0-benign axiom\\)" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, run\\): 4 iterations of launch \\\[0,\\+3\\) -> TTMOP 0, 3, 0" 2 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "mop-caller-template-live-unproven" "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 3, 0" 2 } }

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
      // Per-epoch template program: every config word rewritten before
      // the launch (the ckernel_template::program discipline).
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

int
main ()
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
  return 0;
}
