// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Transpose-involution formation + Dst-park elision on the parked shape:
// each block's SFPTRANSP fuses with its four full-bank gather loads into
// one companion-preserving bundle (SFPTRANSP / 4x SFPLOAD / SFPTRANSP),
// the accumulator parks forward through registers (the intermediate park
// stores and unpark loads vanish; the final deposit stores stay), and one
// materialized all-lanes SFPENCC covers the whole formation (the function
// entry state is not assumed).
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 6 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 12 } }
#include "transp-involution-body.h"

void involution_fire ()
{
  parked<7, 3, 320, 328, 0, 4, 8>::run ();
}
