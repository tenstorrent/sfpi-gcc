// Residency fire, PRESSURE class: nine out-of-loop proven constants
// over-fill the LREG file; the three free PRGM registers (SFPCONFIG
// dests 12..14) take the highest-use candidates, ranked by use count,
// and the remainder refuses by name.  Three parked constants bring the
// pressure model back inside the file and the compile succeeds without
// remat.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .pressure class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-residency: refused .prgm-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

#define NAME residency_pressure_fire
#define C(N) c##N
#define K(N) (0x3e4b0000u + N)
#define TRIPS 32
#include "const-remat-body.h"
