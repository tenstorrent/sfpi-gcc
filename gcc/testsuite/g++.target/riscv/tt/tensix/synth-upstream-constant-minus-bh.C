// Literal -8 canonicalizes to PLUS of an unsigned negative constant before
// renumbering.  This supported PLUS graph must retain its positive variable
// part; it is a control, not evidence of the genuine MINUS_EXPR bailout.
// Runtime addresses remain [1016,1279].
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_synth_renumber" }
// { dg-final { scan-assembler-times {:SFPLOAD} 2 } }
// { dg-final { scan-tree-dump-not {Sub of} "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump-not {Graph is too complex} "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump {Creating new synth} "rvtt_synth_renumber" } }

extern volatile unsigned long iptr[];

void
synth_literal_minus_shared_variable (unsigned x)
{
  x &= 255;
  auto id = __builtin_rvtt_synth_opcode (1, 1024);
  auto base = id + x;
  auto a = __builtin_rvtt_sfpload (iptr, 1024 + x, base, 1, 4, 7);
  auto b = __builtin_rvtt_sfpload (iptr, 1016 + x, base - 8, 1, 4, 7);
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
