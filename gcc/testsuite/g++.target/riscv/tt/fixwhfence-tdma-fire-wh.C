// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whfence" }
// Check that -mtt-fix-whfence expands a barrier as the TDMA status-register sequence.
// { dg-final { scan-assembler "RISCV_TDMA_REG_STATUS" } }
// { dg-final { scan-assembler-not "\tfence" } }

void barrier ()
{
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
}
