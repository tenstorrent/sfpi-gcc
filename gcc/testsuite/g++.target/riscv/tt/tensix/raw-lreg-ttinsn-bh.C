// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "# RAWLREG_EFFECT" 5 } }
// { dg-final { scan-assembler "# READ L1.*SFPLOAD.*\\.ttinsn.*# RAWLREG_EFFECT 2, 4" } }
// { dg-final { scan-assembler "# READ L3.*SFPLOAD.*sw.*# RAWLREG_EFFECT 8, 0" } }
// { dg-final { scan-assembler "# READ L6.*SFPMOV.*L6.*\\.ttinsn.*# RAWLREG_EFFECT 0, 64.*SFPADD" } }

/* ttinsn's final three operands are reserved for future CC/LREG semantics
   upstream.  Keep the explicit marker contract while the new raw issue
   builtin coexists with the legacy asm/store forms.  In particular, both
   constant and runtime issue paths must reserve raw inputs across typed
   temporaries, and a dead raw output must clobber a live typed value.  */

void
constant_issue_input ()
{
  auto temporary = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttinsn (nullptr, 0x7c000120, 0, 0x04, 0x02);
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x04);
  __builtin_rvtt_sfpwritelreg (temporary, 0);
}

void
runtime_issue_input (volatile unsigned long *issue, unsigned word)
{
  auto temporary = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttinsn (issue, word, 0, 0, 0x08);
  __builtin_rvtt_sfprawlreg_effect (0x08, 0);
  __builtin_rvtt_sfpwritelreg (temporary, 0);
}

void
constant_issue_dead_output ()
{
  auto value = __builtin_rvtt_sfpreadlreg (6);
  __builtin_rvtt_ttinsn (nullptr, 0x71620001, 0, 0x40, 0);
  __builtin_rvtt_sfprawlreg_effect (0, 0x40);
  value = __builtin_rvtt_sfpadd (value, value, 0);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

/* Explicit metadata also connects a builtin issue to a legacy raw consumer
   through the same hard-register interval.  */
void
builtin_to_legacy_consumer ()
{
  __builtin_rvtt_ttinsn (nullptr, 0x71220001, 0, 0x04, 0);
  __builtin_rvtt_sfprawlreg_effect (0, 0x04);
  asm volatile (".ttinsn %0" : : "n" (0x7c000230));
  __builtin_rvtt_sfprawlreg_effect (0x04, 0x08);
}
