// Check that cdtor-alias resolution keys on the call edge, not on names: prgm-const-cdtor-alias-bh.C with other names and constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "outside this translation unit" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

// The first function through the pass computes the TU facts while the
// guard consumer below still holds its pre-inline-transform body: the
// destructor call is live, its D1 symtab node is already removed, and
// only the caller's call edge still leads to the body.
void first_through_the_pass () {}

volatile unsigned tally;

template <int TAG>
struct bracket
{
  bool armed = false;
  bracket (const bracket &) = delete;
  __attribute__((always_inline)) inline bracket () { armed = true; }
  ~bracket ()
  {
    if (armed)
      tally = TAG + 3;
    __asm__ __volatile__ ("fence" ::: "memory");
  }
};

void
bracketed_body ()
{
  bracket<4242> b;
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto scale = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned k = 0; k != 24; ++k)
    {
      auto m = __builtin_rvtt_sfpmul (acc, scale, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, m, 0x40a0, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
