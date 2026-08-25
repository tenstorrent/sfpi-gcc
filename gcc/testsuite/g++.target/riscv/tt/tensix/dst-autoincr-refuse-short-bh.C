// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Three rows cannot pay for the three-word Blackhole configuration: the
// break-even falls out of the cost model, not a row threshold.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 3 >= removed 3" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 3 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ROWS 3
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
