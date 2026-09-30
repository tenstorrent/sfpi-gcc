// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-replay-buffer-size=4 -fdump-rtl-rvtt_replay-details" }
// -mtt-tensix-replay-buffer-size bounds the capture length: the shared
// re-record shape's 6-word repeat (captured whole under the default
// 32-word buffer) is truncated to a 4-word capture under =4.
// { dg-final { scan-rtl-dump "Capturing and executing sequence .0,4. 2 instances" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }
#include "record-hoist-body.h"
