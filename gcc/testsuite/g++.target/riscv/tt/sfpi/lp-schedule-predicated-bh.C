// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lp-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*rejected=cc-epoch" "rvtt_lp_schedule" } }
// { dg-final { scan-tree-dump-not "applied=yes" "rvtt_lp_schedule" } }

#include "lp-schedule-predicated-wh.C"
