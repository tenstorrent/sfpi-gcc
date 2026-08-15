// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mtt-tensix-optimize-dst-iteration-fusion -fdump-tree-rvtt_dst_iteration-details -fdump-tree-rvtt_dst_interleave-details" }
// { dg-final { scan-tree-dump-times "Dst-iteration candidate:.*addr-delta=2 final-rwc=4 target=wh emit=yes" 1 "rvtt_dst_iteration" } }
// { dg-final { scan-tree-dump-times "Dst-interleave candidate:.*ops=8 loads=4 dynamic-pairs=1 target=wh emit=yes" 1 "rvtt_dst_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL., 2, 0, 3" 1 } }

#define DST_MODE 3
#define DST_BAD_MODE 2
#define DST_LIMIT 16383
#include "dst-iteration-body.h"
