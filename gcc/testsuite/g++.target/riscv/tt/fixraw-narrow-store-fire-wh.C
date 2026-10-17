// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whraw" }
// Check that -mtt-fix-whraw places an annulling load before the next load or at block end.
// { dg-final { scan-assembler-times "lbu\tzero" 2 } }

volatile unsigned char *pc;
volatile unsigned *pw;

unsigned narrow_store_then_load ()
{
  *pc = 5;			// hazard candidate
  return *pw;			// annulling load goes before this load
}

void narrow_store_end_of_block (unsigned char v)
{
  *pc = v;			// annulling load goes at end of block
}
