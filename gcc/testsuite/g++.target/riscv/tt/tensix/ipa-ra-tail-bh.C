// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-ipa-ra" }
// Check that a callee ending in a tail call to an external function is treated as clobbering all caller-saved registers.
// { dg-final { scan-assembler "tail\t_Z12ira_externalv" } }
// { dg-final { scan-assembler "sw\ts0," } }

extern void ira_external ();

__attribute__((noinline)) static void
ira_tail_callee ()
{
  ira_external ();
}

void
ira_tail_caller (volatile int *p, int tiles, int a, int b, int c)
{
  for (int t = 0; t != tiles; ++t)
    {
      ira_tail_callee ();
      *p = a + t;
      *p = b ^ t;
      *p = c - t;
    }
}
