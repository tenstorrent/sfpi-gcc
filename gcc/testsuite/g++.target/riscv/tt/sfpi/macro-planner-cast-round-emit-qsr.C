#include "../tensix/rtl-lane-reset.h"

// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// Stochastic rounding consumes the PRNG, so the loop is not replayed.
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD" 8 } }
// { dg-final { scan-assembler-times "SFPCAST" 8 } }
// { dg-final { scan-assembler-times "SFPSTOCH" 8 } }
// { dg-final { scan-assembler-times "SFPSTORE" 8 } }

#include "macro-planner-cast-round-emit.inc"
