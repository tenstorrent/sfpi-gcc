// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mno-tt-fix-whfence" }
// Check that -mno-tt-fix-whfence emits the standard RVWMO fence.
// { dg-final { scan-assembler "fence\trw,rw" } }
// { dg-final { scan-assembler-not "RISCV_TDMA_REG_STATUS" } }

void barrier ()
{
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
}
