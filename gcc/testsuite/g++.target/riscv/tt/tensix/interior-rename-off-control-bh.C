// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Control for the interior-rename fire twin: the IDENTICAL TU without
// -mtt-tensix-optimize-rename-temporal.  The consumer is gated off;
// no rename is ever requested and no interior-rename line prints
// (byte-inertness is proven corpus-wide; this twin pins the gate).
// { dg-final { scan-rtl-dump-not "List-schedule \\(interior-rename\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename" "rvtt_schedule" } }
#define IRN_FN irn_control
#include "interior-rename-body.h"
