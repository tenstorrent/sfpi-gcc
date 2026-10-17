// Check that eight cast-round rows with one all-lanes proof form a single region.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467741696" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-cast-round-group-body.h"
