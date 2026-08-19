// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Word near-miss pair: a delivered SFPLOADI word whose audited
// destination field (bits 23:20) names an allocatable register (L3)
// refuses; the same word retargeted at a programmable constant
// register (L11, outside the allocatable mask) is inert for the hoist
// and the second kernel fires.
// { dg-final { scan-tree-dump "refused .crossloop-word-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() __asm__ __volatile__ (".ttinsn %0" : : "n" (0x71312345))
#define XLH_KERNEL xlh_nm_refuse_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
#undef XLH_TILE_EXTRA
#undef XLH_KERNEL
#undef XLH_TILES
#undef XLH_T
#undef XLH_ROW
#undef XLH_X
#undef XLH_C0
#undef XLH_C1
#undef XLH_VAL_C0
#undef XLH_VAL_C1
#undef XLH_ADDR_MODE

#define XLH_TILE_EXTRA() __asm__ __volatile__ (".ttinsn %0" : : "n" (0x71b12345))
#define XLH_KERNEL xlh_nm_fire_kernel
#define XLH_TILES tiles2
#define XLH_T t2
#define XLH_ROW row2
#define XLH_X x2
#define XLH_C0 d0
#define XLH_C1 d1
#define XLH_VAL_C0 0x3e11aa55
#define XLH_VAL_C1 0x3f019ce7
#include "crossloop-hoist-body.h"
