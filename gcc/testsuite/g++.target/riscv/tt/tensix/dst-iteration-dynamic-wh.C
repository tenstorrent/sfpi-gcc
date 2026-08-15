// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fchecking=2 -mtt-tensix-optimize-dst-iteration-fusion -fdump-tree-rvtt_dst_iteration-details -fdump-tree-rvtt_dst_interleave-details" }
// { dg-final { scan-tree-dump-times "Dst-iteration candidate:.*target=wh emit=yes" 1 "rvtt_dst_iteration" } }
// { dg-final { scan-tree-dump-times "Dst-interleave candidate:.*target=wh emit=yes scalar-defs=retained" 1 "rvtt_dst_interleave" } }

#define DST_MODE 3
#define DST_BAD_MODE 2
#define DST_LIMIT 16383
#define DST_INELIGIBLE_ONLY 1
#define DST_DYNAMIC 1
#include "dst-iteration-body.h"
