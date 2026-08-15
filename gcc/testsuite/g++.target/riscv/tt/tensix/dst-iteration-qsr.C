// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -mtt-tensix-optimize-dst-iteration-fusion -fdump-tree-rvtt_dst_iteration-details" }
// { dg-final { scan-tree-dump-times "Dst-iteration candidate:.*addr-delta=2 final-rwc=4 target=qsr emit=no" 1 "rvtt_dst_iteration" } }

#define DST_MODE 7
#include "dst-iteration-body.h"
