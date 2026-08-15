// { dg-options "-mcpu=tt-wh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-rtl-dump-times "descriptor=predicated-three-load-select-store .*modes=2,6 address-mode=3 cc=closed .*templates=7b0000c6,8a0000d0 sequences=13000004,00000005 misc=706 .*emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "predicated-select-reject=encoding-or-format emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "predicated-select-reject=unclosed-dependency emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-predicated-select.inc"
