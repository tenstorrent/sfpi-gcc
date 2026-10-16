// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Check that without -mtt-tensix-optimize-rename-temporal no interior rename is requested.
// { dg-final { scan-rtl-dump-not "List-schedule \\(interior-rename\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename" "rvtt_schedule" } }
#define IRN_FN irn_control
#include "interior-rename-body.h"
