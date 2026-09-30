// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// THE CRITICAL REFUSAL: the token alone is HALF the key.  Without
// -fassociative-math the muli-arm candidate refuses BY NAME
// (associative-math-license-absent) and the immediate fold proceeds
// byte-identically to the token being absent: SFPMULI + SFPADD, no
// SFPMAD.
// { dg-final { scan-tree-dump-times "associative-math-license-absent" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "licensed mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_license_absent
#include "reassoc-mad-restructure-body.h"
