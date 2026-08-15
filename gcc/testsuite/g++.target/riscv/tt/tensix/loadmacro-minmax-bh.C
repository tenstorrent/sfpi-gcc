// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-rtl-dump-times "descriptor=periodic-load-load-swap-store .*resources=load,load,simple.mad-write,store target=bh-v3 target-encoding=lregind-address calendar=missing emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "descriptor-reject=dynamic-encoding emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "descriptor-reject=unclosed-dependency emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=subunit-calendar-missing" 3 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=simulator-event-model-missing" 3 "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-minmax-body.h"
