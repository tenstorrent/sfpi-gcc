// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }
// { dg-final { scan-assembler-times "SFPLOAD" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }
// { dg-final { scan-assembler-times "SFPSTOCH" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "macro-planner-cast-round-emit.inc"
