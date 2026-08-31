// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-early-inlining -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Item #15 (rvtt-ipa-summary): the ADDR_MOD contract's callee reached
// through a committed-inline wrapper hop.  The hop body's epoch scan is
// summary-fed -- one init-face digest, built once and replayed against
// this contract's parameters -- and the contract fires exactly as the
// direct-call shape does: the three-SETC16 slot program hoists to the
// caller's loop entry, the callee's group prices at zero per-call
// configuration cost.
// { dg-final { scan-rtl-dump "ipa-summary: init-face digest built \\(void wrap\\(\\)/\\d+, 1 events\\)" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_NO_CALLER
#include "crosscall-addrmod-body.h"

static void wrap () { cam_callee (); }

void
cam_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    wrap ();
}
