// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-rtl-dump-times "SFPLOADMACRO candidate: .*words=4 loads=2 stores=1 emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=unsafe-replay-member" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=dynamic-encoding-unproved" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=external-lreg-livein-unproved" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=lreg-liveout-unproved" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=cc-effect-unproved" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=dst-rwc-effect-unproved" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=subunit-calendar-missing" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=simulator-event-model-missing" 1 "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-analysis-body.h"
