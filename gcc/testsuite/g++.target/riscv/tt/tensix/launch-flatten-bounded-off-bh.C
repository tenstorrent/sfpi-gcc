// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Without -mtt-tensix-optimize-launch-flatten-bounded the inner loop's
// unknown count refuses as before and the nest is not considered.
// { dg-final { scan-tree-dump "launch-flatten: refused \\(launch-flatten-trip-count-unproven\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump-not "nest\\)" "rvtt_launch_flatten" } }

#define LF_STEPS
#include "launch-flatten-bounded-body.h"
