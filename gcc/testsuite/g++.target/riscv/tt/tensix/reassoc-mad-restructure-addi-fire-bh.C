// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Check that under both license keys the SFPADDI immediate fold is vetoed and the mul+addi pair fuses to SFPMAD.
// { dg-final { scan-tree-dump-times "licensed mad restructure .addi immediate-fold suppressed" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler "SFPLOADI" } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler-not "SFPMUL\t" } }
#define MRB_KERNEL mrb_addi_fire
#define MRB_ARM_ADDI 1
#include "reassoc-mad-restructure-body.h"
