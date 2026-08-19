// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Renamed + varied twin of the census repro: a differently-named
// extern "C" entry (the firmware->run_kernel shape -- no `main' at
// all), different coefficient values, runtime trip count, and the
// slot SFPLOADI destination moved to another CONTRACT register (L5).
// Rooting must key on external visibility, never on the entry's name;
// the refusal must be identical.
// { dg-final { scan-tree-dump "refused .crosscall-caller-mop-slot-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x20" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

#define CCH_TMPL zq_program
#define CCH_CALLEE zq_tile_op
#define CCH_CALLER zq_tile_walk
#define CCH_OUTER zo
#define CCH_INNER zi
#define CCH_TILES zn
#define CCH_T zt
#define CCH_ROW zr
#define CCH_X zx
#define CCH_R zv
#define CCH_A0 q0
#define CCH_A1 q1
#define CCH_A2 q2
#define CCH_B0 p0
#define CCH_B1 p1
#define CCH_B2 p2
#define CCH_VAL_A0 0x3f000000
#define CCH_VAL_A1 0xbe200000
#define CCH_VAL_A2 0x3d99999a
#define CCH_VAL_B0 0x40490fdb
#define CCH_VAL_B1 0x3fb504f3
#define CCH_VAL_B2 0xbf317218
#define CCH_SLOT_WORD 0x7150abcd
#define CCH_ENTRY_DEF extern "C" void zq_kernel_entry (int zn) { zq_program (6, 3); zq_tile_walk (zn); }
#include "crosscall-hoist-census-body.h"
