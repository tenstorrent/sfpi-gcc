// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// A delivered SFPCONFIG word in the tile loop could rewrite a
// programmable constant register or the LaneConfig lane-enable state;
// the audited hoist-region discipline refuses it outright.
// { dg-final { scan-tree-dump "refused .crossloop-config-word-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() __asm__ __volatile__ (".ttinsn %0" : : "n" (0x91000071))
#define XLH_KERNEL xlh_config_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
