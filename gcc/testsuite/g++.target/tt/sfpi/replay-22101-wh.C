// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

#include <lltt.h>

void record () {
  lltt::record(16, 4);
  lltt::record<lltt::Exec>(20, 4);
}
// { dg-final { scan-assembler {\n_Z6recordv:\n\tTTREPLAY	16, 4, 0, 1\n\tTTREPLAY	20, 4, 1, 1\n\tret\n} } }

std::uint32_t replay () {
  lltt::replay(16, 4);
  return lltt::replay_insn(20, 4);
}
// { dg-final { scan-assembler {\n_Z6replayv:\n\tTTREPLAY	16, 4, 0, 0\n\tli	a0,67436544\n\taddi	a0,a0,64\n\tret\n} } }
