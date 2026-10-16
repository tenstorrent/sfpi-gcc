// Check that Dst-row interleave pairs rows across a v_if, keeping each row's CC atom whole.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "Dst-row interleave: loop \\d+ trips 32 row-step 2 stmts \\d+ atoms 1 " "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "shared-words \[1-9\]" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "Dst-row interleave: loop \\d+ rows paired, row step 2 -> 4" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "SFPSTOCHRND" 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL\[0-7\], 2, 2, 7" 1 } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel
{
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
#include <limits>

void
guarded_rows ()
{
  for (int d = 0; d < 32; ++d)
    {
      const sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat r = x * 0.3f + 0.7f;
      r = r * x + 0.11f;
      r = r * x + 0.0625f;
      v_if (sfpi::setsgn (x, 0) <= std::numeric_limits<float>::infinity ())
	{
	  sfpi::dst_reg[0]
	    = sfpi::convert<sfpi::vFloat16b> (r, sfpi::RoundMode::Nearest);
	}
      v_endif;
      sfpi::dst_reg++;
    }
}
