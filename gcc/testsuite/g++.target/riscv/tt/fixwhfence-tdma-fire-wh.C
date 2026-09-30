// { dg-do compile }
// { dg-options "-mcpu=tt-wh -O2 -mtt-fix-whfence" }
// Wormhole inoperative-fence workaround (-mtt-fix-whfence, default on
// for tt-wh): a memory barrier is expanded as the TDMA status-register
// store sequence instead of the (inoperative) fence instruction.
// { dg-final { scan-assembler "RISCV_TDMA_REG_STATUS" } }
// { dg-final { scan-assembler-not "\tfence" } }

void barrier ()
{
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
}
