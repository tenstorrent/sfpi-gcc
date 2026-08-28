// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Cross-call ADDR_MOD contract FIRE (lane IK): the binopscalar shape --
// an eight-row straight-line callee whose group refuses by the lane IA
// per-execution pricing (removed 8 <= 3*2 + 2) -- fires under the
// contract: the three-SETC16 slot program hoists to the caller's loop
// entry (once per kernel), the callee's group prices at ZERO per-call
// configuration cost, and the callee emits mod-write stores with no
// program and no TTINCRWC.
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 8 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb . rows 8 stride 2 crosscall contract config" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr refusal:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 8 } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#include "crosscall-addrmod-body.h"
