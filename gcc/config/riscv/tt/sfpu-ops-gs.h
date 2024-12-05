#pragma once

#define TT_OP_GS(opcode, params) ( (opcode << 24) + params )

#define TT_OP_GS_ADDDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x58, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_ADDRCRXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_GS(0x53, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_GS_ADDRCRZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_GS(0x56, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_GS_APOOL3S1(clear_dvalid, addr_mode, dst) \
  TT_OP_GS(0x25, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_APOOL3S2(clear_dvalid, addr_mode, dst) \
  TT_OP_GS(0x32, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_ATCAS(MemHierSel, SwapVal, CmpVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x64, (((MemHierSel) << 23) + ((SwapVal) << 18) + ((CmpVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_ATGETM(mutex_index) \
  TT_OP_GS(0xa0, (((mutex_index) << 0)))

#define TT_OP_GS_ATINCGET(MemHierSel, WrapVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x61, (((MemHierSel) << 23) + ((WrapVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_ATINCGETPTR(MemHierSel, NoIncr, IncrVal, WrapVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x62, (((MemHierSel) << 23) + ((NoIncr) << 22) + ((IncrVal) << 18) + ((WrapVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_ATRELM(mutex_index) \
  TT_OP_GS(0xa1, (((mutex_index) << 0)))

#define TT_OP_GS_ATSWAP(MemHierSel, SwapMask, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x63, (((MemHierSel) << 23) + ((SwapMask) << 14) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_BITWOPDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x5b, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_CMPDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x5d, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_CONV3S1(clear_dvalid, rotate_weights, addr_mode, dst) \
  TT_OP_GS(0x22, (((clear_dvalid) << 22) + ((rotate_weights) << 17) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_CONV3S2(clear_dvalid, rotate_weights, addr_mode, dst) \
  TT_OP_GS(0x23, (((clear_dvalid) << 22) + ((rotate_weights) << 17) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_DMANOP\
  TT_OP_GS(0x60, 0)

#define TT_OP_GS_DOTPV(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x29, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_ELWADD(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x28, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_ELWMUL(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x27, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_ELWSUB(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x30, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_FLUSHDMA(FlushSpec) \
  TT_OP_GS(0x46, (((FlushSpec) << 0)))

#define TT_OP_GS_GAPOOL(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x34, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_GATESRCRST(reset_srcb_gate_control, reset_srca_gate_control) \
  TT_OP_GS(0x35, (((reset_srcb_gate_control) << 1) + ((reset_srca_gate_control) << 0)))

#define TT_OP_GS_GMPOOL(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x33, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_INCADCXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X) \
  TT_OP_GS(0x52, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6)))

#define TT_OP_GS_INCADCZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X) \
  TT_OP_GS(0x55, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6)))

#define TT_OP_GS_INCRWC(rwc_cr, rwc_d, rwc_b, rwc_a) \
  TT_OP_GS(0x38, (((rwc_cr) << 18) + ((rwc_d) << 14) + ((rwc_b) << 10) + ((rwc_a) << 6)))

#define TT_OP_GS_LOADIND(SizeSel, OffsetIndex, AutoIncSpec, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x49, (((SizeSel) << 22) + ((OffsetIndex) << 14) + ((AutoIncSpec) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_LOADREG(TdmaDataRegIndex, RegAddr) \
  TT_OP_GS(0x68, (((TdmaDataRegIndex) << 18) + ((RegAddr) << 0)))

#define TT_OP_GS_MOP(mop_type, loop_count, zmask_lo16) \
  TT_OP_GS(0x01, (((mop_type) << 23) + ((loop_count) << 16) + ((zmask_lo16) << 0)))

#define TT_OP_GS_MOP_CFG(zmask_hi16) \
  TT_OP_GS(0x03, (((zmask_hi16) << 0)))

#define TT_OP_GS_MOVA2D(instr_mod, addr_mode, src, dst) \
  TT_OP_GS(0x12, (((instr_mod) << 19) + ((addr_mode) << 15) + ((src) << 10) + ((dst) << 0)))

#define TT_OP_GS_MOVB2D(instr_mod, addr_mode, src, dst) \
  TT_OP_GS(0x13, (((instr_mod) << 19) + ((addr_mode) << 15) + ((src) << 10) + ((dst) << 0)))

#define TT_OP_GS_MOVD2A(instr_mod, addr_mode, src, dst) \
  TT_OP_GS(0x08, (((instr_mod) << 19) + ((addr_mode) << 15) + ((src) << 10) + ((dst) << 0)))

#define TT_OP_GS_MOVDBGA2D(instr_mod, addr_mode, src, dst) \
  TT_OP_GS(0x09, (((instr_mod) << 19) + ((addr_mode) << 15) + ((src) << 10) + ((dst) << 0)))

#define TT_OP_GS_MPOOL3S1(clear_dvalid, addr_mode, dst) \
  TT_OP_GS(0x24, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_MPOOL3S2(clear_dvalid, addr_mode, dst) \
  TT_OP_GS(0x31, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_MULDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x5a, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_MVMUL(clear_dvalid, instr_mod, addr_mode, dst) \
  TT_OP_GS(0x26, (((clear_dvalid) << 22) + ((instr_mod) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_GS_NOP\
  TT_OP_GS(0x02, 0)

#define TT_OP_GS_PACR(AddrMode, ZeroWrite, PackSel, OvrdThreadId, Concat, Flush, Last) \
  TT_OP_GS(0x41, (((AddrMode) << 15) + ((ZeroWrite) << 12) + ((PackSel) << 8) + ((OvrdThreadId) << 7) + ((Concat) << 4) + ((Flush) << 1) + ((Last) << 0)))

#define TT_OP_GS_RAREB\
  TT_OP_GS(0x15, 0)

#define TT_OP_GS_RDCFG(GprAddress, CfgReg) \
  TT_OP_GS(0xb1, (((GprAddress) << 16) + ((CfgReg) << 0)))

#define TT_OP_GS_REG2FLOP(SizeSel, TargetSel, ByteOffset, ContextId_2, FlopIndex, RegIndex) \
  TT_OP_GS(0x48, (((SizeSel) << 22) + ((TargetSel) << 20) + ((ByteOffset) << 18) + ((ContextId_2) << 16) + ((FlopIndex) << 6) + ((RegIndex) << 0)))

#define TT_OP_GS_RSTDMA\
  TT_OP_GS(0x44, 0)

#define TT_OP_GS_SEMGET(sem_sel) \
  TT_OP_GS(0xa5, (((sem_sel) << 2)))

#define TT_OP_GS_SEMINIT(max_value, init_value, sem_sel) \
  TT_OP_GS(0xa3, (((max_value) << 20) + ((init_value) << 16) + ((sem_sel) << 2)))

#define TT_OP_GS_SEMPOST(sem_sel) \
  TT_OP_GS(0xa4, (((sem_sel) << 2)))

#define TT_OP_GS_SEMWAIT(stall_res, sem_sel, wait_sem_cond) \
  TT_OP_GS(0xa6, (((stall_res) << 14) + ((sem_sel) << 2) + ((wait_sem_cond) << 0)))

#define TT_OP_GS_SETADC(CntSetMask, ChannelIndex, DimensionIndex, Value) \
  TT_OP_GS(0x50, (((CntSetMask) << 21) + ((ChannelIndex) << 20) + ((DimensionIndex) << 18) + ((Value) << 0)))

#define TT_OP_GS_SETADCXX(CntSetMask, x_end2, x_start) \
  TT_OP_GS(0x5e, (((CntSetMask) << 21) + ((x_end2) << 10) + ((x_start) << 0)))

#define TT_OP_GS_SETADCXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_GS(0x51, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_GS_SETADCZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_GS(0x54, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_GS_SETASHRMH(reg_mask, halo_mask) \
  TT_OP_GS(0x1e, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_GS_SETASHRMH0(reg_mask, halo_mask) \
  TT_OP_GS(0x1a, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_GS_SETASHRMH1(reg_mask, halo_mask) \
  TT_OP_GS(0x1b, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_GS_SETASHRMV(reg_mask2) \
  TT_OP_GS(0x1c, (((reg_mask2) << 0)))

#define TT_OP_GS_SETC16(setc16_reg, setc16_value) \
  TT_OP_GS(0xb2, (((setc16_reg) << 16) + ((setc16_value) << 0)))

#define TT_OP_GS_SETDMAREG(Payload_SigSelSize, Payload_SigSel, SetSignalsMode, RegIndex16b) \
  TT_OP_GS(0x45, (((Payload_SigSelSize) << 22) + ((Payload_SigSel) << 8) + ((SetSignalsMode) << 7) + ((RegIndex16b) << 0)))
#define TT_OP_GS_SETRWC(clear_ab_vld, rwc_cr, rwc_d, rwc_b, rwc_a, BitMask) \
  TT_OP_GS(0x37, (((clear_ab_vld) << 22) + ((rwc_cr) << 18) + ((rwc_d) << 14) + ((rwc_b) << 10) + ((rwc_a) << 6) + ((BitMask) << 0)))

#define TT_OP_GS_SFPABS(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7d, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPADD(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x85, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPADDI(imm16_math, lreg_dest, instr_mod1) \
  TT_OP_GS(0x75, (((imm16_math) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPAND(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7e, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPCOMPC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x8b, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPDIVP2(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x76, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPENCC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x8a, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPEXEXP(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x77, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPEXMAN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x78, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPIADD(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x79, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPLOAD(lreg_ind, instr_mod0, dest_reg_addr) \
  TT_OP_GS(0x70, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((dest_reg_addr) << 0)))

#define TT_OP_GS_SFPLOADI(lreg_ind, instr_mod0, imm16) \
  TT_OP_GS(0x71, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((imm16) << 0)))

#define TT_OP_GS_SFPLUT(lreg_ind, instr_mod0, dest_reg_addr) \
  TT_OP_GS(0x73, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((dest_reg_addr) << 0)))

#define TT_OP_GS_SFPLZ(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x81, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPMAD(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x84, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPMOV(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7c, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPMUL(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x86, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPMULI(imm16_math, lreg_dest, instr_mod1) \
  TT_OP_GS(0x74, (((imm16_math) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPNOT(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x80, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPOR(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7f, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPPOPC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x88, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPPUSHC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x87, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSETCC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7b, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSETEXP(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x82, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSETMAN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x83, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSETSGN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x89, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSHFT(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_GS(0x7a, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_GS_SFPSTORE(lreg_ind, instr_mod0, dest_reg_addr) \
  TT_OP_GS(0x72, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((dest_reg_addr) << 0)))

#define TT_OP_GS_SHIFTDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x5c, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_SHIFTXA(log2_amount2, shift_mode) \
  TT_OP_GS(0x17, (((log2_amount2) << 2) + ((shift_mode) << 0)))

#define TT_OP_GS_STALLWAIT(stall_res, wait_res) \
  TT_OP_GS(0xa2, (((stall_res) << 14) + ((wait_res) << 0)))

#define TT_OP_GS_STOREIND(MemHierSel, SizeSel, RegSizeSel, OffsetIndex, AutoIncSpec, DataRegIndex, AddrRegIndex) \
  TT_OP_GS(0x66, (((MemHierSel) << 23) + ((SizeSel) << 22) + ((RegSizeSel) << 21) + ((OffsetIndex) << 14) + ((AutoIncSpec) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_GS_STOREREG(TdmaDataRegIndex, RegAddr) \
  TT_OP_GS(0x67, (((TdmaDataRegIndex) << 18) + ((RegAddr) << 0)))

#define TT_OP_GS_SUBDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_GS(0x59, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_GS_TRNSPSRCA\
  TT_OP_GS(0x14, 0)

#define TT_OP_GS_UNPACR_NOP(Unpack_block_selection, NoOp) \
  TT_OP_GS(0x43, (((Unpack_block_selection) << 23) + ((NoOp) << 0)))

#define TT_OP_GS_WRCFG(GprAddress, wr128b, CfgReg) \
  TT_OP_GS(0xb0, (((GprAddress) << 16) + ((wr128b) << 15) + ((CfgReg) << 0)))

#define TT_OP_GS_XMOV(Mov_block_selection, Last) \
  TT_OP_GS(0x40, (((Mov_block_selection) << 23) + ((Last) << 0)))

#define TT_OP_GS_ZEROACC(clear_mode, AddrMode, dst) \
  TT_OP_GS(0x10, (((clear_mode) << 19) + ((AddrMode) << 15) + ((dst) << 0)))

#define TT_OP_GS_ZEROSRC(zero_val, write_mode, bank_mask, src_mask) \
  TT_OP_GS(0x11, (((zero_val) << 4) + ((write_mode) << 3) + ((bank_mask) << 2) + ((src_mask) << 0)))

