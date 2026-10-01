// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Converted isomorphic run of 12 insns .bb \[0-9\]+. to launch" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-times "SFPMUL" 5 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#define CONVERT_FN tail_rows
#define CONVERT_TRIPS 8
#define CONVERT_ADDR 0
#define CONVERT_MODE 3
#define CONVERT_STRIDE 2
#define CONVERT_K0 0x3e000001
#define CONVERT_K1 0x3e000002
#define CONVERT_K2 0x3e000003
#define CONVERT_K3 0x3e000004
#include "replay-launch-convert-body.h"
