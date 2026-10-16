// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-ipa-ra" }
// Check that a private copy of a COMDAT SFPU leaf lets the caller keep its loop state in caller-saved registers.
// { dg-final { scan-assembler-not "sw\ts\[0-9\]+," } }
// { dg-final { scan-assembler-not "\\.weak\t_Z10ira_calleeILi0EEvv" } }

template <int N>
__attribute__((noinline)) void
ira_callee ()
{
  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  x = __builtin_rvtt_sfpabs (x, 1);
  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
}

void
ira_caller (volatile int *p, int tiles, int a, int b, int c)
{
  for (int t = 0; t != tiles; ++t)
    {
      ira_callee<0> ();
      *p = a + t;
      *p = b ^ t;
      *p = c - t;
    }
}
