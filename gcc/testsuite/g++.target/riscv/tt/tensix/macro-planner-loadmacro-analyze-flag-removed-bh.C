// WP8: the quarantined exact-calendar pass's opt-in flags were removed
// and error on use rather than silently doing nothing.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro" }
// { dg-error "removed with the quarantined exact-calendar pass" "" { target *-*-* } 0 }

void empty () {}
