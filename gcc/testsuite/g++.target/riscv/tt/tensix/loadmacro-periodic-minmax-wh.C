// Wormhole frozen minmax calendar: the owned SETC16 program is the
// SINGLE Base=1 slot (scratch modifier 2 under the pinned
// ADDR_MOD_SET_Base=1 = physical slot 6, regs 19/29/54 -> raw words
// 0xb2130000/0xb21d0002/0xb2360000 = 2987589632/2988244994/2989883392).
// The base-0 bank words 0xb20b0000/0xb2190002/0xb2320000 (regs 11/25/50
// = LLK's live ADDR_MOD_2) must never be emitted: the dual-slot program
// corrupted every tile after the first (sfpi-gcc 2a0ba1e6602;
// laneAJ-evidence-20260817).  Total .ttinsn drops 22 -> 19 (three fewer
// config words).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987065344" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987982850" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2989621248" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 8 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#include "loadmacro-periodic-minmax-body.h"
