// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-replay-buffer-size=4 -fdump-rtl-rvtt_replay-details" }
// Check that -mtt-tensix-replay-buffer-size=4 truncates a 6-word repeat to a 4-word capture.
// { dg-final { scan-rtl-dump "Capturing and executing sequence .0,4. 2 instances" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }
#include "record-hoist-body.h"
