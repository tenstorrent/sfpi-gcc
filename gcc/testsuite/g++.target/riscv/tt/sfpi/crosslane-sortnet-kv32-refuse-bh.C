// Check that a 32-element key-value sort network is rejected (crosslane-kv32-register-file).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
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
