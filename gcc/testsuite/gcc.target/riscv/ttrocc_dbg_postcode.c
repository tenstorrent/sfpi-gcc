// { dg-do-compile }
// { dg-additional-options "-std=c11 -march=rv64i_xttrocc -mabi=lp64" }

int main(void) {
  __builtin_riscv_ttrocc_dbg_postcode(1); // { dg-final { "scan-assembler" "tt\.rocc\.dbg_postcode" } }
}
