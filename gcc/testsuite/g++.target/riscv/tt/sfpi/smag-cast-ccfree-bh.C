// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }
// { dg-final { scan-assembler-times "SFPCAST\tL\[0-9\]+, L\[0-9\]+, 3" 2 } }

// BH's int->smag SFPCAST (mod1=3) is native, but its inverse is not a
// semantics-preserving smag->int conversion: SFPCAST maps SM negative zero
// (0x80000000) to INT_MIN, whereas the C++ API canonicalizes it to zero.
// Therefore smag->int must retain the predicated sign-clear/negate helper.

namespace ckernel{
    volatile unsigned long *instrn_buffer;
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
**	SFPSETCC	L0, 0, 0
**	SFPSETSGN	L0, L0, 0, 1	# LV:L0
**	SFPIADD	L0, L9, 0, 6	# LV:L0
**	SFPENCC	3, 10
**	SFPLOAD	L1, 74, 4, 7
**	SFPSETCC	L1, 0, 0
**	SFPSETSGN	L1, L1, 0, 1	# LV:L1
**	SFPIADD	L1, L9, 0, 6	# LV:L1
**	SFPENCC	3, 10
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
**	SFPSETCC	L0, 0, 0
**	SFPSETSGN	L0, L0, 0, 1	# LV:L0
**	SFPIADD	L0, L9, 0, 6	# LV:L0
**	SFPENCC	3, 10
**	SFPSTORE	L0, 22, 4, 7
**	SFPLOAD	L0, 26, 4, 7
**	SFPCAST	L0, L0, 3
**	SFPSTORE	L0, 30, 4, 7
**	ret
*/

// Isolate the ISA counterexample in a constant-valued regression: the helper
// must canonicalize sign-magnitude negative zero to integer zero rather than
// replacing the conversion with SFPCAST's INT_MIN result.
void negative_zero ()
{
  l_reg[LRegs::LReg0] = convert<vInt> (vSMag (0x80000000u));
}
/*
**_Z13negative_zerov:
**	SFPLOADI	L0, 32768, 0
**	SFPSETCC	L0, 0, 0
**	SFPSETSGN	L0, L0, 0, 1	# LV:L0
**	SFPIADD	L0, L9, 0, 6	# LV:L0
**	SFPENCC	3, 10
**	# WRITE L0
**	ret
*/
