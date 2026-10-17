// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whbhebreak" }
// Check that -mtt-fix-whbhebreak inserts eight nops after an inline-asm ebreak.
// { dg-final { scan-assembler-times "\tnop" 8 } }

void halt ()
{
  __asm__ __volatile__ ("ebreak");
}
