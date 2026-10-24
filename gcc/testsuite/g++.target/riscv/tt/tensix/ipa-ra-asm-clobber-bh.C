// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-ipa-ra" }
// Check that an asm clobbering every caller-saved register keeps the caller's loop state in callee-saved registers.
// { dg-final { scan-assembler "sw\ts1," } }
// { dg-final { scan-assembler "sw\ts2," } }
// { dg-final { scan-assembler "lw\ts1," } }
// { dg-final { scan-assembler "sw\ts0," } }

__attribute__((noinline)) static void
ira_asm_callee ()
{
  asm volatile ("" ::: "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
		"t0", "t1", "t2", "t3", "t4", "t5", "t6", "s1", "s2");
}

void
ira_asm_caller (volatile int *p, int tiles, int a, int b, int c)
{
  for (int t = 0; t != tiles; ++t)
    {
      ira_asm_callee ();
      *p = a + t;
      *p = b ^ t;
      *p = c - t;
    }
}
