// { dg-do-compile }
// { dg-additional-options "-std=c11 -march=rv64i_xttrocc -mabi=lp64" }

int
main (void)
{
  __builtin_riscv_ttrocc_addrgen_wr_reg (0, 4, 15); // { dg-final { "scan-assembler" "tt\.rocc\.addrgen_wr_reg\tx0,.*,.*,.*,x0" } }

  unsigned long rd0 = __builtin_riscv_ttrocc_addrgen_rd_reg (0, 4); // { dg-final { "scan-assembler" "tt\.rocc\.addrgen_rd_reg\t.*,.*,.*,x0,x0" } }

}
