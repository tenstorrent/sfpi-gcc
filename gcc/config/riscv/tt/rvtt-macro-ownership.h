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

#endif /* GCC_RVTT_MACRO_OWNERSHIP_H */
