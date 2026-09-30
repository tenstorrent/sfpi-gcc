// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Generality: the muli-arm restructure on Wormhole (the immediate-fold
// preference and the mad contract rule are arch-generic; WH mods are
// plain zero).
// { dg-final { scan-tree-dump-times "licensed mad restructure .muli immediate-fold suppressed" 1 "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMULI" } }
#define MRB_KERNEL mrb_fire_wh
#define MRB_ADDR_MODE 3
#include "reassoc-mad-restructure-body.h"
