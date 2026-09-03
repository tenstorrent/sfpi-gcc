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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "tree-cfg.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "rtl.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-raw-boundary.h"

/* ---------------- RTL ownership lattice (planner) ------------------- */

rvtt_ownership_state
rvtt_ownership_state::pessimum ()
{
  rvtt_ownership_state s;
  s.config_dests_foreign = ~0u;
  s.addr_mod_foreign = true;
  s.bank_base_known = false;
  s.lregs_foreign_live = ~0u;
  s.cc = CC_UNKNOWN;
  s.rwc_known = false;
  s.opaque_reached = true;
  return s;
}

/* The optimistic reset state used at a proven ownership boundary:
   nothing foreign, counters derivable -- but the CC lane state and the
   WH bank-base ABI state stay unknown until separately proven.  */

rvtt_ownership_state
rvtt_ownership_state::owned_clean ()
{
  rvtt_ownership_state s;
  s.config_dests_foreign = 0;
  s.addr_mod_foreign = false;
  s.bank_base_known = false;	/* Knowledge is proven, never assumed.  */
  s.lregs_foreign_live = 0;
  s.cc = CC_UNKNOWN;
  s.rwc_known = true;
  s.opaque_reached = false;
  return s;
}

/* Pointwise-pessimistic join of OTHER's path into this state: foreign
   sets union, knowledge bits intersect, and disagreeing CC states
   become CC_UNKNOWN.  */

void
rvtt_ownership_state::join (const rvtt_ownership_state &other)
{
  config_dests_foreign |= other.config_dests_foreign;
  addr_mod_foreign |= other.addr_mod_foreign;
  bank_base_known &= other.bank_base_known;
  lregs_foreign_live |= other.lregs_foreign_live;
  if (cc != other.cc)
    cc = CC_UNKNOWN;
  rwc_known &= other.rwc_known;
  opaque_reached |= other.opaque_reached;
}

/* Forward transfer of one instruction's typed EFFECTS across this
   state.  Only ever pessimistic: foreign config/address-modifier/LREG
   writes accumulate, a CC write drops lane-state knowledge, an unknown
   counter effect drops RWC derivability, and an opaque effect set
   poisons every dimension.  */

void
rvtt_ownership_state::transfer (const xtt_effect_set &effects)
{
  if (effects.opaque)
    {
      /* Opacity poisons everything -- a single unexceptioned bit; there
	 is no whitelist of any kind.  */
      *this = pessimum ();
      return;
    }

  config_dests_foreign |= effects.config_dests_written;
  addr_mod_foreign |= effects.addr_mod_slot_write;

  /* Foreign LREG writes accumulate; the consumer intersects this with
     its own liveness (lreg-livein style) to obtain the live set.  */
  lregs_foreign_live |= effects.lreg_write;

  /* A typed CC write invalidates lane-state knowledge until a consumer
     proves the specific all-lanes pattern.  The consumer-side proof is
     cc_write_all_lanes (rvtt_insn_effects), word-exact against the
     capability table's architectural SFPENCC encoding; region
     discovery and macro-planner formation both consume it, refusing
     cc-enable-unproved otherwise.  This transfer is only ever
     pessimistic.  */
  if (effects.cc_write)
    cc = CC_UNKNOWN;

  switch (effects.rwc.kind)
    {
    case xtt_rwc_effect_t::NONE:
    case xtt_rwc_effect_t::INC:
    case xtt_rwc_effect_t::SET:
    case xtt_rwc_effect_t::FACE:
      /* Typed counter effects preserve derivability of the RWC state.  */
      break;
    case xtt_rwc_effect_t::UNKNOWN:
      rwc_known = false;
      break;
    }
}

/* ---------------- Gimple region ownership (invariant pass) ----------- */

/* Opaque assembly or an unrepresented call can own architectural Tensix
   state without creating a vector SSA value.  Known RVTT calls expose
   their vector values and effects to the typed analyses.  The one
   audited assembly exception is the raw `.ttinsn' constant word whose
   architectural field decode proves the pure Dst/RWC counter class
   (rvtt-raw-boundary.cc): such a word can own no LREG, CC, or
   configuration state -- the same standing the typed TTINCRWC /
   TTSETRWC / face-advance builtins already have here.  Every other
   assembly statement stays opaque.  */

bool
rvtt_gimple_opaque_stmt_p (gimple *stmt)
{
  if (gimple_code (stmt) == GIMPLE_ASM)
    return !rvtt_raw_pure_dst_rwc_gimple (stmt);
  return is_gimple_call (stmt) && !rvtt_get_insn_data (stmt);
}

/* Return LOOP's unique non-abnormal entry edge (the one header
   predecessor from outside the loop), or null when the header has
   several external predecessors or the only entry is abnormal.  */

edge
rvtt_loop_entry_edge (class loop *loop)
{
  edge entry = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, loop->header->preds)
    if (!flow_bb_inside_loop_p (loop, e->src))
      {
	if (entry)
	  return nullptr;
	entry = e;
      }

  return entry && !(entry->flags & EDGE_ABNORMAL) ? entry : nullptr;
}

/* Return whether ENTRY's source block is a dedicated preheader: a
   single-successor block other than the function entry block, so
   hoisted statements can be inserted at its end without executing on
   any other path.  */

bool
rvtt_dedicated_preheader_p (edge entry)
{
  return single_succ_p (entry->src)
    && entry->src != ENTRY_BLOCK_PTR_FOR_FN (cfun);
}

/* Region-scoped ownership proof.  The region that must be free of opaque
   state is {the dedicated preheader at/after the hoist insertion point}
   union {the loop body blocks}, and nothing more:

   - Hoisted values are consumed only inside the loop (the consumer
     proves this), so the hoisted architectural writes are observable
     only between the hoist point and the loop's uses.
   - The loop is single-entry through its unique entry edge, so every
     path from the hoist point to any use lies entirely within the
     region.  Opaque code before the hoist point or after loop exit
     cannot interleave with the hoisted live ranges: re-entering the
     loop re-executes the entry edge and therefore re-hoists.
   - Within a dedicated preheader, statements before the insertion point
     execute before the hoisted statements and are equally harmless.
     With end-of-block insertion the only statement that can execute
     after them is a block-terminating statement (insertion goes before
     it); when that terminator is itself opaque the region is dirty and
     the consumer must refuse.  A block split from a shared entry edge
     is empty and trivially clean.

   This is CFG/dominance structure only: opacity is a single
   unexceptioned bit, and no assembly content or raw encoding is ever
   examined.  */

bool
rvtt_loop_hoist_region_opaque_p (class loop *loop, edge entry)
{
  bool opaque = false;
  basic_block *body = get_loop_body (loop);
  for (unsigned ix = 0; ix != loop->num_nodes && !opaque; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi) && !opaque; gsi_next (&gsi))
      if (rvtt_gimple_opaque_stmt_p (gsi_stmt (gsi)))
	opaque = true;
  free (body);
  if (opaque)
    return true;

  /* Preheader tail at/after the hoist insertion point.  */
  if (single_succ_p (entry->src))
    {
      gimple_stmt_iterator last = gsi_last_nondebug_bb (entry->src);
      if (!gsi_end_p (last) && stmt_ends_bb_p (gsi_stmt (last))
	  && rvtt_gimple_opaque_stmt_p (gsi_stmt (last)))
	return true;
    }
  return false;
}

/* Return whether ENTRY's dedicated preheader ends in a
   block-terminating statement, which an end-of-block insertion cannot
   be placed after -- the hoist must then refuse structurally.  False
   when the preheader is not dedicated (a fresh split block would be
   used instead).  */

bool
rvtt_preheader_insertion_blocked_p (edge entry)
{
  if (!rvtt_dedicated_preheader_p (entry))
    return false;
  gimple_stmt_iterator last = gsi_last_nondebug_bb (entry->src);
  return !gsi_end_p (last) && stmt_ends_bb_p (gsi_stmt (last));
}

/* Commit: callers invoke this only after every proof holds and at least
   one statement will move.  Splitting the shared entry edge now (and
   only now) keeps every refusal byte-identical to the flag-off
   compilation.  split_edge keeps loop membership and dominance info
   consistent and moves the header PHI arguments onto the new edge.  */

basic_block
rvtt_commit_hoist_preheader (edge entry)
{
  return rvtt_dedicated_preheader_p (entry) ? entry->src
    : split_edge (entry);
}
