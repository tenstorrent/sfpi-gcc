// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_reassoc" }
// R2 window near miss: a CC writer AT THE WINDOW'S OWN FRAME DEPTH (a
// bare SFPENCC, no enclosing frame) moves the lane-enable state
// between the original link positions and the rewrite point -- the
// tree maps it to the window's own frame, not a child, so even under
// the stage-B flag the chain refuses BY NAME.
// { dg-final { scan-tree-dump-times "reassoc-cc-region-boundary" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }
#define RA_KERNEL ra_ambient_cc_general
#define RA_N 4
#define RA_MID() __builtin_rvtt_sfpencc (0, 10)
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
