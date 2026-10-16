// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Check that under both license keys the SFPMULI immediate fold is vetoed and the muli+add pair fuses to SFPMAD.
// { dg-final { scan-tree-dump-times "licensed mad restructure .muli immediate-fold suppressed" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPADD\t" } }
#define MRB_KERNEL mrb_muli_fire
#include "reassoc-mad-restructure-body.h"
