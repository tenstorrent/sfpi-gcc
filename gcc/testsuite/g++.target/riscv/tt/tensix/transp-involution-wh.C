// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Wormhole twin of transp-involution-bh.C (no-increment address mode 3).
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 6 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 12 } }
#include "transp-involution-body.h"

void involution_fire_wh ()
{
  parked<3, 3, 320, 328, 0, 4, 8>::run ();
}
