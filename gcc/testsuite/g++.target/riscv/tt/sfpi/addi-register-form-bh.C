// Check that -mtt-tensix-optimize-addi-register-form turns an SFPADDI whose operand stays live into a register SFPADD.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-addi-register-form -fdump-tree-rvtt_combine" }
// { dg-final { scan-tree-dump-times "addi-register-form: " 1 "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPADD\t" 1 } }
// { dg-final { scan-assembler-times "SFPADDI\t" 1 } }
// The one copy left is the predicated t = x merge.
// { dg-final { scan-assembler-times "SFPMOV\t" 1 } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel
{
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void
keep_operand ()
{
  vFloat x = dst_reg[0];
  vFloat t = x + 8388608.0f;
  v_if (x >= 8388608.0f)
    {
      t = x;
    }
  v_endif;
  dst_reg[0] = t;
}

void
operand_dies ()
{
  vFloat x = dst_reg[2];
  dst_reg[2] = x + 0.5f;
}
