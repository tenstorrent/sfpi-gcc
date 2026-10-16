// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "# READ L1" 1 } }
// { dg-final { scan-assembler-times "# READ L2" 2 } }

/* SFPLOADI UPPER/LOWER (mod0 8/10) writes one half of an LREG and keeps the
   other.  Loading the other half next replaces the whole value, so the pair
   needs no reservation back to function entry: only the second load reads
   the first.  */

#define TTI(word) asm volatile (".ttinsn %0" : : "n" (word))

void
full_value_from_halves ()
{
  auto value = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  TTI (0x711a1234);
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x02);
  TTI (0x71185678);
  __builtin_rvtt_sfprawlreg_effect (0x02, 0x02);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

/* Two LOWER loads leave the upper half of the old value in place.  */
void
same_half_twice ()
{
  auto value = __builtin_rvtt_sfpload (0, 0, 0, 0, 0, 0);
  TTI (0x712a1234);
  __builtin_rvtt_sfprawlreg_effect (0x04, 0x04);
  TTI (0x712a5678);
  __builtin_rvtt_sfprawlreg_effect (0x04, 0x04);
  __builtin_rvtt_sfpwritelreg (value, 0);
}
