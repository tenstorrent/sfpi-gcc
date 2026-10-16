// Check that the signbit loop forms with its configuration in the preheader and one launch per row.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-staged-loop-body.h"
