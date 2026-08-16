// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Boundary: the smallest shape with both a formed payload (two identical
// rows) and a renamed final row.
// { dg-final { scan-rtl-dump-times "Converted isomorphic run of 7 insns .bb \[0-9\]+. to launch" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 3 } }
// { dg-final { scan-assembler-times "SFPMUL" 5 } }

#define CONVERT_FN tail_rows_three
#define CONVERT_TRIPS 3
#define CONVERT_ADDR 0
#define CONVERT_MODE 7
#define CONVERT_STRIDE 2
#define CONVERT_K0 0x3e000001
#define CONVERT_K1 0x3e000002
#define CONVERT_K2 0x3e000003
#define CONVERT_K3 0x3e000004
#include "replay-launch-convert-body.h"
