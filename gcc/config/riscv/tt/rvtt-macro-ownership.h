/* Shared path-sensitive ownership analysis for Tensix state (Layer 5).
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

#ifndef GCC_RVTT_MACRO_OWNERSHIP_H
#define GCC_RVTT_MACRO_OWNERSHIP_H

#include "rvtt-effects.h"

/* One shared ownership analysis with two fronts:

   - The RTL ownership lattice below (forward dataflow over
     xtt_effect_set) is the macro planner's proof vocabulary: config,
     address-modifier, LREG, CC, and Dst/RWC ownership per program point.
     Join is pointwise pessimism and opacity poisons everything -- there
     is no whitelist of any kind: opacity is a single unexceptioned bit.

   - The gimple region-ownership helpers implement the same discipline
     structurally for early passes: a region is a set of statements every
     path through which is opacity-free; opacity is GIMPLE_ASM or an
     unrepresented call, content never inspected; and any CFG mutation
     (edge split) is deferred until every proof has passed and a
     transform is committed, so refusals remain byte-identical.  */

/* ---------------- RTL ownership lattice (planner, WP4+) -------------- */

struct rvtt_ownership_state
{
  uint32_t config_dests_foreign;  /* dests possibly written by non-planner
				     code on some path		           */
  bool	   addr_mod_foreign;	  /* address-mod slot regs possibly
				     foreign-written			   */
  bool	   bank_base_known;	  /* WH ADDR_MOD_SET_Base ABI state proven
				     on this path			   */
  uint32_t lregs_foreign_live;	  /* foreign hard-LREG writes seen; the
				     consumer intersects with liveness	   */
  enum cc_state { CC_UNKNOWN, CC_ALL_LANES, CC_MASKED } cc;
  bool	   rwc_known;		  /* Dst/RWC state derivable from typed
				     effects on this path		   */
  bool	   opaque_reached;	  /* call/unclassified asm on some path	   */

  /* The pessimistic entry state: nothing owned, nothing known.  */
  static rvtt_ownership_state pessimum ();
  /* The optimistic reset state used at a proven ownership boundary.  */
  static rvtt_ownership_state owned_clean ();

  /* Pointwise-pessimistic path join.  */
  void join (const rvtt_ownership_state &other);
  /* Forward transfer of one instruction's typed effect set.  Opaque
     effect sets poison every dimension, unexceptioned.  */
  void transfer (const xtt_effect_set &effects);
};

/* ---------------- Gimple region ownership (invariant pass) ----------- */

class loop;

/* Structural opacity: inline assembly or an unrepresented call.  The
   content of an assembly statement is never inspected.  */
extern bool rvtt_gimple_opaque_stmt_p (gimple *stmt);

/* The unique non-abnormal loop entry edge, or null.  The loop is
   single-entry through this edge; hoisted state is placed on it (at the
   end of its source block when that block is a dedicated preheader,
   otherwise in a fresh block split from the edge at commit time).  */
extern edge rvtt_loop_entry_edge (class loop *loop);

/* Whether ENTRY's source block is a dedicated preheader (single
   successor, not the function entry block).  */
extern bool rvtt_dedicated_preheader_p (edge entry);

/* Region-scoped ownership proof for a loop hoist.  The region that must
   be free of opaque statements is {dedicated preheader at/after the
   hoist insertion point} union {loop body blocks}: values hoisted onto
   the entry edge and consumed only inside the loop are observable only
   within that region, and re-entering the loop re-executes the hoist.  */
extern bool rvtt_loop_hoist_region_opaque_p (class loop *loop, edge entry);

/* A dedicated preheader ending in a block-terminating statement cannot
   receive an end-of-block insertion after that terminator; the hoist
   must refuse structurally.  (An opaque terminator additionally makes
   the region opaque above.)  */
extern bool rvtt_preheader_insertion_blocked_p (edge entry);

/* Commit-time insertion block for ENTRY: the dedicated preheader itself,
   or a fresh block split from the edge.  Callers must invoke this only
   after every proof has passed and at least one statement will move --
   refusal paths never mutate the CFG, keeping refusals byte-identical
   to the flag-off compilation.  */
extern basic_block rvtt_commit_hoist_preheader (edge entry);

/* ---- Loop invariant-materialization proofs (gimple-rvtt-invariant.cc) --

   Shared discipline for placing loop-invariant SFPU immediate
   materializations in a loop preheader.  The invariant-loadi pass and
   the LUT instruction selection's coefficient placement consume the
   same proofs so preheader placement carries one refusal discipline
   everywhere.  */

/* No statement in LOOP's body changes the lane-enable CC state or owns
   a volatile target effect other than the typed Dst load/store/counter
   operations.  A hoisted lane-predicated materialization therefore
   executes under the same CC state in the preheader as at its original
   position inside the loop.  */
extern bool rvtt_loop_has_sfpu_barrier_p (class loop *loop);

/* CC-canonical single-block loop body (the shape pass_rvtt_cc lowers a
   structured in-loop v_if region to: candidate materializations, then
   CC writers, with an all-lanes SFPENCC as the LAST CC writer on the
   single linear path).  In such a body the lane-enable mask at every
   statement before FIRST_CC_WRITER equals, on every iteration after the
   first, the architectural all-lanes state the trailing SFPENCC
   re-establishes (capability word rvtt_macro::sfpencc_all_lanes_word;
   craq-sim TENSIX_EXECUTE_SFPENCC).  The first iteration's mask is the
   unknown ambient state -- consumers must reproduce iteration one
   exactly (the const-residency first-iteration peel) rather than reason
   about it.  PROVEN is false for multi-block bodies, bodies with no CC
   writer (the plain-barrier classes handle those), a non-SFPENCC or
   non-all-lanes final CC writer, any opaque statement, memory-touching
   scalar code, or any volatile target effect outside the typed Dst
   load/store/counter class.  A proven body contains only typed RVTT
   calls, pure scalar/vector assignments, PHIs, labels, debug
   statements, and the loop condition -- the statement classes a
   first-iteration peel can duplicate exactly.  */
struct rvtt_cc_canonical_body
{
  bool proven;
  gimple *first_cc_writer;
  const char *why;		/* refusal detail for dumps (never a
				   decision input) */
};
extern rvtt_cc_canonical_body rvtt_loop_cc_canonical_body (class loop *loop);

/* CALL is an SFPU immediate materialization (sfpxloadi of all-constant
   operands through a canonical instruction-buffer operand) whose value
   is consumed only inside LOOP.  ALLOW_SHORTENED additionally admits
   the single-issue sfploadi form that pass_rvtt_immload_shorten
   produces, for consumers running after it; the early invariant pass
   must not set it.  */
extern bool rvtt_invariant_constant_load_p (gcall *call, class loop *loop,
					    bool allow_shortened = false);

/* Keeping every load in LOADS live across LOOP holds the loop's peak
   vector pressure within the architectural eight-LREG file
   (conservative liveness proof; refusal is all-or-nothing for the
   given candidate set).  */
extern bool rvtt_loop_lreg_pressure_legal_p (class loop *loop,
					     const auto_vec<gcall *> &loads,
					     bool report = true,
					     bool cc_transients = false,
					     bool exempt_creg_reads = false);

/* LOOP's first header test provably enters the loop body through
   ENTRY, so an architectural LREG write is never speculated out of a
   possibly-zero-trip loop.  */
extern bool rvtt_loop_first_iteration_executes_p (class loop *loop,
						  edge entry);

/* BB provably executes on every iteration that enters LOOP's body
   (pure CFG dominance structure; no statement content examined).  */
extern bool rvtt_stmt_executes_every_entered_iteration_p (class loop *loop,
							  basic_block bb);

/* Modeled SFPLOADI issue count to materialize CALL's constant after
   the immediate-shortening passes run (target immediate encodings
   only; never recognizes particular values or source patterns).  */
extern unsigned rvtt_sfpxloadi_materialization_cost (gcall *call);

/* Audited hoist-region scan (implemented with the cross-call hoist's
   audited word classification and TU MOP template census,
   gimple-rvtt-crosscall.cc): every statement of {LOOP body} union
   {preheader tail at/after the ENTRY insertion point} is proven unable
   to write an LREG in LREG_MASK, unable to change the SFPU CC/lane
   state, and unable to deliver an unaudited or replay word.  On
   refusal returns false with the dump-stable name in *WHY (and the
   offending statement in *WHY_STMT when known).  Refusing default for
   every class not on record.
   CC_IMMATERIAL selects the programming-only discipline (lane HR,
   -mtt-tensix-optimize-crossloop-cc-peel): a CC-writing statement is
   admitted exactly when it is a structured typed CC atom (whitelisted
   by insn id; its whole architectural effect is the lane-enable state
   plus its SSA definition) -- sound only for a consumer whose lifted
   object executes BEFORE the region and whose parked state (a claimed
   programmable constant register) no CC write can touch.  Every other
   discipline (delivered words, replay, MOP census, explicit LREG
   writes, side-effecting calls) is unchanged; a CC writer off the
   whitelist refuses by name (crossloop-cc-atom-unproven).  */
extern bool rvtt_crossloop_region_scan (class loop *loop, edge entry,
					unsigned lreg_mask,
					const char **why,
					gimple **why_stmt,
					bool cc_immaterial = false);

/* The outermost enclosing entry edge to which a loop-entry placement
   of LOOP may be lifted under the audited-region discipline: walks
   outward from ENTRY while the enclosing loop's region scan, preheader
   insertion, and first-entry execution proofs all hold; returns ENTRY
   unchanged when nothing is proven.  ANCHOR names the block whose
   execution the original placement is tied to (the inner loop's
   header).  */
extern edge rvtt_crossloop_outermost_entry (class loop *loop, edge entry,
					    unsigned lreg_mask,
					    bool cc_immaterial = false);

/* BB provably executes on the first iteration of LOOP entered through
   ENTRY: the header exit test folds (or is implied by a dominating
   guard on the same SSA operands) toward the body, and BB is reached
   from the taken edge by a single-successor chain.  Together with
   rvtt_stmt_executes_every_entered_iteration_p's exit discipline this
   discharges the no-speculation obligation for do-while and
   guard-protected loop shapes the constant fold cannot see.  */
extern bool rvtt_crossloop_block_executes_on_entry_p (class loop *loop,
						      edge entry,
						      basic_block bb);

#endif /* GCC_RVTT_MACRO_OWNERSHIP_H */
