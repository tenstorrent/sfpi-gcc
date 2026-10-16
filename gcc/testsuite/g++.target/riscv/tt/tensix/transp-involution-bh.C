// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Check transpose-involution bundling and Dst-park elision under one all-lanes SFPENCC.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 6 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 12 } }
#include "transp-involution-body.h"

void involution_fire ()
{
  parked<7, 3, 320, 328, 0, 4, 8>::run ();
}
