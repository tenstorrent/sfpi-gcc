// Check that the 9-value ring with a6..a8 spilled by hand to Dst rows compiles.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LSP_NAME ladder9_spilled
#define LSP_N 9
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "ladder-spilled-body.h"
