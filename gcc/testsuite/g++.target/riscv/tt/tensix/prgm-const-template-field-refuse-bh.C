// Near misses for the template-field census (lane CF), both refusing
// the TU freedom proof by name: (a) a RUNTIME-composed word stored
// into a template field -- no audited word reaches the slot; (b) the
// template object's ADDRESS ESCAPES the scanned subtree (stored
// through an unrelated pointer), so unaudited writers can no longer be
// excluded and the whole object poisons.  No allocation happens in
// either TU.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared.: mop-template" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-template-field-unproven: the aggregate's address escaped" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }

typedef volatile unsigned int vu32;

struct mop_template
{
  unsigned outer, inner;
  unsigned start_op, end_op0, end_op1, loop_op0, loop_op1, last0, last1;
};

static inline void program_it (const mop_template *t)
{
  while (((vu32 *) 0xFFB88030)[0] & 1)
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

void runtime_word_refuses (unsigned rows)
{
  mop_template t;
  t.outer = rows;
  t.inner = 4;
  t.start_op = rows | 0x40;	/* runtime word: no audited base */
  t.end_op0 = 0x37020044;
  t.end_op1 = 0x02000000;
  t.loop_op0 = 0x28008000;
  t.loop_op1 = 0x02000000;
  t.last0 = 0x02000000;
  t.last1 = 0x02000000;
  program_it (&t);
  asm volatile (".ttinsn %0" :: "i" (0x01800000));

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void escaped_object_refuses (unsigned rows)
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
  *(mop_template * volatile *) 0x10000 = &u;	/* address escapes */
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
