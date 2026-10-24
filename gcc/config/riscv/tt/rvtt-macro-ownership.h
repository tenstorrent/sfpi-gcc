/* Shared path-sensitive ownership analysis for Tensix state.
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

/* Ownership analysis of Tensix state, in two forms:

   - An RTL lattice (forward dataflow over xtt_effect_set) used by the
     macro planner: which config, address-modifier, LREG, CC and Dst/RWC
     state the compiler owns at each program point.  The join is
     pointwise pessimistic and an opaque instruction clobbers everything.

   - Gimple region helpers for early passes: a region is owned when no
     path through it contains an opaque statement (GIMPLE_ASM or an
     unrepresented call; asm contents are never inspected).  CFG changes
     such as edge splits are deferred until a transform is committed, so
     a refused transform leaves the function unchanged.  */

/* ---------------- RTL ownership lattice (planner) ------------------- */

struct rvtt_ownership_state
{
  uint32_t config_dests_foreign;  /* dests possibly written by non-planner
				     code on some path.  */
  bool	   addr_mod_foreign;	  /* address-mod slot regs possibly
				     written by non-planner code.  */
  bool	   bank_base_known;	  /* WH ADDR_MOD_SET_Base ABI state known
				     on this path.  */
  uint32_t lregs_foreign_live;	  /* hard LREGs written by non-planner
				     code; the consumer intersects this
				     with liveness.  */
  enum cc_state { CC_UNKNOWN, CC_ALL_LANES, CC_MASKED } cc;
  bool	   rwc_known;		  /* Dst/RWC state derivable from typed
				     effects on this path.  */
  bool	   opaque_reached;	  /* call or unclassified asm on some
				     path.  */

  /* The pessimistic entry state: nothing owned, nothing known.  */
  static rvtt_ownership_state pessimum ();
  /* The reset state used at a known ownership boundary.  */
  static rvtt_ownership_state owned_clean ();

  /* Pointwise-pessimistic path join.  */
  void join (const rvtt_ownership_state &other);
  /* Apply one instruction's effect set.  An opaque effect set makes every
     component unknown.  */
  void transfer (const xtt_effect_set &effects);
};

/* ---------------- Gimple region ownership (invariant pass) ----------- */

class loop;

/* Return true if STMT is inline assembly or an unrepresented call.  The
   content of an assembly statement is never inspected.  */
extern bool rvtt_gimple_opaque_stmt_p (gimple *stmt);

/* Return the unique non-abnormal entry edge of LOOP, or null.  Hoisted
   state is placed on this edge: at the end of its source block when that
   block is a dedicated preheader, otherwise in a block split from the
   edge at commit time.  */
extern edge rvtt_loop_entry_edge (class loop *loop);

/* Return true if ENTRY's source block is a dedicated preheader (single
   successor, not the function entry block).  */
extern bool rvtt_dedicated_preheader_p (edge entry);

/* Return true if the region of a hoist onto ENTRY contains an opaque
   statement.  The region is the preheader from the insertion point on
   plus the body of LOOP: a value hoisted onto the entry edge and used
   only inside the loop is observable only there, and re-entering the
   loop re-executes the hoist.  */
extern bool rvtt_loop_hoist_region_opaque_p (class loop *loop, edge entry);

/* Return true if ENTRY's dedicated preheader ends in a control statement,
   so nothing can be inserted at its end.  */
extern bool rvtt_preheader_insertion_blocked_p (edge entry);

/* Return the block to insert hoisted statements for ENTRY into: the
   dedicated preheader, or a new block split from the edge.  Call this
   only once the hoist is certain to happen, so that a refused hoist
   leaves the CFG unchanged.  */
extern basic_block rvtt_commit_hoist_preheader (edge entry);

/* ---- Loop invariant-materialization checks ----

   Shared checks used by every pass that places loop-invariant SFPU
   immediate loads in a loop preheader.  */

/* Return true if some statement in LOOP's body changes the lane-enable
   CC state or has a volatile target effect other than the typed Dst
   load/store/counter operations.  When false, a hoisted lane-predicated
   load executes under the same CC state in the preheader as inside the
   loop.  */
extern bool rvtt_loop_has_sfpu_barrier_p (class loop *loop);

/* Describe whether LOOP has a CC-canonical single-block body, the shape
   pass_rvtt_cc lowers an in-loop v_if region to: candidate loads, then CC
   writers, the last of which is an all-lanes SFPENCC.  In such a body,
   on every iteration after the first, all lanes are enabled at every
   statement before FIRST_CC_WRITER.  The first iteration runs under the
   unknown incoming CC state, so consumers must peel it rather than
   reason about it.  PROVEN is false for multi-block bodies, bodies with
   no CC writer, a final CC writer that is not an all-lanes SFPENCC, and
   bodies with opaque statements, memory-accessing scalar code or volatile
   target effects other than typed Dst load/store/counter operations.  A
   proven body therefore contains only statements a peel can duplicate
   exactly: typed RVTT calls, pure assignments, PHIs, labels, debug
   statements and the loop condition.  */
struct rvtt_cc_canonical_body
{
  bool proven;
  gimple *first_cc_writer;
  const char *why;		/* reason for refusal, for dumps only.  */
};
extern rvtt_cc_canonical_body rvtt_loop_cc_canonical_body (class loop *loop);

/* Return true if STMT is an SFPENCC whose encoded word equals
   rvtt_macro::sfpencc_all_lanes_word AND the independent ROW_MASK proof
   establishes that every lane is enabled after it.  For the encoding-only
   predicate use rvtt_encc_all_lanes_call_p; it proves column flags only.  */
extern bool rvtt_all_lanes_encc_p (gimple *stmt);

/* Return true if CALL is an SFPU immediate load (sfpxloadi of constant
   operands through a canonical instruction-buffer operand) whose value
   is used only inside LOOP.  ALLOW_SHORTENED also accepts the
   single-issue sfploadi form produced by pass_rvtt_immload_shorten, for
   passes that run after it.  */
extern bool rvtt_invariant_constant_load_p (gcall *call, class loop *loop,
					    bool allow_shortened = false);

/* Return true if LOOP's first header test, reached through ENTRY,
   provably enters the body, so hoisting an LREG write out of LOOP does
   not speculate it out of a zero-trip loop.  */
extern bool rvtt_loop_first_iteration_executes_p (class loop *loop,
						  edge entry);

/* Return true if BB executes on every iteration that enters LOOP's body,
   judged from the CFG alone.  */
extern bool rvtt_stmt_executes_every_entered_iteration_p (class loop *loop,
							  basic_block bb);

/* Return the number of SFPLOADI instructions needed to load CALL's
   constant once the immediate-shortening passes have run.  */
extern unsigned rvtt_sfpxloadi_materialization_cost (gcall *call);

#endif /* GCC_RVTT_MACRO_OWNERSHIP_H */
