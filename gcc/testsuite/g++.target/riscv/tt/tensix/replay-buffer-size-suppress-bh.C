// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-replay-buffer-size=1 -fdump-rtl-rvtt_replay-details" }
// Check that -mtt-tensix-replay-buffer-size=1 admits no profitable capture, so no replay forms.
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
#include "record-hoist-body.h"
