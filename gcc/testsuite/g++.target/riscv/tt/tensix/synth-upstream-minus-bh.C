// A synthesized immediate can remain a subtraction after scalar optimization.
// The base addend keeps the actual encoded address in range: 1024 - [0,255].
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_synth_renumber" }
// Later codegen duplicates the conditional join load into both arms.
// { dg-final { scan-assembler-times {:SFPLOAD} 6 } }
// { dg-final { scan-assembler {(sub|neg)[\t ]} } }
// { dg-final { scan-tree-dump-times {Sub of} 3 "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump-times {Graph is too complex} 3 "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump-not {Creating new synth} "rvtt_synth_renumber" } }
// { dg-final { scan-tree-dump {PHI <} "rvtt_synth_renumber" } }

extern volatile unsigned long iptr[];

void
synth_subtract (unsigned x)
{
  unsigned delta = x & 255;
  auto id = __builtin_rvtt_synth_opcode (1, 1024);
  auto value = __builtin_rvtt_sfpload (iptr, 1024 - delta, id - delta,
                                      1, 4, 7);
  __builtin_rvtt_sfpwritelreg (value, 0);
}

// A later constant must not turn (opcode - delta) + 32 into opcode + delta.
// Keeping both uses makes the shared subtraction survive scalar reassociation.
void
synth_subtract_then_constant (unsigned x)
{
  unsigned delta = x & 255;
  auto id = __builtin_rvtt_synth_opcode (1, 1024);
  auto base = id - delta;
  auto a = __builtin_rvtt_sfpload (iptr, 1024 - delta, base, 1, 4, 7);
  auto b = __builtin_rvtt_sfpload (iptr, 1056 - delta, base + 32, 1, 4, 7);
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}

// Stop the unsupported graph before a downstream PHI or the parent's is_used
// assertion.  Distinct scalar asm comments retain both arms and the real PHI.
// Runtime addresses remain bounded: [769,1024], [801,1056], [833,1088].
void
synth_subtract_constant_phi (unsigned x, bool take_left)
{
  unsigned delta = x & 255;
  auto id = __builtin_rvtt_synth_opcode (1, 1024);
  auto base = id - delta;
  auto a = __builtin_rvtt_sfpload (iptr, 1024 - delta, base, 1, 4, 7);
  unsigned word, address;
  if (take_left)
    {
      asm volatile ("# left");
      word = base + 32;
      address = 1056 - delta;
    }
  else
    {
      asm volatile ("# right");
      word = base + 64;
      address = 1088 - delta;
    }
  auto b = __builtin_rvtt_sfpload (iptr, address, word, 1, 4, 7);
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
