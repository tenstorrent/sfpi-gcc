// Hand-spilled form of the 9-live pressure kernel: at most 8 values are
// LREG-resident (a6..a8 are parked in scratch Dst rows with exact INT32
// round-trips).  Because the spill is lossless, the output depends only on
// the dataflow, so these results are the reference for any exact
// Dst-spilling allocator applied to the unspilled form.  With at most 8
// values live, this test itself needs no spilling.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LSP_NAME ladder9_spilled
#define LSP_N 9
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "ladder-spilled-body.h"
