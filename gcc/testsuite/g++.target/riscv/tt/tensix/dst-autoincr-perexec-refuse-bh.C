// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Per-execution slot-pricing boundary, refusing side (hardware
// witness: binopscalar-fresh, pin 35): an eight-row straight-line group
// re-executes its three-SETC16 program on every entry, each word occupying
// the audited two-cycle configuration issue class plus the once-per-entry
// drain residual -- removed 8 <= 3*2 + 2 = 8.  Silicon measured the fired
// form at +3.61% kernel (~1.5 cycles per invocation); the refusal keeps
// the audited TTINCRWC rows byte-identically.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config.entry slots 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 8 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ROWS 8
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
