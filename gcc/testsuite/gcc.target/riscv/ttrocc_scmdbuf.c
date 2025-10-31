// { dg-do-compile }
// { dg-additional-options "-std=c11 -march=rv64i_xttrocc -mabi=lp64" }

int main(void) {
  __builtin_riscv_ttrocc_scmdbuf_wr_reg(4, 15); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_wr_reg"} }

  unsigned long to_write = 15;
  __builtin_riscv_ttrocc_scmdbuf_wr_reg(4, to_write); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_wr_reg"} }

  unsigned long rd0 = __builtin_riscv_ttrocc_scmdbuf_rd_reg(4); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_rd_reg"} }

  unsigned long rd1 = __builtin_riscv_ttrocc_scmdbuf_get_vc_space(); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_get_vc_space"} }
  unsigned long rd2 = __builtin_riscv_ttrocc_scmdbuf_get_vc_space_vc(15); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_get_vc_space_vc"} }
  
  unsigned long rd3 = __builtin_riscv_ttrocc_scmdbuf_wr_sent(); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_wr_sent"} }
  unsigned long rd4 = __builtin_riscv_ttrocc_scmdbuf_wr_sent_trid(15); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_wr_sent_trid"} }
  
  unsigned long rd5 = __builtin_riscv_ttrocc_scmdbuf_tr_ack(); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_tr_ack"} }
  unsigned long rd6 = __builtin_riscv_ttrocc_scmdbuf_tr_ack_trid(15); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_tr_ack_trid"} }
  
  __builtin_riscv_ttrocc_scmdbuf_reset(); // { dg-final { "scan-assembler" "tt\.rocc\.scmdbuf_reset"} }
 
}
