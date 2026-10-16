// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mno-tt-fix-whraw" }
// Check that no annulling load is emitted with -mno-tt-fix-whraw.
// { dg-final { scan-assembler-not "lbu\tzero" } }

volatile unsigned char *pc;
volatile unsigned *pw;

unsigned narrow_store_then_load ()
{
  *pc = 5;
  return *pw;
}

void narrow_store_end_of_block (unsigned char v)
{
  *pc = v;
}
