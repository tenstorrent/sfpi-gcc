// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mtt-tensix-optimize-rename-temporal -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// R1 cyclic-interior rename consumer FIRE: a Dst row loop chopped by
// CC barrier words whose interior region carries a storage collision
// -- the allocator packs the serial chain's dying lifetime into the
// independent pair's register, and the false WAW/WAR serializes the
// interleave.  The consumer requests the chain rename through the
// the du-chain rename engine service before candidate generation; the composition
// commits on the established strict whole-row steady-state II
// decrease.  The sibling control twin pins the same TU without the
// flag never printing an interior-rename line.
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): chain L\\d+ -> L\\d+ at uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(interior-rename\\): committed \\d+ chain rename" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(cyclic-interior\\): bb \\d+ region at uid=\\d+ nodes=\\d+ row II \\d+ -> \\d+ target=bh" "rvtt_schedule" } }
#define IRN_FN irn_fire
#include "interior-rename-body.h"
