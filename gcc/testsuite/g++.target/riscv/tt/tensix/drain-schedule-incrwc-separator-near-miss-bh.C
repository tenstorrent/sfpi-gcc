// Near miss: the face advance spelled as a single typed TTINCRWC (delta 7).  The
// INC class is launch-latched-neutral but dst-autoincr may absorb it
// (AIC_INCRWC), so its presence in the final stream is unproven and it
// earns NO slot credit -- the next run's first launched event (the
// Simple-unit template at issue + 1 + delay 1 = distance 2+1 = 3) then
// lands exactly AT the last pending retirement: two staged events at
// one cycle are a race (the cc-restore-store-race precedent, strict
// inequality), so the boundary refuses drain-lreg-overlap and keeps the
// full derived drain after every run: SFPNOP stays 12.  Flip detector
// for the separator-credit derivation (the same source with the
// two-word CR-class advance elides).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-refusal: drain-lreg-overlap" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#define MINMAX_FACE_ADVANCE() __builtin_rvtt_ttincrwc (4, 7, 0, 0)
#include "loadmacro-periodic-minmax-inplace-body.h"
