// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mno-tt-fix-whbhebreak" }
// Check that no nops follow an ebreak with -mno-tt-fix-whbhebreak.
// { dg-final { scan-assembler-not "\tnop" } }

void halt ()
{
  __asm__ __volatile__ ("ebreak");
}
