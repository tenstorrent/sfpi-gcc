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

/* Every MOP semantic the mop-form pass relies on is recorded here as a
   table fact with provenance, following the macro-table discipline
   (rvtt-macro-tables-*.def): a fact is admitted only when the reference
   simulator executes it and/or a production kernel exercises it.  A MOP
   behavior NOT recorded here is unproven and the pass must refuse any
   shape that would depend on it.

   Provenance sources (all facts below):
     [SIM]  the pinned reference simulator -- its MOP expander,
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
            (sfpu-ops-wh.h:95, sfpu-ops-bh.h:101).

   WH and BH are identical for every fact in this table (verified against
   both tile_regs.h files and both TT_OP headers).  QSR's MOP encoding
   differs (TT_OP_QSR_MOP carries a `done' bit and narrower fields;
   sfpu-ops-qsr.h:62) and its expander semantics are unproven here: the
   pass hard-refuses QSR by gate.  */

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
       cfg[2] (not used by this pass).  [PROD] the lzA/lhA halo classes
       program four per-iteration instructions through exactly these
       slots, and llk_math_reduce_runtime_custom.h uses SETRWC as a MOP
       per-iteration address stepper -- the production precedent for
       carrying typed address-progression words in template slots.

   With zmask == 0 and flags <= 2, config words 2, 7, 8 are never
   consumed (cfg[7]/cfg[8] are the zmask-set arms; cfg[2] is the flags&1
   arm) -- dead-by-construction, so the pass does not write them.  Every
   slot the programmed flags value DOES consume is written explicitly,
   unused trailing step slots with the FIFO-swallowed Tensix NOP.  Any
   template needing zmask skipping or the B side is a later class and
   refuses now.  */

/* Typed SETRWC (WH/BH opcode 0x37) is the only step word this increment
   encodes into template slots.  Field order in the rvtt_ttsetrwc_wh_bh
   pattern == assembler field order == TT_OP_*_SETRWC macro order:
   (clear_ab_vld<<22, rwc_cr<<18, rwc_d<<14, rwc_b<<10, rwc_a<<6,
   BitMask).  [ENC] riscv-opc-sfpu-insns.h ttsetrwc
   "J22u2,J18u4,J14u4,J10u4,J6u4,J0u6"; sfpu-ops-wh.h:211 /
   sfpu-ops-bh.h:224.
   The typed Dst face advance (rvtt_ttdstface_wh_bh) is emission data
   owned by its pattern: exactly two CR-mode Dst += 8 words,
   SETRWC (0, 4, 8, 0, 0, 4) twice (rvtt.md, "issued twice to advance
   exactly one face") -- the pass carries those two words, never a
   decoded guess.  */

/* The zmask consumed by mop_type 0 is (MOP word bits 15:0) |
   (MOP_CFG-set high half << 16); MOP_CFG bits 15:0 set that persistent
   per-thread high half.  [SIM] mop_cfg(): mop_zmask_hi16 = bits<15,0>,
   NonContractualBehavior if (inst & 0xFF0000); mop_expander: zmask =
   zlo | hi16 << 16.  [PROD] ckernel_unpack_template::run (count, zmask):
   TT_MOP_CFG (zmask >> 16) then TT_MOP (0, count-1, zmask & 0xFFFF).
   The pass emits MOP_CFG 0 immediately before every formed MOP so the
   zmask == 0 iteration path is proven regardless of prior thread
   state.  */

/* mop_type 0 flags live in MOP config word 1 (bits 1:0; the unpack
   template's unpackB | unpack_halo << 1) and must be <= 3.  [SIM]
   mop_expander: flags = mop_cfg[1], TTSIM_VERIFY (flags <= 3); [PROD]
   ckernel_unpack_template::program mop_cfg[1].  The formed class always
   writes 0.  */

/* -- MOP config register file ----------------------------------------- */

/* The nine 32-bit MOP template registers are MMIO words at
   TENSIX_MOP_CFG_BASE + 4*i, i = 0..8, decoded per issuing thread
   (RISC id -> pipe).  Base 0xFFB80000 on both WH and BH.  [SIM]
   tile_regs.h TENSIX_MOP_CFG_BASE (wh:13, bh:15) and
   tensix_mop_cfg_wr32 (offset/4 indexes mop_cfg[pipe][]); [PROD]
   ckernel_template.h program() writes through
   `(volatile uint32_t *) TENSIX_MOP_CFG_BASE'.  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_CFG_MMIO_BASE = 0xFFB80000;
constexpr unsigned XTT_MOP_CFG_FLAGS_INDEX = 1; /* unpack-template flags */
constexpr unsigned XTT_MOP_CFG_A0_INDEX = 3;    /* unpack-template A0 */

/* The MOP config MMIO block decodes a 4KB page; the write handler
   accepts exactly the nine aligned words (offsets 0x0..0x20) and
   nothing else.  [SIM] tile_regs.h TENSIX_MOP_CFG_{BASE,LIMIT}
   (wh:13-14, bh:15-16); tile.cpp tensix_mop_cfg_wr32 (~3076): switch
   `case 0x0/4 ... 0x20/4', misaligned/other offsets fault.  A store
   into the page beyond the nine words has no recorded fact and
   refuses (mop-template-slot-range-unproven).  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_CFG_MMIO_LIMIT = 0xFFB80FFF;
constexpr unsigned XTT_MOP_CFG_SLOTS = 9;

/* REPLAY is Tensix frontend opcode 0x04.  [SIM] replay_expander
   (`bits<31,24>(inst) == 0x04'); [ENC] TT_OP_{WH,BH}_REPLAY
   (sfpu-ops-wh.h / sfpu-ops-bh.h:155).  */
constexpr unsigned XTT_REPLAY_OPCODE = 0x04;

/* REPLAY word field decode and the record-window disposition fact.
   [SIM] the pinned reference simulator's replay expander
   arm: load_mode = bits<0,0>, execute_while_loading =
   bits<1,1>, reserved bits<3,2> TTSIM_VERIFY'd zero on BH/WH
   (TT_VERSION <= 1), len = bits<13,4> verified in [1,32], start_idx =
   bits<23,14> verified with start_idx + len <= 32 (the 32-slot
   per-thread buffer).  [SPEC] WormholeB0 REPLAY.md functional model +
   Diagrams/Out/Bits32_REPLAY.svg encoding (Load bit 0, Exec bit 1,
   Count bits 9:4 architected u6, Index bits 18:14 architected u5 --
   the sim decodes wider fields and verifies the same architected
   ranges).  THE DISPOSITION FACT: while a load window is open
   (replay_left > 0) every incoming frontend instruction is STORED to
   the buffer and pushed to the backend instruction FIFO ONLY when
   execute_while_loading was set -- a load_mode=1/exec=0 record's next
   LEN delivered words are architecturally SWALLOWED (zero backend
   effects; the [SPEC] model's `if Exec: yield' arm is the same
   statement).  A word arriving inside an open window is stored
   blindly, REPLAY words included (the window arm precedes the REPLAY
   decode arm in both models), so recorded content is data until a
   playback delivers it.  */
constexpr unsigned XTT_REPLAY_LOAD_BIT = 0;
constexpr unsigned XTT_REPLAY_EXEC_BIT = 1;
constexpr uint32_t XTT_REPLAY_RESERVED_MASK = 0xC;
constexpr unsigned XTT_REPLAY_LEN_SHIFT = 4;
constexpr uint32_t XTT_REPLAY_LEN_MASK = 0x3FF;	  /* [SIM] bits<13,4> */
constexpr unsigned XTT_REPLAY_IDX_SHIFT = 14;
constexpr uint32_t XTT_REPLAY_IDX_MASK = 0x3FF;	  /* [SIM] bits<23,14> */
constexpr unsigned XTT_REPLAY_BUF_SLOTS = 32;

/* -- Instruction-delivery MMIO census (the volatile-store proof) ------- */

/* The RISC instruction-FIFO aperture: a 32-bit MMIO store of a raw
   instruction word at TENSIX_INST_BASE word 0 -- the ONLY accepted
   offset ([SIM] tile.cpp tensix_inst_wr32 (~3155): TTSIM_VERIFY
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
   TENSIX_PC_BUF_MOP_SYNC 0x8, TENSIX_PC_BUF_SEMAPHORE(i) 0x20+4i
   (wh:131-135, bh:136-140); tile.cpp tensix_pc_buf_wr32 (~3227):
   exactly those cases, everything else faults.  Stores at unproven
   offsets refuse (pc-buf-write-unproven).  */
constexpr unsigned HOST_WIDE_INT XTT_PC_BUF_MMIO_BASE = 0xFFE80000;
constexpr unsigned HOST_WIDE_INT XTT_PC_BUF_MMIO_LIMIT = 0xFFE80FFF;
constexpr unsigned XTT_PC_BUF_TENSIX_SYNC_OFFSET = 0x4;
constexpr unsigned XTT_PC_BUF_MOP_SYNC_OFFSET = 0x8;
constexpr unsigned XTT_PC_BUF_SEMAPHORE_OFFSET = 0x20;

/* The RISCV debug-register block: the hardware documentation records
   an instruction-
   injection interface behind it (DBG_INSTRN_BUF*, tile_regs.h wh/bh:55;
   LLK ckernel_debug.h), and the simulator leaves those offsets
   unimplemented -- so no store into the block has a recorded
   PRGM/LaneConfig/CC-inertness fact and every one refuses
   (debug-regs-write-unproven).  [SIM] tile_regs.h
   RISCV_DEBUG_REGS_{BASE,LIMIT} (wh/bh:5-6).  */
constexpr unsigned HOST_WIDE_INT XTT_DEBUG_REGS_MMIO_BASE = 0xFFB12000;
constexpr unsigned HOST_WIDE_INT XTT_DEBUG_REGS_MMIO_LIMIT = 0xFFB12FFF;

/* XTT_MMIO_STORE_INERT: outside the four blocks above, no MMIO-store
   decoder arm reaches instruction delivery or SFPU state.  [SIM]
   tile.cpp t_tile_mmio_wr32 (~3845) is a total decoder: its arms are
   TDMA regs, debug regs (refused above), NOC/overlay regs, the MOP
   config block, the Tensix GPR regfile, the instruction aperture, the
   PC_BUF block, the mailboxes, and the CFG register file; only
   tensix_inst_wr32 and tensix_mop_cfg_wr32 feed the instruction
   frontend, and no arm writes the SFPU LReg file or LaneConfig
   (l_regs/lane_config assignments exist only in the SFPU instruction
   executors, tensix.cpp).  Consequence: a volatile store to any OTHER
   constant address (L1 data, CFG, GPRs, semaphore/mailbox state) is
   inert for the PRGM freedom proof.  */

/* XTT_LINK_IMAGE_DISJOINT: every linker-allocated section and every
   C-runtime section anchor the harness scripts define lives inside
   the script MEMORY regions (L1 and the per-core local-data memories),
   all of which are disjoint from the four MMIO blocks above; the one
   script symbol deliberately pinned INTO an MMIO block is
   `__instrn_buffer' (the FIFO anchor).  [PROD]
   tests/helpers/ld/memory.<arch>.ld MEMORY regions and PROVIDE
   anchors.  Consequence: a store whose address derives in-bounds from
   a TU-defined object or a C-runtime data anchor cannot alias an
   instruction FIFO.  */

/* XTT_RESET_TEMPLATE_AUDITED: a MOP that launches before any in-TU
   template programming expands the thread-entry template state, which
   is audited: the registers reset to all-zero words -- a type-1 MOP
   over them is architecturally a silent no-op (zero loop lengths;
   [SIM] mop_expander's RTL-mirrored early return, citing RTL
   tt_mop_decode.sv:211-216), and a type-0 MOP expands the zero word,
   whose opcode (0x00) is an audited-table row.  [SIM] sim.h
   TensixState::mop_cfg lives in zero-initialized global tile state.
   Residual (review carry-forward): the hardware power-on/soft-reset
   value of the template registers is RTL-cited for type 1 but not
   independently confirmed for the type-0 zero-word path.  */

/* mop_type 1 (the ckernel_template double-loop class, the production
   math datacopy dispatch TTI_MOP (1, 0, 0)) consumes MOP config words
   0..8 -- outer/inner loop lengths from words 0/1 when the instruction
   word's own length fields are zero, start/end/loop/last ops from
   words 2..8 -- and NEVER the MOP_CFG zmask high half (the zmask is a
   mop_type-0 fact only).  [SIM] mop_expander mop_type != 0 arm reads
   mop_cfg[pipe][0..8] and no zmask state; [PROD]
   ckernel_template::{program,run}.  Consequence for the outward
   ownership proof: a caller's type-1 launch after a formed callee
   returns is proven safe by a rewrite of the config words alone; any
   other (type-0 or unclassifiable) launch additionally requires the
   zmask high half rewritten.  */

/* The MOP config registers are WRITE-ONLY from the RISC: the Tensix
   tile MMIO read decoder has no TENSIX_MOP_CFG case (reads fault as
   unimplemented).  [SIM] tile.cpp t_tile_mmio_rd32 (~3738): no
   TENSIX_MOP_CFG_BASE arm (the write side, tensix_mop_cfg_wr32, is
   ~3061/3895).  Consequence: a formed region can neither snapshot nor
   restore a caller's template -- there is no save/restore epilogue
   tier; the only sound discharge of a caller's live template is the
   outward ownership refusal (rtl-rvtt-mop-form.cc file header) or the
   caller's own re-programming protocol.  */

/* Instruction-FIFO pushes (MMIO stores of raw instruction words) are
   classified by the frontend opcode byte, bits 31:24 of the RAW word
   (instruction-buffer and template-register words are unshifted;
   only .text words carry the <<2 encoding).  [SIM] tensix_push_inst
   `opcode = bits<31,24>(inst)' switch: 0x01 -> mop_expander, 0x03 ->
   mop_cfg, everything else never reads MOP template state.  Runtime-
   composed pushes (the TT_OP macro family: `(opcode << 24) + params')
   are classified by the constant opcode base of their PLUS/BIT_IOR
   composition under the tt-op-field-discipline axiom (runtime operands
   stay inside their bit fields -- the discipline the macros themselves
   encode; [PROD] ckernel_ops.h TT_OP).  A push with no constant base
   is unclassifiable and the outward proof treats it as a MOP launch
   consuming everything.  */

/* Reprogramming the MOP config registers while a previously issued MOP
   is still streaming is a race (the expander reads them live).  The
   production guard is mop_sync(): a blocking store of 0 to PC_BUF word
   2 (TENSIX_PC_BUF_BASE + TENSIX_PC_BUF_MOP_SYNC) that stalls the RISC
   until outstanding MOPs complete.  [PROD] ckernel.h mop_sync();
   [SIM] tile_regs.h TENSIX_PC_BUF_BASE 0xFFE80000 /
   TENSIX_PC_BUF_MOP_SYNC 0x8 (wh and bh identical),
   tensix_pc_buf_wr32 accepts the write.  The pass emits this guard at
   the head of every config block, exactly the production protocol.  */
constexpr unsigned HOST_WIDE_INT XTT_MOP_SYNC_MMIO_ADDR = 0xFFE80008;

/* -- REPLAY co-ownership ---------------------------------------------- */

/* A REPLAY word placed in a MOP template slot references the SAME
   32-slot per-thread replay buffer as directly delivered REPLAY words:
   the expander routes template slots through replay_expander
   unchanged.  [SIM] mop_expander -> replay_expander; replay_expander
   REPLAY arm: TTSIM_VERIFY len in [1,32], start_idx < 32, and
   UndefinedBehavior when start_idx + len > 32 -- the S+L <= 32
   co-ownership invariant.  The pass re-verifies start + len against the
   buffer size at emit time and refuses the overflow near-miss rather
   than emitting a word the hardware model rejects.  [PROD]
   ckernel_sfpu_topk_xl.h records replay ranges and runs them as MOP
   template operands (the layering precedent).  */

/* Tensix NOP is opcode 0x02 (word 0x02000000); NOP template slots and
   NOP expansion products are swallowed at the instruction FIFO and
   deliver nothing.  [SIM] tensix.cpp TENSIX_NOP and
   tensix_push_inst_fifo early-return.  Recorded for completeness; the
   formed class never writes a NOP slot because its dead slots are
   simply not consumed (see the zmask == 0 fact above).  */

#endif /* GCC_RVTT_MOP_TABLES_H */
