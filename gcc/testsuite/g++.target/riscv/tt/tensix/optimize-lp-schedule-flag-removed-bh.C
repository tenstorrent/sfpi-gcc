// -mtt-tensix-optimize-lp-schedule was a historical Undocumented alias
// with no in-tree or external consumer.  It was removed and now errors on
// use rather than silently forward to the pressure scheduler.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lp-schedule" }
// { dg-error "-mtt-tensix-optimize-lp-schedule.* was removed" "" { target *-*-* } 0 }

void empty () {}
