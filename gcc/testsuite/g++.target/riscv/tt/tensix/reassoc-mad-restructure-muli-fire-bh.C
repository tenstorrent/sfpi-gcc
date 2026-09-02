// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// THE LICENSED MAD RESTRUCTURE, muli arm (FABLE_GOES_BURR R3 -- the
// trig cert's named "muli+add -> fused mad" successor): a loadi'd
// bf16 multiplicand whose product dies into an add.  With BOTH license
// keys the SFPMULI immediate fold is vetoed and the pair fuses through
// the single-use mul+add->SFPMAD contract rule: one partially-fused
// rounding instead of two serial MAD-subunit roundings, word-neutral.
// { dg-final { scan-tree-dump-times "licensed mad restructure .muli immediate-fold suppressed" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPADD\t" } }
#define MRB_KERNEL mrb_muli_fire
#include "reassoc-mad-restructure-body.h"
