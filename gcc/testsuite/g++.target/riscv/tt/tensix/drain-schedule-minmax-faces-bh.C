// Drain-aware boundary placement on the production minmax shape:
// rows=32 runs=4 in-place face runs separated by the typed architectural
// face advance.  Every intra-region boundary proves -- the separators are
// launch-latched pure-RWC words of the never-absorbed AIC_RWC_STEP class
// (two words of slot credit), and every next-run event's derived access
// slot (carrier position + SequenceBits delay) strictly clears the
// 3-slot derived drain -- so the drain executes once, at the region's
// exit contract: SFPNOP 12 -> 3.  Everything else is byte-identical to
// loadmacro-periodic-minmax-inplace-faces-bh.C (launch words, prefix,
// separators retained).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 67 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473639936" 32 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "drain-boundary: drain=3 separator-credit=2 words-per-row=3" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=32 runs=4 drain-elided" 1 "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
