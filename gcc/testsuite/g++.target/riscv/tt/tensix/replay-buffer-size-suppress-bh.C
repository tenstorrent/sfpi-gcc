// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-replay-buffer-size=1 -fdump-rtl-rvtt_replay-details" }
// Twin of replay-buffer-size-truncate-bh.C: a 1-word buffer admits no
// profitable capture at all, so no replay forms.
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
#include "record-hoist-body.h"
