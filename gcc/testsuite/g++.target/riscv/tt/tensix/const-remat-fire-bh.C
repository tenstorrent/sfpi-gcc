// Check that -mtt-tensix-optimize-const-remat rematerializes loop-held constants that exceed the LREG file.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: pressure \\d+ exceeds the 8-LREG file" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: rematerialized " "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: pressure resolved: \\d+ -> \\d+" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

#define NAME remat_pressure_fire
#define C(N) c##N
#define K(N) (0x3e4b0000u + N)
#define TRIPS 32
#include "const-remat-body.h"
