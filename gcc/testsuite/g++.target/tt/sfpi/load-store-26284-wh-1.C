// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

template<unsigned ADDR>
void load_imm () {
    vFloat r = __builtin_rvtt_sfpload (7, 3, ADDR);
    l_reg[LRegs::LReg3] = r;
}
template void load_imm<0> ();
// { dg-final { scan-assembler {\n_Z8load_immILj0EEvv:\n\tSFPLOAD	L3, 0, 7, 3\n\tret\n} } }

template void load_imm<0x3fff> ();
// { dg-final { scan-assembler {\n_Z8load_immILj16383EEvv:\n\tSFPLOAD	L3, 16383, 7, 3\n\tret\n} } }

void load_var (unsigned addr) {
    vFloat r = __builtin_rvtt_sfpload (7, 3, addr);
    l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z8load_varj:\n\tslli	a0,a0,18\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a5, 1882701824	# 2:7037c000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a0,a0,18\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7037c000 L3 :=\n\tret\n} } }

template<unsigned ADDR>
void store_imm () {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 7, 3, ADDR);
}
template void store_imm<0> ();
// { dg-final { scan-assembler {\n_Z9store_immILj0EEvv:\n\tSFPSTORE	0, L3, 7, 3\n\tret\n} } }

template void store_imm<0x3fff> ();
// { dg-final { scan-assembler {\n_Z9store_immILj16383EEvv:\n\tSFPSTORE	16383, L3, 7, 3\n\tret\n} } }

void store_var (unsigned addr) {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 7, 3, addr);
}
// { dg-final { scan-assembler {\n_Z9store_varj:\n\tslli	a0,a0,18\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a5, 1916256256	# 2:7237c000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a0,a0,18\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7237c000 L3\n\tret\n} } }
