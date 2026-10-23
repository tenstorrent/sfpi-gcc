// Check that a call to a comdat cdtor same-body alias is resolved through the call edge instead of refusing the TU.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "outside this translation unit" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

// The first function through the pass computes the TU facts eagerly,
// while the guard consumer below still carries its pre-inline-transform
// body: the destructor call statement is live, its D1 decl's symtab
// node is already swept, and only the caller's call edge still leads to
// the body (the kernel's run_kernel-then-main shape).
void first_through_the_pass () {}

volatile unsigned sink;

// A class template with a user-provided destructor: the implicit
// instantiation is comdat, the FE clones it into D1/D2, and with
// comdat groups D1 becomes a same-body alias of D2 (no virtual
// bases).  The destructor body is audit-clean (a scalar store plus a
// fence).
template <int ID>
struct guard
{
  bool open = false;
  guard (const guard &) = delete;
  __attribute__((always_inline)) inline guard () { open = true; }
  ~guard ()
  {
    if (open)
      sink = ID;
    __asm__ __volatile__ ("fence" ::: "memory");
  }
};

void
guarded_kernel ()
{
  guard<11> g;
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
