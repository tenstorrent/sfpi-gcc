// Pressure-ladder rung: exactly 16 simultaneously live vector values
// (ring dependence, label machine-checked below).  No constants anywhere:
// the relief flags are ON and still cannot help -- this is pure liveness.
// TODAY: refuses by name (lreg-pressure-exceeded).
// FUTURE-VERDICT (LREG allocator): COMPILE via exact Dst-row spill
// (INT32 format = 32-bit rows, lossless round-trip) and match the
// recorded CRAQ golden bit-exactly (see ARSENAL.md; golden defined by
// the hand-spilled same-DAG twin ladder16_spilled).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: pressure 16 exceeds the 8-LREG file" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "lreg-pressure-unresolvable" "rvtt_prgm_const" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live16
#define LADDER_N 16
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
