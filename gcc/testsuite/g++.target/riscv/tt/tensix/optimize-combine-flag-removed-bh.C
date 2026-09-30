// The Tensix instruction-combine flag was a dead knob with no consumer:
// both the enable and the -mno- form silently did nothing.  It was removed
// and now errors on use rather than mislead.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-combine" }
// { dg-error "-mtt-tensix-optimize-combine.* was removed; it had no effect" "" { target *-*-* } 0 }

void empty () {}
