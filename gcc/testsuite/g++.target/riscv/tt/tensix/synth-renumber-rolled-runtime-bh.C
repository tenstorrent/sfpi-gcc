/* A runtime immediate needs a non-null instruction-buffer pointer to write
   the synthesised instruction word to.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// Check that a rolled counted loop with a variable address keeps one runtime synth_opcode.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler {sw\t[^\n]*# [0-9]+:SFPLOAD} } }
// { dg-final { scan-assembler-times {:SFPLOAD} 1 } }

void rolled_runtime ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  for (unsigned k = 0; k != 5; ++k)
    {
      auto q = __builtin_rvtt_sfpload (ckernel::instrn_buffer, 40 + 6 * k, 0, 0, 4, 7);
      acc = __builtin_rvtt_sfpxor (acc, q);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 30, 0, 0, 4, 7);
}
