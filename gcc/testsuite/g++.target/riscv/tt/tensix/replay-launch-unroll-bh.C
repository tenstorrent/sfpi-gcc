// Twenty proven trips -- beyond the gimple replay-unroll request's
// structural bound, so only the post-hoist launch-loop unroll can remove
// the backedge: capture hoisted to the preheader, then 20 back-to-back
// {playback, typed Dst step} pairs and no scalar loop control.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 20 trips x 2 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 21 } }
// { dg-final { scan-assembler-times "TTINCRWC" 20 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "replay-launch-unroll-body.h"
