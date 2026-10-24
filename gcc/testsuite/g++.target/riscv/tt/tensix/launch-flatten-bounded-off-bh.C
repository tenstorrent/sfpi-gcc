// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Check that without -mtt-tensix-optimize-launch-flatten-bounded the unknown inner count refuses and no nest is requested.
// { dg-final { scan-tree-dump "launch-flatten: refused \\(launch-flatten-trip-count-unproven\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump-not "nest\\)" "rvtt_launch_flatten" } }

#define LF_STEPS
#include "launch-flatten-bounded-body.h"
