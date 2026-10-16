// Check that the pressure model reports the 8-live ring as within the LREG file.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: pressure 8 within the 8-LREG file" "rvtt_prgm_const" } }

#define LADDER_NAME ladder_live8_label
#define LADDER_N 8
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
