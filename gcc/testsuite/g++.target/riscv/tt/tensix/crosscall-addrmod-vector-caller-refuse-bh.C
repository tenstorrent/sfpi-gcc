// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Vector dataflow in the caller loop: a later formation in the caller
// could own the very slot this contract programs (the caller's own Dst
// auto-increment pass runs after the commit) -- the epoch scan refuses.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .drain-init-vector-live." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLER_EXTRA \
  do {									\
    cam_vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);	\
    __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 0, 7);		\
  } while (0)
#include "crosscall-addrmod-body.h"
