// Check that the TU scan walks the body of an inlined comdat whose cgraph node was removed instead of refusing the TU.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// No SFPMAD expectation: under -fno-early-inlining the mul+add combine
// keeps the register-operand SFPADD form.
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

volatile unsigned sink;

template <int ID>
struct zone
{
  bool open = false;
  zone (const zone &) = delete;
  __attribute__((always_inline)) inline zone () { open = true; }
  ~zone ()
  {
    if (open)
      sink = ID;
    __asm__ __volatile__ ("fence" ::: "memory");
  }
};

void
scoped_zone_kernel ()
{
  zone<7> z;
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
