// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// Default-path stability under the FULL license: a SINGLE-use product
// is the sfpi contract's own fusion -- the DEFAULT mul+add->mad rule
// fires exactly as without the license flags, and the licensed rule
// defers (no "reassoc:" line).  The licensed flags never change the
// default path.
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
