// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -mtt-tensix-optimize-dst-iteration-fusion -fdump-tree-rvtt_dst_iteration-details -fdump-tree-rvtt_dst_interleave-details" }
// { dg-final { scan-tree-dump-times "Dst-iteration candidate:.*addr-delta=2 final-rwc=4 target=qsr emit=no" 1 "rvtt_dst_iteration" } }
// { dg-final { scan-tree-dump-not "Dst-interleave candidate:" "rvtt_dst_interleave" } }
// { dg-final { scan-assembler-not "TTINCRWC\\t0, 4, 0, 0" } }

#define DST_MODE 7
#define DST_BAD_MODE 6
#define DST_LIMIT 1023
#define DST_QSR 1
#include "dst-iteration-body.h"
