// Hand-spilled same-DAG twin of raw-ladder10-bh.C: at most 8 values
// LREG-resident (a6..a9 parked in scratch Dst rows with exact INT32
// round-trips).  COMPILES TODAY and defines the rung's CRAQ golden:
// any correct exact-only Dst-spilling allocator must make the 10-live
// rung produce these exact bits (lossless spill => output depends only
// on the DAG).  Also a member of the no-op byte-gate set: its own
// codegen must not change under the allocator (max-live 8).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LSP_NAME ladder10_spilled
#define LSP_N 10
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "ladder-spilled-body.h"
