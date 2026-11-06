// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-assembler "SFPCONFIG\\t12, 0, 0" } }
// { dg-final { scan-assembler "SFPLOADI\\tL0, 16384, 0" } }
// { dg-final { scan-assembler-not "SFPBAD" } }

/* Programming a constant through value-form SFPCONFIG assigns the typed
   value to architectural L0.  Subsequent raw half loads consume that new
   value, so they must not hold the incoming raw L0 live over its replacement.
   An implicit LRA copy to the fixed x0 input used to make this fail with a
   hard-L0 spill even though the kernel needs only a few registers.  */
void config_then_raw_half_loads ()
{
  asm volatile (".ttinsn %0" : : "n" (0x71020001));
  __builtin_rvtt_sfprawlreg_effect (0, 1);
  auto value = __builtin_rvtt_sfpxloadi (0, 0x40000000, 0, 0, -32);
  __builtin_rvtt_sfpwriteconfig_v (value, 0, 12);
  asm volatile (".ttinsn %0" : : "n" (0x710a0504));
  __builtin_rvtt_sfprawlreg_effect (1, 1);
  asm volatile (".ttinsn %0" : : "n" (0x71080000));
  __builtin_rvtt_sfprawlreg_effect (1, 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (0), 1);
}
