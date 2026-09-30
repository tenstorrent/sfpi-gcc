// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Opaque asm between rows: ownership of the address-modifier configuration
// cannot be proven across it on any path, so the region splits and both
// halves are unprofitable.  Byte-identical refusal.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: foreign effect breaks ownership" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 6 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_INTERVENE asm volatile (".ttinsn 0x76000000")
#include "dst-autoincr-refuse-body.h"
