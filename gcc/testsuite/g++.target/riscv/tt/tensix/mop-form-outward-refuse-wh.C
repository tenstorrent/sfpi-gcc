// WH twin of the direct outward-ownership refusal: the caller's
// hoisted type-1 template is launched per iteration around the call
// into the formed function; the formation refuses by name and the
// delivery stays byte-identical (MOP facts are WH/BH-identical,
// rvtt-mop-tables.h).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-caller-template-live-unproven\\)" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "is reachable after a call to this function without a full template re-arm" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP\\t" } }

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
wrapper_hazard (unsigned n)
{
  volatile unsigned *mop_cfg = (volatile unsigned *) 0xFFB80000;
  for (unsigned i = 0; i != 9; ++i)
    mop_cfg[i] = 0x02000000;
  for (unsigned i = 0; i != n; ++i)
    {
      asm volatile (".ttinsn %0" :: "i" (MOP_TYPE1_WORD));
      formed_callee ();
    }
}
