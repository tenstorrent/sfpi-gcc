// Named refusal (crosslane-kv32-register-file, lane FG X5): a
// 32-element key-value machine needs 16 live registers -- twice the
// LReg file -- so the library refuses at compile time and points at
// bitonic_sort16_kv or the packed-index (P19) spelling.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void want_kv32 ()
{
  vFloat k[8]; vUInt p[8];
  bitonic_sort32_kv<SortOrder::Ascending> (k, p);
}

// { dg-error "crosslane-kv32-register-file" "" { target *-*-* } 0 }
// { dg-prune-output "static assertion failed" }
// { dg-prune-output "In instantiation" }
