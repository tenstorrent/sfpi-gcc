// Regression guard for the synth-renumber constant-fold fix: peeled
// iterations whose synthesized encodings stay VARIABLE (runtime base
// address) exercise the pre-existing plain renumber path -- multiple
// adds sharing one synth_opcode get 1:1 renumbered ids.  This shape
// compiled identically before and after the fix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {:SFPLOAD} 3 } }

void plain_multi (unsigned x)
{
  unsigned v = x & 0xff;
  auto r = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
#pragma GCC unroll 3
  for (unsigned j = 0; j != 3; ++j)
    {
      auto q = __builtin_rvtt_sfpload (nullptr, v + 2 * j, 0, 0, 4, 7);
      r = __builtin_rvtt_sfpxor (r, q);
    }
  __builtin_rvtt_sfpstore (nullptr, r, 62, 0, 0, 4, 7);
}
