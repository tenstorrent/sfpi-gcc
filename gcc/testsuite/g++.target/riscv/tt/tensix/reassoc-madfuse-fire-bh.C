// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// Check that under the reassociation license a multi-use product's add fuses to SFPMAD while the mul is kept for its store.
// { dg-final { scan-tree-dump-times "reassoc: licensed mad-fuse of multi-use mul" 1 "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-times "SFPMUL\t" 1 } }
// { dg-final { scan-assembler-not "SFPADD\t" } }
#define RMF_KERNEL rmf_fire
#define RMF_A a
#define RMF_B b
#define RMF_C c
#define RMF_P p
#define RMF_R r
#include "reassoc-madfuse-body.h"
