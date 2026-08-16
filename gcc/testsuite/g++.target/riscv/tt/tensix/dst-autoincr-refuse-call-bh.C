// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// A call between rows: the callee may own or overwrite the modifier slots,
// so ownership cannot be proven on that path.  Byte-identical refusal.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: foreign effect breaks ownership" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 6 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

extern "C" void external_state ();
#define DST_INTERVENE external_state ()
#include "dst-autoincr-refuse-body.h"
