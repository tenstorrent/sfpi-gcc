// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }
#include "topk-multiresult.inc"
// { dg-final { scan-assembler-times "SFPSWAP\\tL(0|1|2|3), L(0|1|2|3), 8\\t# INDEXED R:L(4|5|6|7),L(4|5|6|7)" 2 } }
// { dg-final { scan-assembler "SFPSWAP\\tL0, L2, 8\\t# INDEXED R:L4,L6" } }
// { dg-final { scan-assembler "SFPSWAP\\tL1, L0, 8\\t# INDEXED R:L5,L4" } }
// { dg-final { scan-assembler-times "SFPTRANSP\\t# R:L0,L1,L2,L3,L4,L5,L6,L7" 1 } }
