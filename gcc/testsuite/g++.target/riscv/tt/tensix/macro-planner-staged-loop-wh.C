// WP8 signbit loop parity, Wormhole: dual-slot owned SETC16 program
// (physical slots 2 and 6 -- the launch's two-bit selector can map to
// either bank), preheader config, one launch per row, drain 3.
// Byte-identical to the quarantined oracle (staged-loop wh).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987065344" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987982850" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989621248" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467332096" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-staged-loop-body.h"
