// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// THE LICENSED MAD RESTRUCTURE, addi arm (the
// trig cert's Newton "mul+addi" stall-pair shape): a loadi'd bf16
// addend on a two-register product that dies into the add.  With BOTH
// license keys the SFPADDI immediate fold is vetoed and the pair fuses
// through the contract mad rule with the kept loadi as the addend.
// { dg-final { scan-tree-dump-times "licensed mad restructure .addi immediate-fold suppressed" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler "SFPLOADI" } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler-not "SFPMUL\t" } }
#define MRB_KERNEL mrb_addi_fire
#define MRB_ARM_ADDI 1
#include "reassoc-mad-restructure-body.h"
