// Check that pressure-class residency fires with renamed variables and different constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .pressure class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

#define NAME quarry_signal_ledger
#define C(N) moss_##N
#define K(N) (0x41200000u ^ (0x35b1 * (N + 3)))
#define TRIPS 9
#define CONST_RESIDENCY_ENTRY_PROLOGUE 1
#include "const-remat-body.h"
