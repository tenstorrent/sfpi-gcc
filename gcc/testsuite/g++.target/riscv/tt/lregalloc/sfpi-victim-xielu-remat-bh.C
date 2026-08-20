// Contrast twin of sfpi-victim-xielu-bh.C: the SAME loop-held-alphas
// body COMPILES once -mtt-tensix-optimize-const-remat is on (the alphas
// and polynomial constants are proven-constant and rematerializable).
// Pins the boundary between constant-pressure (relievable today) and
// computed-value pressure (the allocator's job): the future allocator
// must keep this compiling AND keep the flag-OFF sibling refusing until
// it can spill.
// FUTURE-VERDICT (LREG allocator): compile (unchanged); numeric
// contract = the xielu full2x2 row's existing golden.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-remat" }

#define victim_xielu_loopheld victim_xielu_loopheld_remat
#include "sfpi-victim-xielu-bh.C"
