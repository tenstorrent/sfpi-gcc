// WP8: converted from the quarantined pass to the generic macro
// planner; the emission is byte-identical (oracles/wp8-oracle-manifest,
// cast-round). .ttinsn = 3 owned SETC16 + 8 alternating launches.
// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 11 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTOCH" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-cast-round-emit.inc"
