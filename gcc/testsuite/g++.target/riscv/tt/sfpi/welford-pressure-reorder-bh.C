// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure schedule:.*old-peak=9.*new-peak=8.*validated=yes.*reason=ok.*rejection-selftest=passed.*applied=yes" "rvtt_lp_schedule" } }

#include "welford-pressure-reorder-wh.C"
