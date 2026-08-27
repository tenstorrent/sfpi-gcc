// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole mirror of the per-execution slot-pricing boundary (same audited
// constants: one owned slot, three SETC16 words at the two-cycle
// configuration class, min_config_distance 2): removed 8 <= 8 refuses and
// keeps the explicit rows byte-identically.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config.entry slots 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ROWS 8
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
