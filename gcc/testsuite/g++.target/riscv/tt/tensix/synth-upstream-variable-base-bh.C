// Multiple variable addends sharing one opcode must retain their original
// graph when renumbering cannot model the common variable base.  Both runtime
// addresses stay within the 13-bit field: [0,255] and [0,255] + [0,255].
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_synth_renumber" }
// { dg-final { scan-assembler-times {:SFPLOAD} 2 } }
// { dg-final { scan-tree-dump-times {Graph is too complex} 1 "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump-not {Creating new synth} "rvtt_synth_renumber" } }

extern volatile unsigned long iptr[];

void
synth_variable_base (unsigned x, unsigned y)
{
  x &= 255;
  y &= 255;
  auto id = __builtin_rvtt_synth_opcode (1, 0);
  auto base = id + x;
  auto a = __builtin_rvtt_sfpload (iptr, x, base, 1, 4, 7);
  auto b = __builtin_rvtt_sfpload (iptr, x + y, base + y, 1, 4, 7);
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
