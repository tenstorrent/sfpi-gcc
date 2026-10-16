// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Check that the MAD restructure refuses by name without -fassociative-math (associative-math-license-absent).
// { dg-final { scan-tree-dump-times "associative-math-license-absent" 1 "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "licensed mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_license_absent
#include "reassoc-mad-restructure-body.h"
