// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-pressure-schedule-prera-loop -fdump-rtl-rvtt_lp_schedule_prera-details" }
// Check that the pre-RA loop scheduler commits the capped candidate when the ECC order would raise peak pressure.
// { dg-final { scan-rtl-dump "Prera-pressure-schedule region: bb=\\d+ nodes=19 live-in=4 base-peak=7 base-makespan=26" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule candidates: ecc peak=8 end=21 moves=1; model peak=7 end=26 moves=1; limit=8" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule capped candidate: peak=7 end=21 moves=1" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=19 pressure 7 -> 7 makespan 26 -> 21 model-peak=7 candidate=capped target=bh" "rvtt_lp_schedule_prera" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
prera_loop_capped ()
{
  constexpr float C0 = -5.0170612335205078e-01f;
  constexpr float C1 = 3.4454023838043213e-01f;
  constexpr float C2 = -2.0109501481056213e-01f;
  for (int d = 0; d < 32; ++d)
    {
      const sfpi::vFloat x = sfpi::dst_reg[0];
      const sfpi::vFloat u = x + 1.0f;
      const sfpi::vFloat three_quarters = 0.75f;
      sfpi::vInt e = sfpi::as<sfpi::vInt> (u) - sfpi::as<sfpi::vInt> (three_quarters);
      e = sfpi::as<sfpi::vInt> (sfpi::setman (sfpi::as<sfpi::vFloat> (e), 0));
      const sfpi::vFloat neg_four = -4.0f;
      const sfpi::vFloat xs = sfpi::as<sfpi::vFloat> (sfpi::as<sfpi::vInt> (x) - e);
      const sfpi::vFloat s = sfpi::as<sfpi::vFloat> (sfpi::as<sfpi::vInt> (neg_four) - e);
      const sfpi::vFloat t = -0.25f * s - 1.0f;
      const sfpi::vFloat r = xs + t;
      sfpi::vFloat p = C2;
      p = p * r + C1;
      p = p * r + C0;
      sfpi::vFloat result = r + (r * r) * p;
      sfpi::vFloat ef = sfpi::convert<sfpi::vFloat> (sfpi::abs (e), sfpi::RoundMode::Nearest);
      ef = sfpi::copysgn (ef, sfpi::as<sfpi::vFloat> (e));
      result = ef * 0x1p-23f + result;
      sfpi::dst_reg[0] = result;
      sfpi::dst_reg++;
    }
}
