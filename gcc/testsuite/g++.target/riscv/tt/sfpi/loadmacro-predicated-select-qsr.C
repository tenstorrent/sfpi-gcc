// { dg-options "-mcpu=tt-qsr32-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-rtl-dump-not "descriptor=predicated-three-load-select-store" "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-predicated-select.inc"
