// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whbhebreak" }
// Wormhole/Blackhole ebreak erratum workaround (-mtt-fix-whbhebreak,
// default on for tt-wh/tt-bh): eight nops are inserted after an
// inline-asm ebreak.
// { dg-final { scan-assembler-times "\tnop" 8 } }

void halt ()
{
  __asm__ __volatile__ ("ebreak");
}
