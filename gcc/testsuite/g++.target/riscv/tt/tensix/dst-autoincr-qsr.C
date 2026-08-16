// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// QSR has no Dst auto-increment capability entry: the pass must refuse and
// keep every explicit increment.
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 4 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ROWS 4
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
