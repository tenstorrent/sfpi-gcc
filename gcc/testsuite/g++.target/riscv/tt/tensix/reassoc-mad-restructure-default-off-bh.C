// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -fdump-tree-rvtt_combine" }
// DEFAULT-OFF control (Init(0) byte-inertness): the same muli-arm pair
// with the FULL -fassociative-math triple but WITHOUT the token keeps
// the historical immediate-fold output -- SFPMULI + SFPADD, no SFPMAD,
// no restructure or refusal line.  The token, not the generic FP
// license, owns the mechanism (one-knob-one-mechanism).
// { dg-final { scan-tree-dump-not "mad restructure" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-times "SFPADD\t" 1 } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_default_off
#include "reassoc-mad-restructure-body.h"
