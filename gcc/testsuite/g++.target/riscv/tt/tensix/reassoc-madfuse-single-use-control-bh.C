// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// Check that a single-use product still fuses through the default mul+add->mad rule and the licensed rule stays silent.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMUL\t" } }
// { dg-final { scan-assembler-not "SFPADD\t" } }
#define RMF_SINGLE_USE 1
#define RMF_KERNEL rmf_single_use
#define RMF_A a
#define RMF_B b
#define RMF_C c
#define RMF_P p
#define RMF_R r
#include "reassoc-madfuse-body.h"
