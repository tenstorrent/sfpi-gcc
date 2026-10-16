// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -fdump-tree-rvtt_combine" }
// Check that without -mtt-tensix-optimize-reassoc-mad-restructure the immediate fold is kept (SFPMULI + SFPADD).
// { dg-final { scan-tree-dump-not "mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-times "SFPADD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_default_off
#include "reassoc-mad-restructure-body.h"
