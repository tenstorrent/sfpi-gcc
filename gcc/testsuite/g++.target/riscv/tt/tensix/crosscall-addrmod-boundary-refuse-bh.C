// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Call-boundary crossing boundary, refusing side: the block-final
// mod-write's next consumer is the NEXT invocation's first Dst access
// through frontend-draining scalar return/call control; with only the
// return word covering (caller words credited zero), rows <= W_drain -
// cover refuses by name.
// { dg-final { scan-rtl-dump "mod-write-dominates-crosscall-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 6
#include "crosscall-addrmod-body.h"
