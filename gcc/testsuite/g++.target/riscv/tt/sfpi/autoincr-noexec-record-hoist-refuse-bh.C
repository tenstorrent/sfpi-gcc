// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_dst_autoincr -fdump-rtl-rvtt_replay" }
// Check that record-hoist refuses a capture near a mod-write row, so the dst-autoincr group still forms.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel { constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer; }
#include <sfpi.h>
__attribute__((noinline)) void
qk_zeta_rows_near (volatile int *sep)
{
  if (*sep & 1)
    {
      sfpi::vFloat qv = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = qv * qv; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv + qv; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv * 3.25f; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv + 1.5f; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv * qv + qv; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv * 0.5f; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv + 2.75f; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv * qv - qv; sfpi::dst_reg++;
      sfpi::dst_reg[0] = qv * 1.25f + qv; sfpi::dst_reg++;
    }
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned kx = 0; kx != 5; ++kx)
    {
      ga = __builtin_rvtt_sfpmul (ga, ga, 0);
      gb = __builtin_rvtt_sfpmul (gb, gb, 0);
      gc = __builtin_rvtt_sfpmul (gc, gc, 0);
      ga = __builtin_rvtt_sfpmul (ga, gb, 0);
      gb = __builtin_rvtt_sfpmul (gb, gc, 0);
      gc = __builtin_rvtt_sfpmul (gc, ga, 0);
      *sep = (int) kx;
      ga = __builtin_rvtt_sfpmul (ga, ga, 0);
      gb = __builtin_rvtt_sfpmul (gb, gb, 0);
      gc = __builtin_rvtt_sfpmul (gc, gc, 0);
      ga = __builtin_rvtt_sfpmul (ga, gb, 0);
      gb = __builtin_rvtt_sfpmul (gb, gc, 0);
      gc = __builtin_rvtt_sfpmul (gc, ga, 0);
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
