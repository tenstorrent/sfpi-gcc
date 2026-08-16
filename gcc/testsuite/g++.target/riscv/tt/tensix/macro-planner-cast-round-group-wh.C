// WP8 cast-round parity, Wormhole (dual-slot owned SETC16 program).
// Byte-identical to the quarantined oracle (cast-round wh).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987065344" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987982850" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989621248" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466676736" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467725312" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-cast-round-group-body.h"
