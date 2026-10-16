// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule-prera-loop -fdump-rtl-rvtt_lp_schedule_prera-details" }
// Check that the pre-RA loop scheduler does not move instructions across the CC writes of a v_if region.
// { dg-final { scan-rtl-dump-times "Prera-pressure-schedule barrier: cc-write" 2 "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule region: bb=\\d+ nodes=3 live-in=1 base-peak=2 base-makespan=5" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "pressure 2 -> ecc 2 / model 2; makespan 5 -> ecc 5 / model 5" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-assembler "SFPMUL\\t\[^\\n\]*\\n\\tSFPMUL\\t\[^\\n\]*\\n\\tSFPMUL\\t\[^\\n\]*\\n\\tSFPGT" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
prera_loop_cc_barrier ()
{
  for (int d = 0; d < 32; ++d)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      sfpi::vFloat c = a * a;
      c = c * c;
      c = c * c;
      v_if (a < 0.0f)
	{
	  c = c * a;
	}
      v_endif;
      sfpi::vFloat e = a + 1.0f;
      sfpi::vFloat f = a + 2.0f;
      sfpi::vFloat g = a + 3.0f;
      sfpi::dst_reg[0] = c + e * f * g;
      sfpi::dst_reg++;
    }
}
