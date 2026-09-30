// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// QSR has no validated audit of the replay erratum machinery against
// flattened user records; refuse wholesale.
// { dg-final { scan-tree-dump "refused .launch-flatten-qsr-unproven." "rvtt_launch_flatten" } }

#define LF_EXEC 0
#include "launch-flatten-body.h"
