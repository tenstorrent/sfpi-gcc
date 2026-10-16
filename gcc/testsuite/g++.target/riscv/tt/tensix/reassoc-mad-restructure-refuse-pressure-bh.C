// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Check that the MAD restructure refuses when the pair window is at the 8-LREG limit (reassoc-pressure-budget-exceeded).
// { dg-final { scan-tree-dump-times "reassoc-pressure-budget-exceeded" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "licensed mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_pressure
#define MRB_PRESSURE 1
#include "reassoc-mad-restructure-body.h"
