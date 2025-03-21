#pragma once

#define TT_OP_WH(opcode, params) ( (opcode << 24) + params )

#define TT_OP_WH_ADDDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x58, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_ADDRCRXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_WH(0x53, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_WH_ADDRCRZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_WH(0x56, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_WH_APOOL3S1(clear_dvalid, addr_mode, index_en, dst) \
  TT_OP_WH(0x25, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_APOOL3S2(clear_dvalid, addr_mode, index_en, dst) \
  TT_OP_WH(0x32, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_ATCAS(MemHierSel, SwapVal, CmpVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x64, (((MemHierSel) << 23) + ((SwapVal) << 18) + ((CmpVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_ATGETM(mutex_index) \
  TT_OP_WH(0xa0, (((mutex_index) << 0)))

#define TT_OP_WH_ATINCGET(MemHierSel, WrapVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x61, (((MemHierSel) << 23) + ((WrapVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_ATINCGETPTR(MemHierSel, NoIncr, IncrVal, WrapVal, Sel32b, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x62, (((MemHierSel) << 23) + ((NoIncr) << 22) + ((IncrVal) << 18) + ((WrapVal) << 14) + ((Sel32b) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_ATRELM(mutex_index) \
  TT_OP_WH(0xa1, (((mutex_index) << 0)))

#define TT_OP_WH_ATSWAP(MemHierSel, SwapMask, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x63, (((MemHierSel) << 23) + ((SwapMask) << 14) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_BITWOPDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x5b, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_CMPDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x5d, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_CONV3S1(clear_dvalid, rotate_weights, addr_mode, dst) \
  TT_OP_WH(0x22, (((clear_dvalid) << 22) + ((rotate_weights) << 17) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_CONV3S2(clear_dvalid, rotate_weights, addr_mode, dst) \
  TT_OP_WH(0x23, (((clear_dvalid) << 22) + ((rotate_weights) << 17) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_DMANOP\
  TT_OP_WH(0x60, 0)

#define TT_OP_WH_DOTPV(clear_dvalid, dest_accum_en, instr_mod19, addr_mode, dst) \
  TT_OP_WH(0x29, (((clear_dvalid) << 22) + ((dest_accum_en) << 21) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_ELWADD(clear_dvalid, dest_accum_en, instr_mod19, addr_mode, dst) \
  TT_OP_WH(0x28, (((clear_dvalid) << 22) + ((dest_accum_en) << 21) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_ELWMUL(clear_dvalid, dest_accum_en, instr_mod19, addr_mode, dst) \
  TT_OP_WH(0x27, (((clear_dvalid) << 22) + ((dest_accum_en) << 21) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_ELWSUB(clear_dvalid, dest_accum_en, instr_mod19, addr_mode, dst) \
  TT_OP_WH(0x30, (((clear_dvalid) << 22) + ((dest_accum_en) << 21) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_FLUSHDMA(FlushSpec) \
  TT_OP_WH(0x46, (((FlushSpec) << 0)))

#define TT_OP_WH_GAPOOL(clear_dvalid, instr_mod19, addr_mode, max_pool_index_en, dst) \
  TT_OP_WH(0x34, (((clear_dvalid) << 22) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((max_pool_index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_GATESRCRST(reset_srcb_gate_control, reset_srca_gate_control) \
  TT_OP_WH(0x35, (((reset_srcb_gate_control) << 1) + ((reset_srca_gate_control) << 0)))

#define TT_OP_WH_GMPOOL(clear_dvalid, instr_mod19, addr_mode, max_pool_index_en, dst) \
  TT_OP_WH(0x33, (((clear_dvalid) << 22) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((max_pool_index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_INCADCXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X) \
  TT_OP_WH(0x52, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6)))

#define TT_OP_WH_INCADCZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X) \
  TT_OP_WH(0x55, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6)))

#define TT_OP_WH_INCRWC(rwc_cr, rwc_d, rwc_b, rwc_a) \
  TT_OP_WH(0x38, (((rwc_cr) << 18) + ((rwc_d) << 14) + ((rwc_b) << 10) + ((rwc_a) << 6)))

#define TT_OP_WH_LOADIND(SizeSel, OffsetIndex, AutoIncSpec, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x49, (((SizeSel) << 22) + ((OffsetIndex) << 14) + ((AutoIncSpec) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_LOADREG(TdmaDataRegIndex, RegAddr) \
  TT_OP_WH(0x68, (((TdmaDataRegIndex) << 18) + ((RegAddr) << 0)))

#define TT_OP_WH_MFCONV3S1(clear_dvalid, rotate_weights, addr_mode, dst) \
  TT_OP_WH(0x3a, (((clear_dvalid) << 22) + ((rotate_weights) << 17) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_MOP(mop_type, loop_count, zmask_lo16) \
  TT_OP_WH(0x01, (((mop_type) << 23) + ((loop_count) << 16) + ((zmask_lo16) << 0)))

#define TT_OP_WH_MOP_CFG(zmask_hi16) \
  TT_OP_WH(0x03, (((zmask_hi16) << 0)))

#define TT_OP_WH_MOVA2D(dest_32b_lo, src, addr_mode, instr_mod, dst) \
  TT_OP_WH(0x12, (((dest_32b_lo) << 23) + ((src) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((dst) << 0)))

#define TT_OP_WH_MOVB2A(srca, addr_mode, instr_mod, srcb) \
  TT_OP_WH(0x0b, (((srca) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((srcb) << 0)))

#define TT_OP_WH_MOVB2D(dest_32b_lo, src, addr_mode, instr_mod, dst) \
  TT_OP_WH(0x13, (((dest_32b_lo) << 23) + ((src) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((dst) << 0)))

#define TT_OP_WH_MOVD2A(dest_32b_lo, src, addr_mode, instr_mod, dst) \
  TT_OP_WH(0x08, (((dest_32b_lo) << 23) + ((src) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((dst) << 0)))

#define TT_OP_WH_MOVD2B(dest_32b_lo, src, addr_mode, instr_mod, dst) \
  TT_OP_WH(0x0a, (((dest_32b_lo) << 23) + ((src) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((dst) << 0)))

#define TT_OP_WH_MOVDBGA2D(dest_32b_lo, src, addr_mode, instr_mod, dst) \
  TT_OP_WH(0x09, (((dest_32b_lo) << 23) + ((src) << 17) + ((addr_mode) << 15) + ((instr_mod) << 12) + ((dst) << 0)))

#define TT_OP_WH_MPOOL3S1(clear_dvalid, addr_mode, index_en, dst) \
  TT_OP_WH(0x24, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_MPOOL3S2(clear_dvalid, addr_mode, index_en, dst) \
  TT_OP_WH(0x31, (((clear_dvalid) << 22) + ((addr_mode) << 15) + ((index_en) << 14) + ((dst) << 0)))

#define TT_OP_WH_MULDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x5a, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_MVMUL(clear_dvalid, instr_mod19, addr_mode, dst) \
  TT_OP_WH(0x26, (((clear_dvalid) << 22) + ((instr_mod19) << 19) + ((addr_mode) << 15) + ((dst) << 0)))

#define TT_OP_WH_NOP\
  TT_OP_WH(0x02, 0)

#define TT_OP_WH_PACR(AddrMode, ZeroWrite, PackSel, OvrdThreadId, Concat, Flush, Last) \
  TT_OP_WH(0x41, (((AddrMode) << 15) + ((ZeroWrite) << 12) + ((PackSel) << 8) + ((OvrdThreadId) << 7) + ((Concat) << 4) + ((Flush) << 1) + ((Last) << 0)))

#define TT_OP_WH_PACR_SETREG(Push, AddrSel, WrData, PackSel, StreamId, Flush, Last) \
  TT_OP_WH(0x4a, (((Push) << 23) + ((AddrSel) << 22) + ((WrData) << 12) + ((PackSel) << 8) + ((StreamId) << 2) + ((Flush) << 1) + ((Last) << 0)))

#define TT_OP_WH_RAREB\
  TT_OP_WH(0x15, 0)

#define TT_OP_WH_RDCFG(GprAddress, CfgReg) \
  TT_OP_WH(0xb1, (((GprAddress) << 16) + ((CfgReg) << 0)))

#define TT_OP_WH_REG2FLOP(SizeSel, TargetSel, ByteOffset, ContextId_2, FlopIndex, RegIndex) \
  TT_OP_WH(0x48, (((SizeSel) << 22) + ((TargetSel) << 20) + ((ByteOffset) << 18) + ((ContextId_2) << 16) + ((FlopIndex) << 6) + ((RegIndex) << 0)))

#define TT_OP_WH_REPLAY(start_idx, len, execute_while_loading, load_mode) \
  TT_OP_WH(0x04, (((start_idx) << 14) + ((len) << 4) + ((execute_while_loading) << 1) + ((load_mode) << 0)))

#define TT_OP_WH_RMWCIB0(Mask, Data, CfgRegAddr) \
  TT_OP_WH(0xb3, (((Mask) << 16) + ((Data) << 8) + ((CfgRegAddr) << 0)))

#define TT_OP_WH_RMWCIB1(Mask, Data, CfgRegAddr) \
  TT_OP_WH(0xb4, (((Mask) << 16) + ((Data) << 8) + ((CfgRegAddr) << 0)))

#define TT_OP_WH_RMWCIB2(Mask, Data, CfgRegAddr) \
  TT_OP_WH(0xb5, (((Mask) << 16) + ((Data) << 8) + ((CfgRegAddr) << 0)))

#define TT_OP_WH_RMWCIB3(Mask, Data, CfgRegAddr) \
  TT_OP_WH(0xb6, (((Mask) << 16) + ((Data) << 8) + ((CfgRegAddr) << 0)))

#define TT_OP_WH_RSTDMA\
  TT_OP_WH(0x44, 0)

#define TT_OP_WH_SEMGET(sem_sel) \
  TT_OP_WH(0xa5, (((sem_sel) << 2)))

#define TT_OP_WH_SEMINIT(max_value, init_value, sem_sel) \
  TT_OP_WH(0xa3, (((max_value) << 20) + ((init_value) << 16) + ((sem_sel) << 2)))

#define TT_OP_WH_SEMPOST(sem_sel) \
  TT_OP_WH(0xa4, (((sem_sel) << 2)))

#define TT_OP_WH_SEMWAIT(stall_res, sem_sel, wait_sem_cond) \
  TT_OP_WH(0xa6, (((stall_res) << 15) + ((sem_sel) << 2) + ((wait_sem_cond) << 0)))

#define TT_OP_WH_SETADC(CntSetMask, ChannelIndex, DimensionIndex, Value) \
  TT_OP_WH(0x50, (((CntSetMask) << 21) + ((ChannelIndex) << 20) + ((DimensionIndex) << 18) + ((Value) << 0)))

#define TT_OP_WH_SETADCXX(CntSetMask, x_end2, x_start) \
  TT_OP_WH(0x5e, (((CntSetMask) << 21) + ((x_end2) << 10) + ((x_start) << 0)))

#define TT_OP_WH_SETADCXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_WH(0x51, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_WH_SETADCZW(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask) \
  TT_OP_WH(0x54, (((CntSetMask) << 21) + ((Ch1_Y) << 15) + ((Ch1_X) << 12) + ((Ch0_Y) << 9) + ((Ch0_X) << 6) + ((BitMask) << 0)))

#define TT_OP_WH_SETASHRMH(reg_mask, halo_mask) \
  TT_OP_WH(0x1e, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_WH_SETASHRMH0(reg_mask, halo_mask) \
  TT_OP_WH(0x1a, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_WH_SETASHRMH1(reg_mask, halo_mask) \
  TT_OP_WH(0x1b, (((reg_mask) << 1) + ((halo_mask) << 0)))

#define TT_OP_WH_SETASHRMV(reg_mask2) \
  TT_OP_WH(0x1c, (((reg_mask2) << 0)))

#define TT_OP_WH_SETC16(setc16_reg, setc16_value) \
  TT_OP_WH(0xb2, (((setc16_reg) << 16) + ((setc16_value) << 0)))

#define TT_OP_WH_SETDMAREG(Payload_SigSelSize, Payload_SigSel, SetSignalsMode, RegIndex16b) \
  TT_OP_WH(0x45, (((Payload_SigSelSize) << 22) + ((Payload_SigSel) << 8) + ((SetSignalsMode) << 7) + ((RegIndex16b) << 0)))
#define TT_OP_WH_SETPKEDGOF(y_end, y_start, x_end, x_start) \
  TT_OP_WH(0x1d, (((y_end) << 12) + ((y_start) << 8) + ((x_end) << 4) + ((x_start) << 0)))

#define TT_OP_WH_SETRWC(clear_ab_vld, rwc_cr, rwc_d, rwc_b, rwc_a, BitMask) \
  TT_OP_WH(0x37, (((clear_ab_vld) << 22) + ((rwc_cr) << 18) + ((rwc_d) << 14) + ((rwc_b) << 10) + ((rwc_a) << 6) + ((BitMask) << 0)))

#define TT_OP_WH_SFPABS(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7d, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPADD(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x85, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPADDI(imm16_math, lreg_dest, instr_mod1) \
  TT_OP_WH(0x75, (((imm16_math) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPAND(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7e, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPCAST(lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x90, (((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPCOMPC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x8b, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPCONFIG(imm16_math, config_dest, instr_mod1) \
  TT_OP_WH(0x91, (((imm16_math) << 8) + ((config_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPDIVP2(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x76, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPENCC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x8a, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPEXEXP(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x77, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPEXMAN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x78, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPIADD(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x79, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPLOAD(lreg_ind, instr_mod0, sfpu_addr_mode, dest_reg_addr) \
  TT_OP_WH(0x70, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((sfpu_addr_mode) << 14) + ((dest_reg_addr) << 0)))

#define TT_OP_WH_SFPLOADI(lreg_ind, instr_mod0, imm16) \
  TT_OP_WH(0x71, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((imm16) << 0)))

#define TT_OP_WH_SFPLOADMACRO(lreg_ind, instr_mod0, sfpu_addr_mode, dest_reg_addr) \
  TT_OP_WH(0x93, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((sfpu_addr_mode) << 14) + ((dest_reg_addr) << 0)))

#define TT_OP_WH_SFPLUT(lreg_ind, instr_mod0, dest_reg_addr) \
  TT_OP_WH(0x73, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((dest_reg_addr) << 0)))

#define TT_OP_WH_SFPLUTFP32(lreg_dest, instr_mod1) \
  TT_OP_WH(0x95, (((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPLZ(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x81, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPMAD(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x84, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPMOV(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7c, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPMUL(lreg_src_a, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x86, (((lreg_src_a) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPMULI(imm16_math, lreg_dest, instr_mod1) \
  TT_OP_WH(0x74, (((imm16_math) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPNOP\
  TT_OP_WH(0x8f, 0)

#define TT_OP_WH_SFPNOT(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x80, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPOR(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7f, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPPOPC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x88, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPPUSHC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x87, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSETCC(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7b, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSETEXP(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x82, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSETMAN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x83, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSETSGN(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x89, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSHFT(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x7a, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSHFT2(imm12_math, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x94, (((imm12_math) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPSTORE(lreg_ind, instr_mod0, sfpu_addr_mode, dest_reg_addr) \
  TT_OP_WH(0x72, (((lreg_ind) << 20) + ((instr_mod0) << 16) + ((sfpu_addr_mode) << 14) + ((dest_reg_addr) << 0)))

#define TT_OP_WH_SFPSWAP(imm12_math, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x92, (((imm12_math) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPTRANSP(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x8c, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFPXOR(imm12_math, lreg_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x8d, (((imm12_math) << 12) + ((lreg_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SFP_STOCH_RND(rnd_mode, imm8_math, lreg_src_b, lreg_src_c, lreg_dest, instr_mod1) \
  TT_OP_WH(0x8e, (((rnd_mode) << 21) + ((imm8_math) << 16) + ((lreg_src_b) << 12) + ((lreg_src_c) << 8) + ((lreg_dest) << 4) + ((instr_mod1) << 0)))

#define TT_OP_WH_SHIFTDMAREG(OpBisConst, OpSel, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x5c, (((OpBisConst) << 23) + ((OpSel) << 18) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_SHIFTXA(log2_amount2, shift_mode) \
  TT_OP_WH(0x17, (((log2_amount2) << 2) + ((shift_mode) << 0)))

#define TT_OP_WH_SHIFTXB(addr_mode, rot_shift, shift_row) \
  TT_OP_WH(0x18, (((addr_mode) << 15) + ((rot_shift) << 10) + ((shift_row) << 0)))

#define TT_OP_WH_STALLWAIT(stall_res, wait_res) \
  TT_OP_WH(0xa2, (((stall_res) << 15) + ((wait_res) << 0)))

#define TT_OP_WH_STOREIND(MemHierSel, SizeSel, RegSizeSel, OffsetIndex, AutoIncSpec, DataRegIndex, AddrRegIndex) \
  TT_OP_WH(0x66, (((MemHierSel) << 23) + ((SizeSel) << 22) + ((RegSizeSel) << 21) + ((OffsetIndex) << 14) + ((AutoIncSpec) << 12) + ((DataRegIndex) << 6) + ((AddrRegIndex) << 0)))

#define TT_OP_WH_STOREREG(TdmaDataRegIndex, RegAddr) \
  TT_OP_WH(0x67, (((TdmaDataRegIndex) << 18) + ((RegAddr) << 0)))

#define TT_OP_WH_SUBDMAREG(OpBisConst, ResultRegIndex, OpBRegIndex, OpARegIndex) \
  TT_OP_WH(0x59, (((OpBisConst) << 23) + ((ResultRegIndex) << 12) + ((OpBRegIndex) << 6) + ((OpARegIndex) << 0)))

#define TT_OP_WH_TBUFCMD\
  TT_OP_WH(0x4b, 0)

#define TT_OP_WH_TRNSPSRCA\
  TT_OP_WH(0x14, 0)

#define TT_OP_WH_TRNSPSRCB\
  TT_OP_WH(0x16, 0)

#define TT_OP_WH_UNPACR(Unpack_block_selection, AddrMode, CfgContextCntInc, CfgContextId, AddrCntContextId, OvrdThreadId, SetDatValid, rareb_en, ZeroWrite2, AutoIncContextID, RowSearch, SearchCacheFlush, Last) \
  TT_OP_WH(0x42, (((Unpack_block_selection) << 23) + ((AddrMode) << 15) + ((CfgContextCntInc) << 13) + ((CfgContextId) << 10) + ((AddrCntContextId) << 8) + ((OvrdThreadId) << 7) + ((SetDatValid) << 6) + ((rareb_en) << 5) + ((ZeroWrite2) << 4) + ((AutoIncContextID) << 3) + ((RowSearch) << 2) + ((SearchCacheFlush) << 1) + ((Last) << 0)))

#define TT_OP_WH_UNPACR_NOP(Unpack_block_selection, NoOp) \
  TT_OP_WH(0x43, (((Unpack_block_selection) << 23) + ((NoOp) << 0)))

#define TT_OP_WH_WRCFG(GprAddress, wr128b, CfgReg) \
  TT_OP_WH(0xb0, (((GprAddress) << 16) + ((wr128b) << 15) + ((CfgReg) << 0)))

#define TT_OP_WH_XMOV(Mov_block_selection, Last) \
  TT_OP_WH(0x40, (((Mov_block_selection) << 23) + ((Last) << 0)))

#define TT_OP_WH_ZEROACC(clear_mode, AddrMode, dst) \
  TT_OP_WH(0x10, (((clear_mode) << 19) + ((AddrMode) << 15) + ((dst) << 0)))

#define TT_OP_WH_ZEROSRC(zero_val, write_mode, bank_mask, src_mask) \
  TT_OP_WH(0x11, (((zero_val) << 4) + ((write_mode) << 3) + ((bank_mask) << 2) + ((src_mask) << 0)))

