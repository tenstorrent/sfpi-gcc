// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-iteration-fusion -fdump-tree-rvtt_dst_iteration-details" }
// { dg-final { scan-tree-dump-times "Dst-iteration candidate:.*addr-delta=2 final-rwc=4 target=bh emit=yes" 1 "rvtt_dst_iteration" } }
// { dg-final { scan-assembler-times "TTINCRWC\\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL., 2, 0, 7" 1 } }

#define DST_MODE 7
#define DST_BAD_MODE 6
#define DST_LIMIT 8191
#include "dst-iteration-body.h"
