// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Check transpose-involution with other addresses and the bit-exact INT32-class park format.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 6 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 12 } }
#include "transp-involution-body.h"

void involution_varied ()
{
  parked<7, 4, 512, 520, 32, 36, 40>::run ();
}
