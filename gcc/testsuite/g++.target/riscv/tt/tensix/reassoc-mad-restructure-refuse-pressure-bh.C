// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Pressure near-miss (the item-#10 engine budget, windowed form):
// seven extra vector values live ACROSS the pair take the
// pair-window peak to exactly the 8-LREG file (the kernel itself
// still allocates at eight), so keeping the loadi live to the mad has
// no headroom -- the candidate refuses BY NAME
// (reassoc-pressure-budget-exceeded), the immediate fold proceeds,
// and the kernel keeps compiling exactly as before (a licensed
// transform must never make a compilable kernel uncompilable).
// { dg-final { scan-tree-dump-times "reassoc-pressure-budget-exceeded" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "licensed mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_pressure
#define MRB_PRESSURE 1
#include "reassoc-mad-restructure-body.h"
