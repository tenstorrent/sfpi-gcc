// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whraw" }
// Wormhole read-after-write hazard workaround (-mtt-fix-whraw, default
// on for tt-wh): after a byte/half store, a dummy volatile load of the
// stored location (into x0) is sunk as late as possible -- before the
// next real load, or at the end of the block -- forcing the store to
// drain.  Both placements fire here.
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
