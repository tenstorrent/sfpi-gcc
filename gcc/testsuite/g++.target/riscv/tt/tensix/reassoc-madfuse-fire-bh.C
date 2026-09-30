// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// THE LICENSED MAD FUSION: the product has two consumers (store +
// add), so the default single-use mul+add->mad rule refuses; under the
// license the add fuses to SFPMAD while the mul survives for its store
// -- the add's consumer sees the singly-rounded product, the store the
// doubly-rounded one, exactly the value divergence -fassociative-math
// licenses.  Named dump line; both instructions present.
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
