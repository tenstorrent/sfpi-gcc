/* A runtime immediate needs a non-null instruction-buffer pointer to write
   the synthesised instruction word to.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// Check that variable synthesized encodings sharing one synth_opcode renumber 1:1.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {:SFPLOAD} 3 } }

void plain_multi (unsigned x)
{
  unsigned v = x & 0xff;
  auto r = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
#pragma GCC unroll 3
  for (unsigned j = 0; j != 3; ++j)
    {
      auto q = __builtin_rvtt_sfpload (ckernel::instrn_buffer, v + 2 * j, 0, 0, 4, 7);
      r = __builtin_rvtt_sfpxor (r, q);
    }
  __builtin_rvtt_sfpstore (nullptr, r, 62, 0, 0, 4, 7);
}
