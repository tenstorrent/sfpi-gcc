// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lp-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*ops=16.*live-in=7.*peak=8" "rvtt_lp_schedule" } }

#include "welford-pressure-wh.C"
