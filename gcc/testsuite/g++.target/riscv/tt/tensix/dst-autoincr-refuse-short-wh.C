// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Six rows transform on Blackhole but not on Wormhole, whose dual-slot
// configuration costs six words: the per-target break-even falls out of the
// capability table and the cost model.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 6 >= removed 6" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 6 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ROWS 6
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
