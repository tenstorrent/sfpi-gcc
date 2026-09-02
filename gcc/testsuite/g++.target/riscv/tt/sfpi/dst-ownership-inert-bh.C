// Track B (18.9 B2 gate): with the flag off the registered pass runs its
// analysis, reports the provable identity, and mutates nothing -- the
// stream keeps both loads byte-identically.
// Default-ON promotion (hardware-validated: addcmul -10.9% vs hand,
// erfinv fold bit-exact): this inertness gate used to rely on the
// ambient default being off; it now pins the -mno- spelling so the
// analysis-only path stays covered.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "provable identity reload" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "transform disabled" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
inert_reload ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 0.30102999f + 1.25f;
  vFloat in2 = dst_reg[0];
  dst_reg[0] = copysgn (r, in2);
}
