// The opt-in flags of the removed exact-calendar pass are rejected with an
// error rather than silently ignored.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-error "removed with the quarantined exact-calendar pass" "" { target *-*-* } 0 }

void empty () {}
