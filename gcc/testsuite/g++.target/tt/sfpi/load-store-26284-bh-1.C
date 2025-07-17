// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

template<unsigned ADDR>
void load_imm () {
    vFloat r = __builtin_rvtt_sfpload (3, 7, ADDR);
    l_reg[LRegs::LReg3] = r;
}
template void load_imm<0> ();
// { dg-final { scan-assembler {\n_Z8load_immILj0EEvv:\n\tSFPLOAD	L3, 0, 3, 7\n\tret\n} } }

template void load_imm<0x1fff> ();
// { dg-final { scan-assembler {\n_Z8load_immILj8191EEvv:\n\tSFPLOAD	L3, 8191, 3, 7\n\tret\n} } }

void load_var (unsigned addr) {
    vFloat r = __builtin_rvtt_sfpload (3, 7, addr);
    l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z8load_varj:\n\tslli	a0,a0,19\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a5, 1882447872	# 2:7033e000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a0,a0,19\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7033e000 L3 :=\n\tret\n} } }

template<unsigned ADDR>
void store_imm () {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 3, 7, ADDR);
}
template void store_imm<0> ();
// { dg-final { scan-assembler {\n_Z9store_immILj0EEvv:\n\tSFPSTORE	0, L3, 3, 7\n\tret\n} } }

template void store_imm<0x1fff> ();
// { dg-final { scan-assembler {\n_Z9store_immILj8191EEvv:\n\tSFPSTORE	8191, L3, 3, 7\n\tret\n} } }

void store_var (unsigned addr) {
    vFloat r = l_reg[LRegs::LReg3];
    __builtin_rvtt_sfpstore (r.get (), 3, 7, addr);
}
// { dg-final { scan-assembler {\n_Z9store_varj:\n\tslli	a0,a0,19\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a5, 1916002304	# 2:7233e000\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tsrli	a0,a0,19\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7233e000 L3\n\tret\n} } }
