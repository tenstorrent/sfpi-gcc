/* SFPLOADMACRO target capability tables (macro planner Layer 4).
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

/* This header and its companions (rvtt-macro-tables.cc,
   rvtt-macro-tables-wh.def, rvtt-macro-tables-bh.def) are the DESIGNED HOME
   for raw SFPLOADMACRO encodings, per the non-negotiable compiler rule:
   "Raw encodings belong only in target encoding/capability tables, not
   semantic recognizers."  Nothing in this layer inspects IR, RTL, opcodes,
   operation names, or source structure.  The planner (Layers 2/3/5/6) asks
   encodability questions; this layer answers them from per-CPU data.

   Every constant is transcribed from the frozen Min/Max pass
   (/localdev/nkapre/sfpi-gcc-minmax-macro, review SHA 4e045d31d,
   gcc/config/riscv/tt/rtl-rvtt-loadmacro.cc) and from the legitimate
   TT_OP_* field-shift tables (sfpu-ops-{wh,bh}.h).  See NOTES-wp6-prep.md
   in this directory for the constant-by-constant provenance audit that the
   WP7 byte-parity gate consumes, including the explicit list of constants
   whose architectural meaning could NOT be established from the frozen pass
   plus in-tree docs.

   The file is deliberately freestanding (only <stdint.h>/<stddef.h>) so the
   standalone unit test rvtt-macro-tables-test.cc can compile it against a
   host compiler without GCC internals.  In-tree consumers translate between
   this layer's subunit enum and rvtt-effects.h's xtt_subunit_t (the values
   correspond one to one; see the comment on rvtt_macro::subunit_t).

   QSR is INTENTIONALLY TABLE-ABSENT: its SFPLOADMACRO seq_id / split-VD /
   'done' encoding has not been proven equivalent to WH/BH
   (SFPLOADMACRO_FORMATION.md "Architecture boundary" and "First opt-in
   executable slice").  rvtt_macro_caps_for_cpu (CPU_QSR) returns null and
   rvtt_macro_caps_refusal returns the stable refusal name
   "target-macro-encoding-unproven".  */

#ifndef GCC_RVTT_MACRO_TABLES_H
#define GCC_RVTT_MACRO_TABLES_H

#include <stdint.h>
#include <stddef.h>

namespace rvtt_macro {

/* Target CPUs.  Mirrors TARGET_XTT_TENSIX_{WH,BH,QSR}; kept independent so
   this layer stays freestanding.  */
enum cpu_t { CPU_WH, CPU_BH, CPU_QSR };

/* Execution subunits.  Values correspond one to one with
   rvtt-effects.h xtt_subunit_t (XTT_SU_NONE..XTT_SU_SYNC); duplicated here
   only to keep the table layer freestanding.  A static assertion in the
   in-tree consumer must pin the correspondence when Layer 3 lands.  */
enum subunit_t { SU_NONE, SU_SIMPLE, SU_MAD, SU_ROUND,
		 SU_LOAD, SU_STORE, SU_CFG, SU_SYNC };

/* Sentinel for a per-event programmed delay that is present in a proven
   sequence word but whose value could not be established from the frozen
   pass + docs (see NOTES-wp6-prep.md, unestablished list).  */
const uint8_t DELAY_UNKNOWN = 0xff;

/* Stable refusal vocabulary name returned for CPUs with no capability
   table (today: QSR).  Appended to, never renamed.  */
extern const char *const refusal_target_macro_encoding_unproven;

/* ------------------------------------------------------------------ */
/* Table row types (plain data; all raw words live in the .def files). */
/* ------------------------------------------------------------------ */

/* One physical SFPLOADMACRO address-modifier slot and the SETC16-programmed
   configuration registers that own it.  Programming VALUE fields (0,2,0)
   yields: zero Src increment/clear, Dst increment two, zero fidelity
   increment and bias -- the reset-state-equivalent Dst += 2 auto-increment
   program of the frozen pass (rtl-rvtt-loadmacro.cc:823-845).  */
struct addr_mod_slot
{
  uint8_t phys_slot;	/* architectural slot number		     */
  uint8_t src_reg;	/* SETC16 reg: Src increment and clear	     */
  uint8_t src_val;
  uint8_t dst_reg;	/* SETC16 reg: Dst increment		     */
  uint8_t dst_val;
  uint8_t fid_reg;	/* SETC16 reg: fidelity increment and bias   */
  uint8_t fid_val;
};

/* A (config_reg, value) SETC16 program step.  */
struct setc16_program
{
  uint8_t config_reg;
  uint16_t value;
};

/* One delayed event inside a proven per-macro sequence program.
   template_index is -1 for events realized without an instruction template
   (the delayed Store).  */
struct seq_event
{
  subunit_t unit;
  int8_t template_index;
  uint8_t delay;	/* 3-bit programmed delay, or DELAY_UNKNOWN  */
  bool is_store;
};

/* A proven, CRAQ/testsuite-validated per-macro sequence program: the ONLY
   sequence encodings this table can produce.  The bit-level format of the
   sequence word is NOT established (NOTES-wp6-prep.md); until an
   independent architectural reference is attached, encode_sequence () is a
   whole-word lookup over these entries and refuses everything else.
   The sequence word for macro K is written to SFPCONFIG dest 4+K
   (established from the frozen pass: select writes dests 4/5/6 for macros
   0/1/2, binary writes 4/5 for macros 0/1, unary shapes write 4).  */
struct seq_program
{
  const char *name;	/* provenance label, e.g. "minmax-binary-m0"  */
  uint8_t macro_index;
  uint8_t n_events;
  seq_event events[4];
  uint32_t word;
};

/* A proven misc word (SFPCONFIG dest 8).  For entries with
   store_mod0_in_bits_3_0, bits 3:0 carry the store data format (proven by
   the frozen pass computing 0x700 | mode at rtl-rvtt-loadmacro.cc:1572);
   remaining bit semantics are hypothesis-only, see NOTES-wp6-prep.md.  */
struct misc_word_entry
{
  const char *name;
  uint32_t word;
  bool store_mod0_in_bits_3_0;
  uint8_t store_mod0;		/* valid when store_mod0_in_bits_3_0  */
};

/* Instruction-template field bundle for the generic template-word packer.
   Templates are ordinary Tensix SFPU instruction words (op << 24 | fields)
   whose LREG destination field may carry a macro ROUTING SELECTOR instead
   of a physical LREG: observed selector values are 0xC and 0xD
   (comment-derived meanings: 0xC ~ the launch VD, 0xD ~ the macro
   transient LReg16; precise read/write-role semantics are PARTIALLY
   established -- see NOTES-wp6-prep.md).  */
struct template_spec
{
  uint8_t opcode;	/* Tensix opcode byte (0x92 SFPSWAP, ...)     */
  uint16_t imm12;	/* imm12_math field (bits 23:12)	      */
  uint8_t src_c;	/* lreg_c / lreg_src_c field (bits 11:8)      */
  uint8_t dest_sel;	/* lreg_dest field: 0-7 physical, 0xC/0xD sel */
  uint8_t mod1;		/* instr_mod1 field (bits 3:0)		      */
};

/* Hidden physical-LREG writes attached to a proven template encoding
   (e.g. the Min/Max SFPSWAP template writes physical L2, modeled in RTL as
   the fixed clobber of hard reg 82 = SFPU_REG_FIRST + 2).  MATCH is
   (word & mask) == value.  LREG_WRITE_MASK is a bitmask over LREG indices
   L0..L15 (bit N = LN), not GCC hard-register numbers.  */
struct hidden_write_entry
{
  uint32_t value;
  uint32_t mask;
  uint32_t lreg_write_mask;
};

/* A reference descriptor word: (SFPCONFIG dest, word) pairs for each
   validated shape.  This is AUDIT/EXPECTATION data for the WP7 byte-parity
   gate and the unit tests -- never an input to formation decisions.  */
struct ref_descriptor_word
{
  const char *shape;	/* "minmax-binary-min", "signbit", ...	      */
  uint8_t dest;
  uint32_t word;
};

/* ------------------------------------------------------------------ */
/* Per-CPU capability record.					      */
/* ------------------------------------------------------------------ */

struct caps
{
  cpu_t cpu;
  const char *name;

  /* Architectural resource counts.  Four instruction templates and four
     delay sequences exist (SFPLOADMACRO_FORMATION.md, MulInt32 handwritten
     init: "programs four instruction templates, four delay sequences");
     the frozen pass exercises templates 0-1 and sequences 0-2.  */
  unsigned n_templates;
  unsigned n_sequence_slots;
  unsigned delay_bits;		/* each programmed delay is 3 bits     */

  /* Launch-word field layout: opcode << 24 | lreg_ind << lreg_ind_shift
     | instr_mod0 << mod0_shift | addr_mode << addr_mode_shift | address.
     lreg_ind = (macro_index << 2) | vd for WH/BH.  */
  uint8_t launch_opcode;
  unsigned lreg_ind_shift;
  unsigned mod0_shift;
  unsigned addr_mode_shift;
  unsigned addr_mode_bits;
  unsigned address_bits;	/* 10-bit even Dst row address	       */

  /* Address-modifier machinery.  */
  unsigned no_increment_addr_mode;
  unsigned auto_increment_dst2_addr_mode;
  /* The launch's addr-mode field names its physical slot THROUGH the
     bank base bit (WH: ADDR_MOD_SET_Base maps modifier m to physical
     slot m or m+4).  When set, the emitted single-slot program is
     valid under the Base=1 SFPU platform contract -- proven by the
     typed LLK start/done bracket (cmath_common.h
     set/clear_addr_mod_base from _llk_math_eltwise_sfpu_start_/_done_)
     and adjudicated at sfpi-gcc 2a0ba1e6602 (laneAJ-evidence-20260817:
     programming the base-0 bank destroys the FPU's live ADDR_MOD_2 and
     corrupts the next tile's datacopy, so dual-slot "defensive"
     ownership is a miscompile, not a proof).  */
  bool needs_bank_base_ownership;
  const addr_mod_slot *addr_mod_slots;
  unsigned n_addr_mod_slots;

  /* SETC16 / SFPCONFIG field layouts (op << 24 | reg << 16 | value16;
     op << 24 | imm16 << 8 | dest << 4 | mod1).  */
  uint8_t setc16_opcode;
  uint8_t sfpconfig_opcode;

  /* Configuration destinations the planner owns under the formation
     contract (bitmask over dests 0..15): templates 0-1, sequences 0-2,
     misc.  */
  uint16_t owned_config_dests;

  /* Proven sequence programs / misc words / hidden template writes.  */
  const seq_program *seq_programs;
  unsigned n_seq_programs;
  const misc_word_entry *misc_words;
  unsigned n_misc_words;
  const hidden_write_entry *hidden_writes;
  unsigned n_hidden_writes;

  /* Reference expectation data (WP7 audit; not decision input).  */
  const ref_descriptor_word *ref_descriptors;
  unsigned n_ref_descriptors;
  const setc16_program *ref_addr_mod_setc16;   /* full owned program   */
  unsigned n_ref_addr_mod_setc16;

  /* Drain/latency cost constants.  Drain of every proven calendar is
     three issue slots ("maximum elapsed-instruction delay is three
     slots"); the generic rule remains "greatest remaining programmed
     delay", which for all proven programs evaluates to this value.  */
  unsigned proven_drain_slots;

  /* Frozen-pass profitability break-even (rows per run: BH 7, WH 8).
     REFERENCE DATA ONLY for the WP7 cost-model regression -- the Layer-6
     model must DERIVE these values; it must never read them.  */
  unsigned reference_breakeven_rows;
};

/* ------------------------------------------------------------------ */
/* Lookup.							      */
/* ------------------------------------------------------------------ */

/* Return the capability table for CPU, or null when no macro encoding is
   proven for it (QSR).  */
extern const caps *rvtt_macro_caps_for_cpu (cpu_t);

/* Stable refusal name for a null caps lookup ("target-macro-encoding-
   unproven"); null for CPUs that have a table.  */
extern const char *rvtt_macro_caps_refusal (cpu_t);

/* ------------------------------------------------------------------ */
/* Encoders / decoders (pure field packing; no policy).	      */
/* ------------------------------------------------------------------ */

/* Even ten-bit Dst row address predicate: odd rows alias the macro VD-high
   encoding bit and rows above 1023 exceed the WH/BH address field.  */
extern bool dst_address_encodable (const caps *, unsigned address);

/* Launch word.  Refuses out-of-range fields; ADDRESS must satisfy
   dst_address_encodable.  */
extern bool encode_launch (const caps *, unsigned macro_index, unsigned vd,
			   unsigned mode, unsigned addr_mode,
			   unsigned address, uint32_t *word);
extern bool decode_launch (const caps *, uint32_t word,
			   unsigned *macro_index, unsigned *vd,
			   unsigned *mode, unsigned *addr_mode,
			   unsigned *address);

/* SETC16 word (identical field layout on WH and BH).  */
extern bool encode_setc16 (const caps *, unsigned config_reg,
			   unsigned value, uint32_t *word);
extern bool decode_setc16 (const caps *, uint32_t word,
			   unsigned *config_reg, unsigned *value);

/* SFPCONFIG instruction word (reference encoder; the frozen pass
   materializes config through an LREG instead, mod1 = 0 path).  */
extern uint32_t encode_sfpconfig (const caps *, unsigned imm16,
				  unsigned dest, unsigned mod1);

/* The owned SETC16 program that establishes the Dst += DST_DELTA
   auto-increment address modifier from any incoming state.  Only
   dst_delta == +2 is proven; everything else refuses.  OUT must hold at
   least 6 entries.  *NEEDS_BANK_BASE_OWNERSHIP reports that the
   program's slot naming stands on the Base=1 SFPU platform contract
   (see the caps field comment; WH single slot 6 = scratch modifier 2
   under Base=1, SETC16 19/29/54 -- never the base-0 bank).  */
extern bool addr_mod_program (const caps *, int dst_delta,
			      setc16_program *out, unsigned *n_out,
			      bool *needs_bank_base_ownership);

/* Template word packer over the generic imm12/src/dest/mod1 layout.
   Validates field ranges and the routing-selector whitelist {0xC, 0xD};
   refuses opcode 0x8e (SFP_STOCH_RND) with a nonzero imm12 because its
   true field layout differs above bit 12.  */
extern bool encode_template (const caps *, const template_spec &,
			     uint32_t *word);
extern bool decode_template (uint32_t word, template_spec *out);

/* Sequence word for MACRO_INDEX realizing EVENTS: whole-word lookup over
   the proven programs (bit-level format unestablished; see header
   comment).  Event delays equal to DELAY_UNKNOWN in the table act as
   wildcards; documented delays must match.  */
extern bool encode_sequence (const caps *, unsigned macro_index,
			     const seq_event *events, unsigned n_events,
			     uint32_t *word);

/* Misc word for a proven store mode.  Only the select-shape rule
   (0x700 | store_mod0) is field-parameterized; the other proven misc
   words are exposed via the reference data.  */
extern bool encode_misc_select (const caps *, unsigned store_mod0,
				uint32_t *word);

/* ------------------------------------------------------------------ */
/* CC (lane-state) template facts (WH/BH-common).		      */
/* ------------------------------------------------------------------ */

/* Deferred-CC visibility lag: a CC result computed by a retiring macro
   event becomes architecturally visible to instructions issued in the
   cycle AFTER the event executed (the hardware's flag-forwarding
   latency; modeled identically by the CRAQ simulator's
   load_macro_cc_update_pending latch).  An event launched in slot S
   with programmed delay D executes in slot S + 1 + D; its CC write is
   visible to words issued in slots >= S + 1 + D + this lag.  */
extern unsigned cc_visibility_lag ();   /* 1 */

/* The scheduled store's LANE PREDICATE is the LIVE CC state at the
   store's EXECUTION cycle, never a launch-latched copy.  The ISA's
   launch-latched store overrides are exhaustive -- the Dst address,
   the Mod0 source, and the backdoor-load bit (SFPLOADMACRO.md,
   StoreSubUnit extra considerations) -- and do not include the lane
   predicate, which therefore keeps SFPSTORE's ordinary live
   lane-enable evaluation.  A CC write retiring in the SAME cycle as
   the store is architecturally not yet visible to it (the retirement
   group's pre-write snapshot); one retiring in any EARLIER cycle is.
   Proven on BH silicon (Where adjudication 2026-08-17, tt-quietbox-0
   p150, two resets, deterministic;
   ~/sfpi-uplift/where-adjudication-20260817/verdicts/VERDICT.md) and
   reproduced bit-exactly by the corrected CRAQ executor (craq-sim
   9f324140): the 4-slot select calendar, whose all-lanes restore
   retires in the Delay-2 store's own cycle, executes the store under
   the SFPSETCC complement mask and leaves the true-branch lanes
   unwritten; the compact 3-slot calendar, whose restore retires one
   cycle earlier, writes all lanes and is silicon-correct.  A
   CC-writing calendar must therefore retire its all-lanes restore
   STRICTLY BEFORE the scheduled store executes (macro_cc_model;
   refusal cc-restore-store-race).  */
extern bool store_lane_mask_live_at_execution ();   /* true */

/* Architecturally-defined complement of an SFPSETCC instr_mod1
   (WormholeB0/BlackholeA0 SFPSETCC.md; mirrored by the CRAQ executor):
   bit 0 selects the immediate operand, bit 1 selects the !=0 test over
   the sign test, bit 2 complements the register-test result, bit 3
   forces false.  Only the plain register-test class {0, 2, 4, 6} has a
   defined complement (toggle bit 2); immediate and force-false forms
   refuse.  */
extern bool sfpsetcc_complement_mod1 (uint64_t mod1, unsigned *out);

/* Hidden physical-LREG writes of a proven template word (L0..L15 index
   bitmask; 0 when none is on record).  */
extern uint32_t template_hidden_lreg_writes (const caps *, uint32_t word);

/* ------------------------------------------------------------------ */
/* Derived-calendar architectural facts (WH/BH-common).		      */
/*								      */
/* The sequence-word bit format and the per-event delay semantics are  */
/* ESTABLISHED by three independent sources (see			      */
/* docs/TIMING_CALENDAR_DERIVATION.md section 1-2): the ISA functional	      */
/* specification SFPLOADMACRO.md (SequenceBits, the per-sub-unit      */
/* opcode legality table, the Simple/Round VD16 rule, the SFPSWAP     */
/* adjacency rule, the Misc field layout), the CRAQ generic executor  */
/* (craq-sim f80a8d64 sfploadmacro_events.h: absolute ready =	      */
/* issue+1+delay, retire-before-issue, transactional same-cycle	      */
/* groups), and the handwritten MulInt32 init (tt_llk_blackhole	      */
/* ckernel_sfpu_mul_int.h, author-annotated fields).  This resolves   */
/* NOTES-wp6-prep.md section 9(b)/(c)/(g).  Every fact below carries that    */
/* provenance; whole-word proven programs above remain the capability */
/* for the frozen shapes (byte parity), while these facts let the     */
/* planner DERIVE calendars for new shapes.			      */
/* ------------------------------------------------------------------ */

/* Sequence byte cases (SequenceBits bits 2:0).  Case 1 is
   architecturally undefined and never encoded.  */
const unsigned SEQ_CASE_SKIP = 0;
const unsigned SEQ_CASE_NOP = 2;
const unsigned SEQ_CASE_STORE = 3;
const unsigned SEQ_CASE_TEMPLATE0 = 4;	/* 4 + template index (0..3)  */

/* Delay field (bits 5:3): the event executes at issue + 1 + delay.  */
const unsigned SEQ_MAX_DELAY = 7;

/* Pack one sequence byte: case | delay << 3 | VD16 << 6 | route << 7.
   Refuses case 1, out-of-range delay, and template indices > 3.  */
extern bool encode_sequence_bits (unsigned case_kind, unsigned delay,
				  bool vd16, bool route_vb, uint8_t *out);
/* Exact inverse (kept beside the encoder: one home for the layout);
   false for the architecturally undefined case 1.  */
extern bool decode_sequence_bits (uint8_t byte, unsigned *case_kind,
				  unsigned *delay, bool *vd16,
				  bool *route_vb);

/* Byte i of the word programs sub-unit i (Simple, MAD, Round, Store).  */
extern uint32_t compose_sequence_word (const uint8_t bytes[4]);
extern void decompose_sequence_word (uint32_t word, uint8_t bytes[4]);

/* Per-sub-unit opcode legality (the normative ISA table): bitmask with
   bit N = sub-unit N (0 Simple, 1 MAD, 2 Round, 3 Store) over the
   subunit-byte indices, NOT over rvtt_macro::subunit_t.  0 = no entry
   on record (the refusing default): an opcode scheduled on a sub-unit
   that cannot execute it silently becomes SFPNOP on hardware, so
   placement is a hard architectural fact.  */
const unsigned SEQ_UNIT_SIMPLE = 0;
const unsigned SEQ_UNIT_MAD = 1;
const unsigned SEQ_UNIT_ROUND = 2;
const unsigned SEQ_UNIT_STORE = 3;
extern unsigned subunit_legal_mask (const caps *, uint8_t opcode);

/* Result latency of a value event by executing sub-unit: the earliest
   a consumer may execute after its producer.  Simple/Round = 1 (every
   proven chain steps by one slot); MAD = 2 (the handwritten MulInt32
   store delays: exec+2 in both calendar variants).  */
extern unsigned subunit_result_latency (unsigned seq_unit);

/* Operand-routing class of a template opcode (ISA field-override
   rules; mirrored by the CRAQ build_dispatch).  */
enum route_class { RC_NONE, RC_VC, RC_VB_VC, RC_SHFT2 };
extern route_class opcode_route_class (const caps *, uint8_t opcode);

/* Does the opcode READ its VD operand (SFPSWAP)?  Such an event cannot
   target LReg16 and must keep route=1 so a planned VC survives.  */
extern bool opcode_reads_vd (const caps *, uint8_t opcode);
/* Whether OPCODE may be realized with the LReg16 staging target: the
   oracle-proven direct-evaluator set (lane IS F1; see the table's
   comment).  Everything else keeps VD-direct/staging-copy or refuses.  */
extern bool opcode_l16_target_proven (const caps *, uint8_t opcode);

/* The SFPSWAP scheduling rule (ISA (ddag)): MAD hosts nothing in the
   SWAP's execution cycle, and Simple and Round host nothing in the
   cycle after it.  */
extern bool opcode_needs_swap_adjacency (const caps *, uint8_t opcode);

/* The one PROVEN staging-copy realization (the frozen minmax
   transient copy): SFPSHFT2 immediate-0 on the Round sub-unit,
   route=1 (the SHFT_IMM VB<-VD override reads the launch VD), VD16.  */
struct staging_copy_facts
{
  uint8_t opcode;	/* SFPSHFT2			   */
  uint8_t mod1;		/* SHFT_IMM			   */
  unsigned seq_unit;	/* SEQ_UNIT_ROUND		   */
};
extern bool staging_copy_realization (const caps *, staging_copy_facts *);

/* Misc word field layout (ISA Misc struct; MulInt32 author comment):
   UnitDelayKind[3:0] << 8 | UsesLoadMod0ForStore[3:0] << 4
   | StoreMod0[3:0].  UnitDelayKind bit i = sub-unit i counts issued
   SFPU instructions instead of cycles (required exactly when an event
   consumes a value issued after its own launch).  */
extern uint32_t encode_misc_fields (unsigned store_mod0,
				    unsigned uses_load_mod0_mask,
				    unsigned delay_kind_mask);
extern void decode_misc_fields (uint32_t word, unsigned *store_mod0,
				unsigned *uses_load_mod0_mask,
				unsigned *delay_kind_mask);

/* Whether a DERIVED calendar may absorb the row's Dst stride into a
   launch's auto-increment address mode on this CPU.  BH is proven end
   to end (the derived unary max/min calendar is CRAQ bit-exact through
   the generic simulator path).  WH is now proven too: the laneR1
   position-shuffled-tiles failure that grounded the former WH refusal
   (laneR1-evidence-20260817 wh-onma trace: every latched launch
   dst_row/mask correct, data displaced from the second tile on) was
   adjudicated at sfpi-gcc 2a0ba1e6602 (laneAJ-evidence-20260817) as
   the DUAL-SLOT SETC16 program itself -- the emitted words clobbered
   LLK's live base-0 ADDR_MOD_2 and corrupted the NEXT tile's datacopy;
   the absorption machinery (launch auto-increment through the owned
   slot) was wrong, not unproven.  With the corrected single-slot
   Base=1 program (this table) the faithful WH sim (craq-sim wh
   8f0079a9) executes the derived absorbed-stride calendar bit-exact
   (laneAP WH CRAQ, derived unary max/min, multi-tile).  QSR has no
   capability entry and refuses.  Refusal name (unproven CPUs):
   derived-stride-absorption-unproven.  */
extern bool derived_stride_absorption_proven (const caps *);

/* ------------------------------------------------------------------ */
/* Fixed architectural words (WH/BH-common).			      */
/* ------------------------------------------------------------------ */

/* SFPENCC instruction-word encoding: TT_OP_{WH,BH}_SFPENCC (0x8a),
   imm12 << 12 | lreg_c << 8 | lreg_dest << 4 | mod1; the typed
   rvtt_sfpencc pattern carries no register operands, so both lreg
   fields encode as zero.  Returns false (refusing) for out-of-range
   fields.  This is the ONE derivation of SFPENCC words: the all-lanes
   constant below and every consumer-side lane-state proof
   (rvtt_insn_effects's cc_write_all_lanes) go through it, so the
   proof can never drift from the encoding.  */
extern bool sfpencc_encode (uint64_t imm12, uint64_t mod1, uint32_t *word);

/* SFPENCC all-lanes enable: imm12 = SFPENCC_IMM12_BOTH (3),
   mod1 = SFPENCC_MOD1_EI_RI (10) -> 0x8a00300a.  */
extern uint32_t sfpencc_all_lanes_word ();

/* One CR-mode Dst += 8 counter step: SETRWC (0, CR=4, D=8, B=0, A=0,
   mask=4) = 0x37120004.  A Dst face advance issues this word twice
   (typed rvtt_ttdstface insn since WP1).  */
extern uint32_t dst_step8_setrwc_word ();
extern unsigned dst_face_advance_step_count ();   /* 2 */

/* The absorbed explicit increment: TTINCRWC (CR=0, D=2, B=0, A=0)
   = 0x38008000.  */
extern uint32_t absorbed_dst_increment_word ();

/* SETRWC instruction-word field decode (WH/BH share the encoding):
   TT_OP_{WH,BH}_SETRWC (0x37): clear_ab_vld << 22 | rwc_cr << 18
   | rwc_d << 14 | rwc_b << 10 | rwc_a << 6 | bit_mask.  This is the
   ONE derivation of SETRWC fields; dst_step8_setrwc_word () above
   round-trips through it (unit-pinned in rvtt-macro-tables-test.cc),
   so field decode can never drift from the emitted encoding.
   Returns false (refusing) for any other opcode byte.  Field
   SEMANTICS (normative, concordant ISA spec + simulator executor
   TENSIX_EXECUTE_SETRWC + ckernel_instr_params.h p_setrwc):
     clear_ab_vld  two bits: clear SrcA/SrcB bank data-valid
     bit_mask      bit0 SrcA RWC write, bit1 SrcB RWC write,
		   bit2 Dst RWC write, bit3 fidelity-phase reset
     rwc_cr	   bit0/bit1 SrcA/SrcB CR mode, bit2 Dst CR-relative
		   (Dst = Dst_CR + rwc_d, CR follows), bit3 Dst
		   current-relative (Dst += rwc_d, CR follows)
     rwc_a/rwc_b/rwc_d  the 4-bit per-leg values.  */
struct setrwc_fields
{
  unsigned clear_ab_vld;
  unsigned rwc_cr;
  unsigned rwc_d;
  unsigned rwc_b;
  unsigned rwc_a;
  unsigned bit_mask;
};
extern bool setrwc_decode (uint32_t word, setrwc_fields *);

}  /* namespace rvtt_macro */

#endif /* GCC_RVTT_MACRO_TABLES_H */
