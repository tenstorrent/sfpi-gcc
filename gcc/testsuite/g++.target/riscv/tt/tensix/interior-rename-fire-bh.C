// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mtt-tensix-optimize-rename-temporal -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Check that the cyclic-interior scheduler renames a colliding chain and commits the interleave.
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): chain L\\d+ -> L\\d+ at uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): committed \\d+ chain rename" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(cyclic-interior\\): bb \\d+ region at uid=\\d+ nodes=\\d+ row II \\d+ -> \\d+ target=bh" "rvtt_schedule" } }
#define IRN_FN irn_fire
#include "interior-rename-body.h"
