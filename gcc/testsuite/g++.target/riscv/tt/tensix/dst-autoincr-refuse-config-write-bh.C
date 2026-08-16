// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// An address-modifier configuration write hidden in opaque asm intervenes
// between rows.  The pass never inspects asm content: opacity alone refuses
// the path, which is exactly what makes the intervening SETC16 safe.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: foreign effect breaks ownership" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 6 } }
// { dg-final { scan-assembler-not "TTSETC16\t" } }

#define DST_INTERVENE asm volatile (".ttinsn 0xc8880002")
#include "dst-autoincr-refuse-body.h"
