/* Typed architectural effect sets for Tensix instructions (Layer 1).
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

#ifndef GCC_RVTT_EFFECTS_H
#define GCC_RVTT_EFFECTS_H

/* This is the ONLY vocabulary macro-planner layers may use to classify
   instructions.  Values are derived from the generated effect attribute
   family in rvtt-cost.md (all of whose defaults are refusing), resolved
   against instruction operands after the attribute class has admitted the
   instruction.  recog-code comparisons are permitted solely to reach an
   admitted instruction's operands -- never as region identity.  */

enum xtt_subunit_t { XTT_SU_NONE, XTT_SU_SIMPLE, XTT_SU_MAD, XTT_SU_ROUND,
		     XTT_SU_LOAD, XTT_SU_STORE, XTT_SU_CFG, XTT_SU_SYNC };

struct xtt_rwc_effect_t {
  enum kind_t { NONE, INC, SET, FACE, UNKNOWN } kind;
  int dst_delta;		/* meaningful for INC; FACE == one face step */
  int cr_delta;			/* counter-register semantics, typed operands */
  unsigned set_mask;		/* for SET (TTSETRWC) */
};

struct xtt_effect_set {
  uint32_t lreg_read, lreg_write; /* hard-reg mask over L0..L15/LREG16 */
  bool	   cc_read, cc_write;
  bool	   cc_write_all_lanes;	  /* cc_write provably writes the all-lanes
				     enabled state (word-exact against the
				     capability table's architectural
				     all-lanes SFPENCC encoding); refusing
				     default false -- any other CC write is
				     lane-state-unproved */
  uint32_t config_dests_written;  /* bitmask over SFPCONFIG dests 0..15 */
  uint32_t config_dests_read;	  /* bitmask over SFPCONFIG dests 0..15 */
  bool	   addr_mod_slot_write;	  /* SETC16 into an address-mod slot reg */
  xtt_rwc_effect_t rwc;
  bool	   dst_mem_read, dst_mem_write;	  /* from MEM operands (existing) */
  int	   result_latency;	  /* from xtt_result_latency */
  bool	   next_slot_stall;	  /* architectural next-slot acceptance
				     stall (xtt_next_slot_stall) */
  xtt_subunit_t subunit;
  bool	   opaque;		  /* CALL_P, unclassified asm, unknown */
};

/* Attribute-driven; opaque=true default.  */
extern xtt_effect_set rvtt_insn_effects (rtx_insn *);

/* The target's architectural no-increment load/store address mode
   (capability data; -1 = unproven, refuse).  */
extern int rvtt_no_increment_address_mode ();

/* Gimple-level access for builtin calls: the subunit of the late RTL
   pattern a builtin resolves to.  Returns XTT_SU_NONE (the refusing
   default) for unaudited builtins.  */
struct rvtt_insn_data;
extern xtt_subunit_t rvtt_builtin_subunit (const rvtt_insn_data *);

/* Gimple-level access to the audited result latency of the late RTL
   pattern a builtin resolves to (the same scratch-code attribute seam
   as rvtt_builtin_subunit): the encoded `xtt_result_latency' minus the
   +1 bias, so 0 = same-slot chaining and -1 = UNAUDITED (the refusing
   default, also returned for unlisted builtins).  Only builtins whose
   late pattern carries a constant (operand-free) latency attribute are
   listed in the map; the timing semantics consuming this value live in
   rvtt-timing.h (the timing-engine discipline: facts read once at the
   consumer
   seam, math in the engine).  */
extern int rvtt_builtin_result_latency (const rvtt_insn_data *);

/* Annotate FILE with INSN's effect set (under -mtt-tensix-dump-effects).  */
extern void rvtt_dump_insn_effects (FILE *, rtx_insn *);

/* Audited lane-local value-op family (the
   effect_overrides tables formerly copied verbatim between
   rtl-rvtt-lp-alloc.cc and rtl-rvtt-dst-ownership.cc, migrated to the
   xtt_lane_local/xtt_cc_write attributes at the definitions in
   rvtt.md).  Returns true iff INSN's pattern is an audited SFPU LREG
   value operation with no Dst-memory, RWC-counter, or configuration
   effect (keyed per insn code, deliberately mod-independent -- a
   verbatim re-homing of the audited tables); *CC_WRITES then reports
   whether the operation's mod field can architecturally set CC,
   recorded conservatively regardless of the mod value.  Refusing
   default: unannotated patterns return false.  Deliberately separate
   from rvtt_insn_effects, whose opacity surface (and with it the
   frozen macro-planner refusal surface) is untouched by the migration
   (planner-oracle re-freeze: oracles/refreeze-pin49-20260831.txt).  */
extern bool rvtt_lane_local_effects (rtx_insn *, bool *cc_writes);

/* Lane-gated consumer family (rtl-rvtt-lp-alloc.cc's
   hand lane_gated_consumers allowlist migrated to the xtt_lane_gated
   attribute): true iff INSN's LREG/Dst writes are lane-gated and its
   dataflow is lane-local.  The CC-gated predicated-assign copy is NOT
   covered here (its starred pattern is recognized structurally by the
   consumer, exactly as before).  Refusing default false.  */
extern bool rvtt_lane_gated_consumer_p (rtx_insn *);

/* Post-admission operand access for a Dst load/store (EFFECTS must have
   dst_mem_read or dst_mem_write): the typed address, data-mode, and
   address-mode operands.  Returns false for insns whose Dst operand
   layout is not on record.  */
extern bool rvtt_dst_access_operands (rtx_insn *, const xtt_effect_set &,
				      rtx *address, rtx *mode,
				      rtx *addr_mode);

/* ---- Multi-result / shadow-coupled effect structure (TOP3-2 layer 1/2).

   A multi-result instruction is one whose single architectural event
   defines both a value-bank result set (L0-L3) and a companion result
   set (L4-L7) in one PARALLEL: the indexed SFPSWAP (value pair plus
   companion pair at value+4, per SFPSWAP.md's ENABLE_DEST_INDEX leg) and
   the eight-definition SFPTRANSP (both Transpose4 banks).  The masks are
   hard-LREG masks in the same domain as xtt_effect_set::lreg_write.
   Post-admission operand access: EFFECTS must be non-opaque.  Returns
   false for instructions with no companion structure on record.  */

struct xtt_multiresult_group {
  uint32_t value_write_mask;
  uint32_t companion_write_mask;
};

extern bool rvtt_multiresult_group (rtx_insn *, const xtt_effect_set &,
				    xtt_multiresult_group *);

/* Zero-length architectural LREG interface marker (the typed
   variable-LREG read/write patterns): sets *LREG_MASK to the pinned hard
   LREG.  These markers deliver no instruction word but carry the
   dataflow connection of fixed-LREG protocols; a capture boundary must
   not separate them from the multi-result instruction they describe.  */
extern bool rvtt_lreg_marker (rtx_insn *, uint32_t *lreg_mask);

/* Recording-epoch closure proof (ownership-epoch model for the replay
   buffer's recording state).  A user fixed capture of PAYLOAD_WORDS
   words opens an epoch; the epoch closes when the typed instruction
   lengths of the following slot-occupying Tensix instructions account
   for exactly the declared words.  Anything the typed stream cannot
   account for refuses, by name:

     OPAQUE_PAYLOAD        - opaque asm, a call, an unrecognized insn, or
			     a declared length that would split one
			     instruction's words (blocker = the insn);
     CROSSES_BLOCK         - the block ends while the epoch is open;
     OWNER_DURING_CAPTURE  - an explicit replay-owner instruction inside
			     the open epoch (broken user code).

   On CLOSED, close_at is the last payload instruction extended across
   any immediately following zero-length LREG interface markers (the
   companion-group protocol above), and multiresult_members counts the
   payload's multi-result instructions -- the capture retains them.  */

struct xtt_replay_epoch {
  enum status_t { CLOSED, OPAQUE_PAYLOAD, CROSSES_BLOCK,
		  OWNER_DURING_CAPTURE } status;
  rtx_insn *close_at;
  rtx_insn *blocker;
  unsigned multiresult_members;
};

extern xtt_replay_epoch rvtt_replay_epoch_close (rtx_insn *capture,
						 unsigned payload_words);

/* Whether index-tracking shadow coupling (LaneConfig.ENABLE_DEST_INDEX,
   written through SFPCONFIG destination 15) is possibly enabled anywhere
   in FN: true when the function contains a typed multi-result
   instruction (its semantics are the coupling) or any typed
   configuration write that can reach destination 15.  The programming
   model's default state is disabled; a raw-TTI enable is invisible to
   the typed stream and remains a documented layer-2 gap (a later
   increment types those words).  Lossy by design: there is no proven-off
   transition, so one enable is function-sticky.  */
extern bool rvtt_shadow_coupling_possible (function *);

/* ---- Planner emission records: launch issue-plane effects.

   A macro launch's architectural effects are descriptor-dependent, so
   rvtt_insn_effects keeps every SFPLOADMACRO pattern effect-opaque (the
   attribute family cannot express them).  When the MACRO PLANNER ITSELF
   emits the launch it has just synthesized the descriptor the launch
   executes, so it can derive the launch's effect interface from its own
   construction -- a planner emission record, the same discipline as the
   residency-benign emitted set.  These records are the pricing-side
   consumer interface for exec_interlocked_slots (rtl-rvtt-replay.cc);
   they never alter rvtt_insn_effects itself, so every other consumer of
   the vocabulary keeps the refusing opaque default byte-identically.

   Record contract (all facts derived at emission from the synthesized
   descriptor, never from op names or word fingerprints):

     - lreg_read = 0: SFPLOADMACRO issue is never operand-gated.  [ISA]
       SFPLOADMACRO.md schedules every sub-unit event for an absolute
       later cycle (issue + 1 + delay) and states no issue-cycle
       register rule; [SIM] the reference simulator's event model
       enqueues events unconditionally at issue
       -- "there is no FIFO between launches"; [HAND] the production
       Where kernel and handwritten typecast issue launches
       back-to-back.  Every register a launch's events read is either
       the launch's own VD (produced by its load), a planner-planned
       resident constant, or an explicit calendar member whose
       availability the schedule's delay derivation proved -- event
       operand readiness is discharged by the calendar's construction,
       not by issue stalls.
     - lreg_write = launch VD | the descriptor's hidden template writes:
       the complete architectural LREG write set of the launch.
     - result_latency = the launch's settle distance: the greatest
       event completion past the launch's own done slot, from the
       descriptor's own SequenceBits delays plus the audited
       subunit_result_latency facts (the drain derivation's math), so a
       foreign consumer of a launch-written register waits for the
       macro to settle.
     - Writes are FULL-LANE by construction: records exist only for
       non-CC calendars, whose formation carries the all-lanes ambient
       proof, so a later full overwrite of a launch-written register is
       not a dependence under the scheduler's definition (only reads
       and lane-predicated writes are).  The pricing consumer may
       therefore drop the write-side dependence edge for record-carried
       insns.

   Refusing defaults everywhere: derivation fails closed (no record) for
   CC-writing calendars and any undecodable sequence byte; lookup
   fails closed unless the insn is a
   recognized SFPLOADMACRO pattern whose INSN_UID, containing function,
   encoded launch word, and VD operand all match the record written at
   emission.  USER-written launches (raw `.ttinsn' words) never acquire
   records: they are asm, refused upstream of any record lookup.  */

extern void rvtt_planner_launch_effects_reset ();
extern void rvtt_planner_launch_effects_record (rtx_insn *, uint64_t word,
						unsigned vd_regno,
						const xtt_effect_set &);
extern bool rvtt_planner_launch_effects (rtx_insn *, xtt_effect_set *);

#endif /* GCC_RVTT_EFFECTS_H */
