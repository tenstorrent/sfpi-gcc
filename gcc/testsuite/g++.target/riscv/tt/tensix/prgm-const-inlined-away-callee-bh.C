// The TU freedom proof runs at the first execution of the pass, after
// IPA inlining has consumed a fully-inlined comdat (here: an
// implicitly-instantiated inline destructor of a profiler-zone-shaped
// guard) and the unreachable-node sweep has removed its cgraph node --
// while the caller still carries the call statement and the callee's
// gimple body stays alive for the pending inline transform.  The scan
// walks that body on demand instead of refusing the whole TU as a call
// outside the translation unit; the body is audit-clean (scalar stores
// plus a fence), so the allocation fires.  -fno-early-inlining keeps
// the destructor call out of the early inliner so the removed-node
// state is reached deterministically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// (No SFPMAD expectation: under -fno-early-inlining the later mul+add
// combine keeps the register-operand SFPADD form; the rewrite itself is
// what this test pins -- the immediate form is gone and the PRGM
// register is programmed once.)
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
