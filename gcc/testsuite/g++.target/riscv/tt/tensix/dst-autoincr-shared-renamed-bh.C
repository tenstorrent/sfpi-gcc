// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed-equivalent shared placement with varied constants: different
// identifiers, stride, address, and group sizes must not change the
// decision (no name or coefficient recognition).
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 5 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 4 shared config" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 2, 0, 6" 9 } }

#define DST_MODE 7
#define DST_STRIDE 4
#define DST_ADDR 2
#define DST_GROUP1 5
#define DST_GROUP2 4
#define SHARED_ROW emit_quantum
#define SHARED_FN quantum_block
#include "dst-autoincr-shared-body.h"
