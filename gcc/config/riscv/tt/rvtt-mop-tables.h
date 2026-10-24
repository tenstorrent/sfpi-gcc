/* Tensix MOP loop-delivery capability table.  -*- C++ -*-
   Copyright (C) 2026 Tenstorrent Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_RVTT_MOP_TABLES_H
#define GCC_RVTT_MOP_TABLES_H

/* The MOP, MOP_CFG and REPLAY semantics the compiler relies on, each
   with its source, in the same style as rvtt-macro-tables-*.def.  A fact
   is recorded only when the reference simulator implements it or a
   production kernel uses it; a transformation must refuse any shape that
   depends on behaviour not recorded here.

   Sources:
     [SIM]  the reference simulator -- its MOP expander,
            MOP-config path, replay expander, instruction-push path,
            MOP-config/PC-buffer store handlers, and generated tile
            register headers.
     [PROD] tt-llk (tt-metal tt_metal/tt-llk) tt_llk_{wormhole_b0,
            blackhole}/common/inc/ckernel_template.h --
            ckernel_unpack_template::{program,run,lA} and ckernel.h
            mop_sync; production MOP-over-SFPU users: ckernel_sfpu_topk_xl.h
            (1-slot lA(REPLAY,NOP) merge loops), llk_math_transpose_dest.h,
            llk_math_reduce_runtime_custom.h.
     [ENC]  sfpi binutils opcodes/riscv-opc-sfpu-insns.h (ttmop
            "J23u1,J16u7,J0u16", ttmopcfg "J0u24") and
            include/opcode/riscv-opc-sfpu.h (MATCH_SFPMOP 0x01000000,
            MATCH_SFPMOPCFG 0x03000000); in-tree TT_OP_{WH,BH}_MOP{,_CFG}
            (sfpu-ops-wh.h, sfpu-ops-bh.h).

   WH and BH agree on every fact here (both tile_regs.h files and both
   TT_OP headers).  QSR's MOP encoding differs (TT_OP_QSR_MOP has a `done'
   bit and narrower fields; sfpu-ops-qsr.h) and its expander semantics are
   not recorded, so QSR is refused.  */

/* -- Instruction words ------------------------------------------------ */

/* MOP is Tensix frontend opcode 0x01; MOP_CFG is 0x03.  [SIM]
   tensix_push_inst switch (case 0x01 -> mop_expander, case 0x03 ->
   mop_cfg); [ENC] MATCH_SFPMOP / MATCH_SFPMOPCFG.  */
constexpr unsigned XTT_MOP_OPCODE = 0x01;
constexpr unsigned XTT_MOP_CFG_OPCODE = 0x03;

/* MOP word layout (WH/BH): bit 23 = mop_type, bits 22:16 = loop_count,
   bits 15:0 = zmask low half.  [SIM] mop_expander bits<23,23>/<22,16>/
   <15,0>; [ENC] ttmop "J23u1,J16u7,J0u16"; TT_OP_*_MOP.  */

/* mop_type 0 (the ckernel_unpack_template class) executes loop_count + 1
   iterations; loop_count must be <= 127.  [SIM] mop_expander:
   TTSIM_VERIFY (loop_count <= 127); loop `for (i = 0; i <= loop_count)'.
   [PROD] ckernel_unpack_template::run (count) issues
   TT_MOP (0, count - 1, 0).  */
constexpr unsigned XTT_MOP0_MAX_ITERATIONS = 128;

/* mop_type 0 per-iteration emission with zmask == 0:

     flags == 0: exactly one instruction, MOP config word 3 (the
       template's A0 slot), routed through the same replay expander as a
       directly pushed word.  [PROD] ckernel_unpack_template::program
       stores A0 at mop_cfg[3]; the lA class programs exactly
       { flags = 0, A0 = op, skipA = op' }.

     flags == 2 (the unpack template's halo bit): config words 3, 4, 5,
       6 in that order, each through the replay expander.  [SIM]
       mop_expander mop_type==0 arm: zmask bit clear -> emit mop_cfg[3];
       flags&2 -> additionally emit cfg[4], cfg[5], cfg[6]; flags&1 ->
       cfg[2].  [PROD] the lzA/lhA halo classes program four
       per-iteration instructions through these slots, and
       llk_math_reduce_runtime_custom.h uses SETRWC in a template slot as
       a per-iteration address stepper.

   With zmask == 0 and flags <= 2, config words 2, 7 and 8 are never
   read (cfg[7]/cfg[8] are the zmask-set arms; cfg[2] is the flags&1
   arm), so they need not be written.  Every slot that the programmed
   flags value does read must be written explicitly, unused trailing
   step slots with the Tensix NOP.  Templates that need zmask skipping
   or the B side are not supported.  */

/* Typed SETRWC (WH/BH opcode 0x37) is the only step word with a
   recorded template-slot fact.  Field order in the rvtt_ttsetrwc_wh_bh
   pattern == assembler field order == TT_OP_*_SETRWC macro order:
   (clear_ab_vld<<22, rwc_cr<<18, rwc_d<<14, rwc_b<<10, rwc_a<<6,
   BitMask).  [ENC] riscv-opc-sfpu-insns.h ttsetrwc
   "J22u2,J18u4,J14u4,J10u4,J6u4,J0u6"; sfpu-ops-wh.h /
   sfpu-ops-bh.h.
   The typed Dst face advance (rvtt_ttdstface_wh_bh) is two CR-mode
   Dst += 8 words, SETRWC (0, 4, 8, 0, 0, 4) twice (see rvtt.md); a
   template copies those two words from the pattern.  */

/* The zmask consumed by mop_type 0 is (MOP word bits 15:0) |
   (MOP_CFG-set high half << 16); MOP_CFG bits 15:0 set that persistent
   per-thread high half.  [SIM] mop_cfg(): mop_zmask_hi16 = bits<15,0>,
   NonContractualBehavior if (inst & 0xFF0000); mop_expander: zmask =
   zlo | hi16 << 16.  [PROD] ckernel_unpack_template::run (count, zmask):
   TT_MOP_CFG (zmask >> 16) then TT_MOP (0, count-1, zmask & 0xFFFF).
   A formed MOP is therefore immediately preceded by MOP_CFG 0, so that
   the zmask == 0 path holds whatever the prior thread state.  */

/* mop_type 0 flags live in MOP config word 1 (bits 1:0; the unpack
   template's unpackB | unpack_halo << 1) and must be <= 3.  [SIM]
   mop_expander: flags = mop_cfg[1], TTSIM_VERIFY (flags <= 3); [PROD]
   ckernel_unpack_template::program mop_cfg[1].  A flags == 0 template
   writes 0 there.  */

/* -- MOP config register file ----------------------------------------- */

/* The nine 32-bit MOP template registers are MMIO words at
   TENSIX_MOP_CFG_BASE + 4*i, i = 0..8, decoded per issuing thread
   (RISC id -> pipe).  Base 0xFFB80000 on both WH and BH.  [SIM]
   tile_regs.h TENSIX_MOP_CFG_BASE and
   tensix_mop_cfg_wr32 (offset/4 indexes mop_cfg[pipe][]); [PROD]
   ckernel_template.h program() writes through
   `(volatile uint32_t *) TENSIX_MOP_CFG_BASE'.  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_CFG_MMIO_BASE = 0xFFB80000;
constexpr unsigned XTT_MOP_CFG_FLAGS_INDEX = 1; /* unpack-template flags */
constexpr unsigned XTT_MOP_CFG_A0_INDEX = 3;    /* unpack-template A0 */

/* The MOP config MMIO block decodes a 4KB page; the write handler
   accepts exactly the nine aligned words (offsets 0x0..0x20) and
   nothing else.  [SIM] tile_regs.h TENSIX_MOP_CFG_{BASE,LIMIT};
   tile.cpp tensix_mop_cfg_wr32: switch
   `case 0x0/4 ... 0x20/4', misaligned/other offsets fault.  A store
   into the page beyond the nine words is refused
   (mop-template-slot-range-unproven).  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_CFG_MMIO_LIMIT = 0xFFB80FFF;
constexpr unsigned XTT_MOP_CFG_SLOTS = 9;

/* REPLAY is Tensix frontend opcode 0x04.  [SIM] replay_expander
   (`bits<31,24>(inst) == 0x04'); [ENC] TT_OP_{WH,BH}_REPLAY
   (sfpu-ops-wh.h / sfpu-ops-bh.h).  */
constexpr unsigned XTT_REPLAY_OPCODE = 0x04;

/* REPLAY word fields and record-window behaviour.
   [SIM] the reference simulator's replay expander
   arm: load_mode = bits<0,0>, execute_while_loading =
   bits<1,1>, reserved bits<3,2> TTSIM_VERIFY'd zero on BH/WH
   (TT_VERSION <= 1), len = bits<13,4> verified in [1,32], start_idx =
   bits<23,14> verified with start_idx + len <= 32 (the 32-slot
   per-thread buffer).  [SPEC] WormholeB0 REPLAY.md functional model +
   Diagrams/Out/Bits32_REPLAY.svg encoding (Load bit 0, Exec bit 1,
   Count bits 9:4 architected u6, Index bits 18:14 architected u5 --
   the sim decodes wider fields and verifies the same architected
   ranges).  While a load window is open (replay_left > 0) every
   incoming frontend instruction is stored to the buffer, and is pushed
   to the backend instruction FIFO only when execute_while_loading was
   set: the next LEN words after a load_mode=1/exec=0 REPLAY have no
   backend effect (the [SPEC] model's `if Exec: yield' arm).  Words
   inside an open window are stored unconditionally, REPLAY words
   included (the window arm precedes the REPLAY decode arm in both
   models), so recorded content is data until a playback delivers
   it.  */
constexpr unsigned XTT_REPLAY_LOAD_BIT = 0;
constexpr unsigned XTT_REPLAY_EXEC_BIT = 1;
constexpr uint32_t XTT_REPLAY_RESERVED_MASK = 0xC;
constexpr unsigned XTT_REPLAY_LEN_SHIFT = 4;
constexpr uint32_t XTT_REPLAY_LEN_MASK = 0x3FF;	  /* [SIM] bits<13,4> */
constexpr unsigned XTT_REPLAY_IDX_SHIFT = 14;
constexpr uint32_t XTT_REPLAY_IDX_MASK = 0x3FF;	  /* [SIM] bits<23,14> */
constexpr unsigned XTT_REPLAY_BUF_SLOTS = 32;

/* -- Instruction-delivery MMIO blocks (volatile-store analysis) ----- */

/* The RISC instruction-FIFO aperture: a 32-bit MMIO store of a raw
   instruction word at TENSIX_INST_BASE word 0, the only accepted
   offset ([SIM] tile.cpp tensix_inst_wr32: TTSIM_VERIFY
   (!offset)).  [SIM] tile_regs.h TENSIX_INST_{BASE,LIMIT} (identical
   wh/bh); [PROD] the harness linker scripts pin the ABI anchor to it
   (tests/helpers/ld/memory.<arch>.ld: `PROVIDE(__instrn_buffer =
   0xFFE40000)'), and the TT_ macro family (ckernel_ops.h) delivers
   runtime-composed words as `ckernel::instrn_buffer[0] = TT_OP_*'
   volatile stores through exactly that anchor.  */
constexpr unsigned HOST_WIDE_INT XTT_INSTRN_BUF_MMIO_BASE = 0xFFE40000;
constexpr unsigned HOST_WIDE_INT XTT_INSTRN_BUF_MMIO_LIMIT = 0xFFE40FFF;

/* The PC_BUF block: the architected store targets are the blocking
   sync words and the eight semaphores -- none delivers an instruction
   word or touches PRGM/LaneConfig/CC.  [SIM] tile_regs.h
   TENSIX_PC_BUF_{BASE,LIMIT}, TENSIX_PC_BUF_TENSIX_SYNC 0x4,
   TENSIX_PC_BUF_MOP_SYNC 0x8, TENSIX_PC_BUF_SEMAPHORE(i) 0x20+4i;
   tile.cpp tensix_pc_buf_wr32: exactly those cases, everything else
   faults.  Stores at other offsets are refused
   (pc-buf-write-unproven).  */
constexpr unsigned HOST_WIDE_INT XTT_PC_BUF_MMIO_BASE = 0xFFE80000;
constexpr unsigned HOST_WIDE_INT XTT_PC_BUF_MMIO_LIMIT = 0xFFE80FFF;
constexpr unsigned XTT_PC_BUF_TENSIX_SYNC_OFFSET = 0x4;
constexpr unsigned XTT_PC_BUF_MOP_SYNC_OFFSET = 0x8;
constexpr unsigned XTT_PC_BUF_SEMAPHORE_OFFSET = 0x20;

/* The RISCV debug-register block contains an instruction-injection
   interface (DBG_INSTRN_BUF*, tile_regs.h; LLK ckernel_debug.h) that the
   simulator does not implement, so every store into the block is refused
   (debug-regs-write-unproven).  [SIM] tile_regs.h
   RISCV_DEBUG_REGS_{BASE,LIMIT}.  */
constexpr unsigned HOST_WIDE_INT XTT_DEBUG_REGS_MMIO_BASE = 0xFFB12000;
constexpr unsigned HOST_WIDE_INT XTT_DEBUG_REGS_MMIO_LIMIT = 0xFFB12FFF;

/* XTT_MMIO_STORE_INERT: outside the four blocks above, no MMIO-store
   decoder arm reaches instruction delivery or SFPU state.  [SIM]
   tile.cpp t_tile_mmio_wr32 is a total decoder: its arms are
   TDMA regs, debug regs (refused above), NOC/overlay regs, the MOP
   config block, the Tensix GPR regfile, the instruction aperture, the
   PC_BUF block, the mailboxes, and the CFG register file; only
   tensix_inst_wr32 and tensix_mop_cfg_wr32 feed the instruction
   frontend, and no arm writes the SFPU LReg file or LaneConfig
   (l_regs/lane_config assignments exist only in the SFPU instruction
   executors, tensix.cpp).  A volatile store to any other constant
   address (L1 data, CFG, GPRs, semaphore/mailbox state) therefore cannot
   affect PRGM state.  */

/* XTT_LINK_IMAGE_DISJOINT: every linker-allocated section and every
   C-runtime section anchor the harness scripts define lives inside
   the script MEMORY regions (L1 and the per-core local-data memories),
   all of which are disjoint from the four MMIO blocks above; the only
   script symbol placed inside an MMIO block is `__instrn_buffer' (the
   FIFO anchor).  [PROD] tests/helpers/ld/memory.<arch>.ld MEMORY regions
   and PROVIDE anchors.  So a store whose address is in bounds of a
   TU-defined object or a C-runtime data anchor cannot reach an
   instruction FIFO.  */

/* Reset-template observations are NOT a source-function entry contract.
   [SIM] sim.h TensixState::mop_cfg starts as zero-initialized tile state,
   and a zero-count type-1 expansion returns early.  Neither observation
   establishes current hardware state on entry to an arbitrary function.
   Issuance certificates require the current-CFG nine-slot MUST proof at
   every actual launch; no XTT_RESET_TEMPLATE_AUDITED permission is used.  */

/* mop_type 1 (the ckernel_template double-loop class, the production
   math datacopy dispatch TTI_MOP (1, 0, 0)) consumes MOP config words
   0..8 -- outer/inner loop lengths from words 0/1 when the instruction
   word's own length fields are zero, start/end/loop/last ops from
   words 2..8 -- and never the MOP_CFG zmask high half, which only
   mop_type 0 reads.  [SIM] mop_expander mop_type != 0 arm reads
   mop_cfg[pipe][0..8] and no zmask state; [PROD]
   ckernel_template::{program,run}.  So a caller's type-1 launch after a
   formed callee returns is safe once the caller rewrites the config
   words; any other (type-0 or unclassifiable) launch also needs the zmask
   high half rewritten.  */

/* The MOP config registers are write-only from the RISC: the Tensix
   tile MMIO read decoder has no TENSIX_MOP_CFG case (reads fault as
   unimplemented).  [SIM] tile.cpp t_tile_mmio_rd32: no
   TENSIX_MOP_CFG_BASE arm (only the write side, tensix_mop_cfg_wr32,
   exists).  A formed region therefore cannot save and restore a
   caller's template; it may clobber the template only when the callers
   are shown not to depend on it (rvtt_mop_outward_owned_p) or reprogram
   it themselves.  */

/* Instruction-FIFO pushes (MMIO stores of raw instruction words) are
   classified by the frontend opcode byte, bits 31:24 of the RAW word
   (instruction-buffer and template-register words are unshifted;
   only .text words carry the <<2 encoding).  [SIM] tensix_push_inst
   `opcode = bits<31,24>(inst)' switch: 0x01 -> mop_expander, 0x03 ->
   mop_cfg, everything else never reads MOP template state.  Run-time
   composed pushes (the TT_OP macro family: `(opcode << 24) + params')
   require unsigned bounds proving their opcode, not a field-discipline
   promise from the macro spelling.  PHI/select unions retain every possible
   delivered class.  An unknown delivery word cannot certify physical
   issuance or template initialization.  */

/* Reprogramming the MOP config registers while a previously issued MOP
   is still streaming is a race (the expander reads them live).  The
   production guard is mop_sync(): a blocking store of 0 to PC_BUF word
   2 (TENSIX_PC_BUF_BASE + TENSIX_PC_BUF_MOP_SYNC) that stalls the RISC
   until outstanding MOPs complete.  [PROD] ckernel.h mop_sync();
   [SIM] tile_regs.h TENSIX_PC_BUF_BASE 0xFFE80000 /
   TENSIX_PC_BUF_MOP_SYNC 0x8 (wh and bh identical),
   tensix_pc_buf_wr32 accepts the write.  A template-programming block
   therefore starts with this guard, as the library code does.  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_SYNC_MMIO_ADDR = 0xFFE80008;

/* -- REPLAY co-ownership ---------------------------------------------- */

/* A REPLAY word placed in a MOP template slot references the same
   32-slot per-thread replay buffer as directly delivered REPLAY words:
   the expander routes template slots through replay_expander
   unchanged.  [SIM] mop_expander -> replay_expander; replay_expander
   REPLAY arm: TTSIM_VERIFY len in [1,32], start_idx < 32, and
   UndefinedBehavior when start_idx + len > 32.  A formed launch
   therefore checks start + len <= 32 and refuses otherwise.  [PROD]
   ckernel_sfpu_topk_xl.h records replay ranges and runs them as MOP
   template operands.  */

/* Tensix NOP is opcode 0x02 (word 0x02000000); NOP template slots and
   NOP expansion products are swallowed at the instruction FIFO and
   deliver nothing.  [SIM] tensix.cpp TENSIX_NOP and
   tensix_push_inst_fifo early-return.  A template slot that is
   consumed but must deliver nothing holds this word.  */

/* Whether every transitive caller of CFN rewrites the MOP template
   before its next launch, so a formation inside CFN may commit.  Defined in
   rtl-rvtt-mop-outward.cc; the assumptions are listed in the
   rtl-rvtt-mop-form.cc file header.  On a false return
   *WHY names the refusal, *WHY_FN the function that caused it and *HOW
   the mechanism.  */
extern bool rvtt_mop_outward_owned_p (function *cfn, const char **why,
				      const char **why_fn, const char **how);

#endif /* GCC_RVTT_MOP_TABLES_H */
