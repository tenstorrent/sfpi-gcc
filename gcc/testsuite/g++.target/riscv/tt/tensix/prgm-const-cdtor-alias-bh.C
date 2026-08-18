// The comdat cdtor SAME-BODY ALIAS shape: when the toolchain build has
// comdat-group support (HAVE_COMDAT_GROUP -- the shipping sfpi recipe;
// the D1 complete-object destructor is emitted as a bodiless alias of
// D2), a caller's out-of-line destructor call references a decl that
// (a) has no gimple body of its own and (b) loses even its symtab node
// once IPA inlining consumes every use and the unreachable-node sweep
// runs.  The TU freedom proof must resolve such a call through the
// caller's own call edge -- which tracks the alias redirection and the
// inline-clone bookkeeping -- and scan the ultimate target's body on
// demand, instead of refusing the whole TU as a call outside the
// translation unit.  On a toolchain built without comdat groups the
// same source keeps a full D1 body and the pre-existing decl-level
// on-demand scan covers it: the fire below is asserted for BOTH build
// recipes (this was the pin-11 stock-vs-lane divergence: the identical
// compiler source fired on lane-recipe builds and refused on the
// shipping recipe).  -fno-early-inlining keeps the destructor call out
// of the early inliner so the removed-node state is reached
// deterministically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "outside this translation unit" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

// The first function through the pass computes the TU facts eagerly,
// while the guard consumer below still carries its PRE-inline-transform
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
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
