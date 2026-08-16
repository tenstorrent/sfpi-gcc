// Encodability refusal direction: a nonzero stochastic-round immediate
// has no encodable home in the 0x8e template layout (its fields differ
// above bit 12), so the region refuses by name and the explicit rows
// stay byte-identical.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "descriptor-encoding-failed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// (Replay formation compresses the identical explicit rows, so scan
// for presence, not row count.)
// { dg-final { scan-assembler "SFPSTOCHRND" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#define CAST_ROUND_IMM8 21
#include "macro-planner-cast-round-group-body.h"
