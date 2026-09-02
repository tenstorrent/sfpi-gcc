// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mno-tt-fix-whbhebreak" }
// Flag-off twin of fixebreak-asm-fire-wh.C: no nops without the
// workaround.
// { dg-final { scan-assembler-not "\tnop" } }

void halt ()
{
  __asm__ __volatile__ ("ebreak");
}
