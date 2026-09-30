// MOP template words programmed through a local aggregate:
// the production ckernel_template discipline builds the template in an
// automatic struct and a separate (not-early-inlined) program()
// routine stores the FIELDS into the MOP slot registers.  The TU scan
// classifies those slot words by scanning the callee's body UNDER the
// driving call's parameter bindings and by the local-aggregate field
// census: every store to each field is audited, the object's address
// never escapes, so the loaded slot words are exactly the audited
// constants.  The freedom proof passes and the prgm-const allocation
// fires in both callers.  (-fno-early-inlining pins the
// inline-clone-at-scan-time state the production kernels reach
// naturally through their mop_sync spin loops.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "mop-template" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
typedef volatile unsigned int vu32;

struct mop_template
{
  unsigned outer, inner;
  unsigned start_op, end_op0, end_op1, loop_op0, loop_op1, last0, last1;
};

static inline void program_it (const mop_template *t)
{
  while (((vu32 *) 0xFFB88030)[0] & 1)	/* mop_sync-like volatile spin */
    ;
  while (((vu32 *) 0xFFB88034)[0] & 2)
    ;
  while (((vu32 *) 0xFFB88038)[0] & 4)
    ;
  while (((vu32 *) 0xFFB8803c)[0] & 8)
    ;
  while (((vu32 *) 0xFFB88028)[0] & 1)
    ;
  while (((vu32 *) 0xFFB8802c)[0] & 2)
    ;
  while (((vu32 *) 0xFFB88030)[0] & 4)
    ;
  while (((vu32 *) 0xFFB88034)[0] & 8)
    ;
  while (((vu32 *) 0xFFB88038)[0] & 16)
    ;
  while (((vu32 *) 0xFFB8803c)[0] & 32)
    ;
  while (((vu32 *) 0xFFB88040)[0] & 64)
    ;
  while (((vu32 *) 0xFFB88044)[0] & 128)
    ;

  ((vu32 *) 0xFFB80000)[0] = t->outer;
  ((vu32 *) 0xFFB80000)[1] = t->inner;
  ((vu32 *) 0xFFB80000)[2] = t->start_op;
  ((vu32 *) 0xFFB80000)[3] = t->end_op0;
  ((vu32 *) 0xFFB80000)[4] = t->end_op1;
  ((vu32 *) 0xFFB80000)[5] = t->loop_op0;
  ((vu32 *) 0xFFB80000)[6] = t->loop_op1;
  ((vu32 *) 0xFFB80000)[7] = t->last0;
  ((vu32 *) 0xFFB80000)[8] = t->last1;
}

void datacopy_like (unsigned rows)
{
  mop_template t;
  t.outer = rows;
  t.inner = 4;
  t.start_op = 0x02000000;
  t.end_op0 = 0x37020044;
  t.end_op1 = 0x02000000;
  t.loop_op0 = 0x28008000;
  t.loop_op1 = 0x02000000;
  t.last0 = 0x02000000;
  t.last1 = 0x02000000;
  program_it (&t);
  asm volatile (".ttinsn %0" :: "i" (0x01800000));	/* TTI_MOP */

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void datacopy_like_twice (unsigned rows)
{
  mop_template u;
  u.outer = rows;
  u.inner = 8;
  u.start_op = 0x02000000;
  u.end_op0 = 0x37020044;
  u.end_op1 = 0x02000000;
  u.loop_op0 = 0x12010000;
  u.loop_op1 = 0x02000000;
  u.last0 = 0x02000000;
  u.last1 = 0x02000000;
  program_it (&u);
  asm volatile (".ttinsn %0" :: "i" (0x01800000));

  auto y = __builtin_rvtt_sfpreadlreg (2);
  auto q = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (y, q, 0);
      y = __builtin_rvtt_sfpaddi (nullptr, prod, 0x4142, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 2);
}
