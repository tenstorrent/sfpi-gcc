// { dg-do-compile }
// { dg-additional-options "-std=c11 -march=rv64i_xttrocc -mabi=lp64" }

int main(void) {
  unsigned long slot = __builtin_riscv_ttrocc_cs_alloc (); // { dg-final { "scan-assembler" "tt\.rocc\.cs_alloc"} }

  __builtin_riscv_ttrocc_cs_save(slot); // { dg-final { "scan-assembler" "tt\.rocc\.cs_save" } }
  __builtin_riscv_ttrocc_cs_restore(slot); // { dg-final { "scan-assembler" "tt\.rocc\.cs_restore" } }

  __builtin_riscv_ttrocc_cs_dealloc(slot); // { dg-final { "scan-assembler" "tt\.rocc\.cs_dealloc" } }

}
