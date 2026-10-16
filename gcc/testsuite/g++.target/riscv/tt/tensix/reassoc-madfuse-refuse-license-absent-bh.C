// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// Check that the multi-use MAD fusion does not happen without -fassociative-math.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
// { dg-final { scan-assembler-times "SFPMUL\t" 1 } }
// { dg-final { scan-assembler-times "SFPADD\t" 1 } }
#define RMF_KERNEL rmf_license_absent
#define RMF_A a
#define RMF_B b
#define RMF_C c
#define RMF_P p
#define RMF_R r
#include "reassoc-madfuse-body.h"
