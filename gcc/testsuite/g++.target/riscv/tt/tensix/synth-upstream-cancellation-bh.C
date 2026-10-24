// Disabling scalar reassociation keeps this cancellation intact at renumbering.
// Literal subtraction is represented as PLUS of an unsigned negative constant.
// Keep the whole graph when its accumulated constant returns to zero: skipping
// rebasing then replacing its ancestor would otherwise drop the variable part.
// All effective runtime addresses stay within [1024,1283].
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-tree-reassoc -fno-exceptions -fno-rtti -fdump-tree-rvtt_synth_renumber" }
// Two runtime words and one literal load remain.
// { dg-final { scan-assembler-times {SFPLOAD} 3 } }
// { dg-final { scan-tree-dump-times {Graph is too complex} 1 "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump-not {Creating new synth} "rvtt_synth_renumber" } }

extern volatile unsigned long iptr[];

void
synth_constant_base_cancellation (unsigned x)
{
  x &= 255;
  auto id = __builtin_rvtt_synth_opcode (1, 1024);
  auto constant = id + 4;
  auto a = __builtin_rvtt_sfpload (iptr, 1028, constant, 1, 4, 7);
  auto base = constant + x;
  auto b = __builtin_rvtt_sfpload (iptr, 1028 + x, base, 1, 4, 7);
  auto c = __builtin_rvtt_sfpload (iptr, 1024 + x, base - 4, 1, 4, 7);
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}

