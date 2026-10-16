// Check that the loop-held-alphas xielu body compiles with -mtt-tensix-optimize-const-remat.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-remat" }

#define pressure_xielu_loopheld pressure_xielu_loopheld_remat
#include "sfpi-pressure-xielu-bh.C"
