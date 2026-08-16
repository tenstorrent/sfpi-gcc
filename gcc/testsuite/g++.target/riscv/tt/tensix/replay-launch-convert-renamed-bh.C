// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Renamed-equivalent with varied constants, address, mode, stride, and trip
// count: the conversion keys on structure only.
// { dg-final { scan-rtl-dump-times "Converted isomorphic run of 7 insns .bb \[0-9\]+. to launch" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 6 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 6 } }
// { dg-final { scan-assembler-times "SFPMUL" 5 } }

#define CONVERT_FN quantum_flux_rows
#define CONVERT_TRIPS 6
#define CONVERT_ADDR 2
#define CONVERT_MODE 7
#define CONVERT_STRIDE 4
#define CONVERT_K0 0x3d80000b
#define CONVERT_K1 0x3d80000c
#define CONVERT_K2 0x3d80000d
#define CONVERT_K3 0x3d80000e
#include "replay-launch-convert-body.h"
