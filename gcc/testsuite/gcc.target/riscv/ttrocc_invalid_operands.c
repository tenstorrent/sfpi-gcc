// { dg-do-compile }
// HACK: we add -fno-lto here as the error is not emitted for the invalid builtin arguments otherwise
// { dg-additional-options "-std=c11 -march=rv64i_xttrocc -mabi=lp64 -fno-lto" }

int main(void) {
  // Out of range cmdbuf
  __builtin_riscv_ttrocc_cmdbuf_wr_reg(15, 4, 15); // { dg-error "invalid argument to built-in function" }

  // Out of range register
  __builtin_riscv_ttrocc_cmdbuf_wr_reg(0, 74, 15); // { dg-error "invalid argument to built-in function" }

  // Valid limits
  __builtin_riscv_ttrocc_cmdbuf_wr_reg(0, 0, 15);
  __builtin_riscv_ttrocc_cmdbuf_wr_reg(1, 23, 15);
  __builtin_riscv_ttrocc_addrgen_wr_reg(1, 47, 15);
}
