// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-rtl-dump-times "descriptor=periodic-load-load-swap-store .*resources=load,load,simple.mad-write,store target=wh-v2 target-encoding=lregind-address calendar=missing emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "descriptor-reject=dynamic-encoding emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "descriptor-reject=unclosed-dependency emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-minmax-body.h"
