// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mno-tt-fix-whfence" }
// Flag-off twin of fixwhfence-tdma-fire-wh.C: the standard RVWMO fence
// is emitted.
// { dg-final { scan-assembler "fence\trw,rw" } }
// { dg-final { scan-assembler-not "RISCV_TDMA_REG_STATUS" } }

void barrier ()
{
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
}
