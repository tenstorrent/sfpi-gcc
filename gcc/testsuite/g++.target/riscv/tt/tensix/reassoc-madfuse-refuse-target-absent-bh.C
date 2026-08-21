// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -fdump-tree-rvtt_combine" }
// CRITICAL REFUSAL, other half: the -fassociative-math license alone does not
// license the multi-use fusion -- without -mtt-tensix-optimize-reassoc
// the pattern is disabled, no SFPMAD forms, mul+add survive.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
// { dg-final { scan-assembler-times "SFPMUL\t" 1 } }
// { dg-final { scan-assembler-times "SFPADD\t" 1 } }
#define RMF_KERNEL rmf_target_absent
#define RMF_A a
#define RMF_B b
#define RMF_C c
#define RMF_P p
#define RMF_R r
#include "reassoc-madfuse-body.h"
