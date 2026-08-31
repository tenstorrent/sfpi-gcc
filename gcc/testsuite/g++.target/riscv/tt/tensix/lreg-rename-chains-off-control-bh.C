// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay" }
// Default-off control: without the flag the pass is gated off and the
// storage collisions stand (byte-inertness is proven corpus-wide; this
// twin pins the gate).
// { dg-final { scan-assembler-not "Lreg chain" } }
#define RENC_FN renc_ctl
#define RENC_TRIPS 20
#define RENC_K1 k1
#define RENC_K2 k2
#define RENC_X x
#define RENC_T t
#define RENC_P p
#define RENC_R r
#define RENC_U u
#define RENC_T2 t2
#define RENC_R2 r2
#define RENC_U2 u2
#define RENC_R3 r3
#define RENC_ROW row
#define RENC_CONSUME(a, b) __builtin_rvtt_sfpxor ((a), (b))
#include "lreg-rename-chains-body.h"
