// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
// { dg-final { scan-assembler-not "SFPSETSGN" } }
// { dg-final { scan-assembler-times "SFPCAST\tL\[0-9\]+, L\[0-9\]+, 3" 5 } }

// BH's int<->int SFPCAST (mod1=3) is a self-inverse sign-preserving
// conditional negate, so BOTH smag->int and int->smag lower to one SFPCAST
// and neither direction needs CC predication (SETCC/ENCC) or an SFPSETSGN
// fixup.  Renamed-equivalent forms with varied Dst offsets; the lowering
// keys only on the conversion types and the target, never on names/values.

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

// sign-magnitude Dst row combined into a two's-complement sum (the integer
// binary-op row shape): two smag->int loads, one int->smag store.
void gamma_row ()
{
  vInt lhs = dst_reg[5].mode<DataLayout::SM32>();
  vInt rhs = dst_reg[37].mode<DataLayout::SM32>();
  dst_reg[5].mode<DataLayout::SM32>() = lhs + rhs;
}
/*
**_Z9gamma_rowv:
**	SFPLOAD	L0, 10, 4, 7
**	SFPCAST	L0, L0, 3
**	SFPLOAD	L1, 74, 4, 7
**	SFPCAST	L1, L1, 3
**	SFPIADD	L0, L1, 0, 4
**	SFPCAST	L0, L0, 3
**	SFPSTORE	L0, 10, 4, 7
**	ret
*/

// explicit convert<> both directions
void delta_pair ()
{
  vSMag m = dst_reg[9];
  dst_reg[11] = convert<vInt> (m);
  vInt i = dst_reg[13].mode<DataLayout::I32>();
  dst_reg[15] = convert<vSMag> (i);
}
/*
**_Z10delta_pairv:
**	SFPLOAD	L0, 18, 4, 7
**	SFPCAST	L0, L0, 3
**	SFPSTORE	L0, 22, 4, 7
**	SFPLOAD	L0, 26, 4, 7
**	SFPCAST	L0, L0, 3
**	SFPSTORE	L0, 30, 4, 7
**	ret
*/
