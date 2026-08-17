// Wormhole in-place store: the store-demotion fallback re-derives the
// frozen calendar (single-slot Base=1 SETC16, three raw config words --
// regs 19/29/54; the base-0 bank 11/25/50 is LLK's live ADDR_MOD_2 and
// must never be written, sfpi-gcc 2a0ba1e6602); store launch word
// 2473623552 carries the in-place address 0.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987065344" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987982850" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2989621248" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299904" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473623552" 8 } }
// { dg-final { scan-assembler-times {SFPLOADI\tL0, 705, 2} 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 8 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#include "loadmacro-periodic-minmax-inplace-body.h"
