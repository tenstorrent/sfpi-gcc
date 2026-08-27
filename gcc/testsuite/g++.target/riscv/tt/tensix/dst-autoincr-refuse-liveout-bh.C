// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The final replayed row has no trailing increment: rewriting the shared
// payload store would advance Dst at that execution site with no explicit
// increment to absorb, changing live-out RWC state unrestorably.  The whole
// payload must refuse and every explicit increment must survive.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: payload execution site without matching increment .live-out RWC state." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 9 } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-not "SFPSTORE\tL., 0, 0, 6" } }

#define DST_MODE 7
#define DST_DROP_LAST_INCREMENT 1
#include "dst-autoincr-replay-body.h"
