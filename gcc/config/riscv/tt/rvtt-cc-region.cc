/* Tensix CC-region tree (pushc/popc frame structure).
   Copyright (C) 2022-2026 Tenstorrent Inc.

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

/* See rvtt-cc-region.h for the interface.  The build has three phases:

   A. a linear walk of each basic block producing a summary (pops below
      the entry frame, net pushes, CC-event counts);

   B. a forward dataflow over frame stacks, iterated in reverse post-order
      to a fixpoint, creating one region per static sfppushc under its
      parent and recognizing the counted-pop diamond.  Any other mismatch
      at a join, a nonzero pushc/popc operand, a pop of the ambient frame
      or depth beyond 8 marks the open frames unstructured and leaves
      everything reachable from that point unmapped;

   C. a walk of each structured block assigning every statement its frame
      and recording exits, refinements and poison facts.

   Only the condition forms the shape matchers trust count as refinements;
   every other CC writer or opaque statement is recorded as poison.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "tree-cfg.h"
#include "cfganal.h"
#include "cfgloop.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-derive.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-cc-region.h"

/* Hardware CC-stack depth.  */
static const unsigned RVTT_CC_MAX_DEPTH = 8;

/* Per-statement classification.  */

enum stmt_cc_kind
{
  STMT_CC_NONE,		/* no CC involvement recorded */
  STMT_CC_PUSHC,	/* sfppushc (0): opens a frame */
  STMT_CC_POPC,		/* sfppopc (0): closes the current frame */
  STMT_CC_REFINE,	/* positive mask-refinement vocabulary */
  STMT_CC_ENCC,		/* sfpencc / sfpencc_all_lanes */
  STMT_CC_OTHER,	/* CC writer outside the vocabulary */
  STMT_CC_OPAQUE,	/* raw asm / call with unknown body */
  STMT_CC_BREAKER,	/* pushc/popc with nonzero or non-constant arg */
};
/* Classify STMT.  Only sfppushc/sfppopc with a constant 0 argument open
   or close a frame; any other argument is a BREAKER.  */

static stmt_cc_kind
classify_stmt (gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return STMT_CC_NONE;
  if (gimple_code (stmt) == GIMPLE_ASM)
    return STMT_CC_OPAQUE;
  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    return STMT_CC_NONE;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    /* An unknown call may execute arbitrary CC instructions.  */
    return STMT_CC_OPAQUE;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfppushc:
      return rvtt_call_int_arg (call, 0) == 0 ? STMT_CC_PUSHC : STMT_CC_BREAKER;
    case rvtt_insn_data::sfppopc:
      return rvtt_call_int_arg (call, 0) == 0 ? STMT_CC_POPC : STMT_CC_BREAKER;
    /* The structured condition forms and the SETCC/COMPC they lower
       to.  SFPENCC is not a refinement.  */
    case rvtt_insn_data::sfpxpred:
    case rvtt_insn_data::sfpxlogic:
    case rvtt_insn_data::sfpxcond:
    case rvtt_insn_data::sfpxcmp:
    case rvtt_insn_data::sfpsetcc:
    case rvtt_insn_data::sfpcompc:
      return STMT_CC_REFINE;
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpencc_all_lanes:
      return STMT_CC_ENCC;
    case rvtt_insn_data::sfpconfig_i:
    case rvtt_insn_data::sfpwriteconfig_i:
    case rvtt_insn_data::sfpwriteconfig_v:
      {
	unsigned arg = insnd->id == rvtt_insn_data::sfpconfig_i ? 1
	  : insnd->id == rvtt_insn_data::sfpwriteconfig_i ? 5 : 2;
	unsigned dest;
	return rvtt_call_const_uarg (call, arg, &dest) && dest != 15
	  ? STMT_CC_NONE : STMT_CC_OPAQUE;
      }
    default:
      return insnd->sets_cc (call) ? STMT_CC_OTHER : STMT_CC_NONE;
    }
}

/* See rvtt-cc-region.h.  This differs from classify_stmt: sfppushc (0)
   does not set CC and so is not a window barrier, while sfppopc is.  */

bool
rvtt_cc_window_cc_event_p (gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return false;
  if (gimple_code (stmt) == GIMPLE_ASM)
    return true;
  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return true;
  if (insnd->sets_cc (call))
    return true;
  if (insnd->id == rvtt_insn_data::sfpconfig_i
      || insnd->id == rvtt_insn_data::sfpwriteconfig_i
      || insnd->id == rvtt_insn_data::sfpwriteconfig_v)
    {
      unsigned arg = insnd->id == rvtt_insn_data::sfpconfig_i ? 1
	: insnd->id == rvtt_insn_data::sfpwriteconfig_i ? 5 : 2;
      unsigned dest;
      return !rvtt_call_const_uarg (call, arg, &dest) || dest == 15;
    }
  /* The typed all-lanes SFPENCC has no CC flag.  */
  return insnd->id == rvtt_insn_data::sfpencc_all_lanes;
}

/* ------------------------------------------------------------------ */
/* Build machinery.  */

namespace {

/* Phase A per-block summary.  */
struct bb_summary
{
  bool breaker;
  /* Frames popped below the block's entry frame before any net push.  */
  unsigned entry_pops;
  /* Frames left open at block exit, in push order.  */
  auto_vec<gcall *, 2> net_pushes;
  /* Number of CC-relevant statements (any kind but NONE).  */
  unsigned n_cc_events;
  /* The block's only CC-relevant statement, if it is an sfppopc (0).  */
  gimple *sole_popc;
};

enum bb_state_kind { BB_UNSEEN, BB_SET, BB_DRAIN, BB_BROKEN };

struct bb_state
{
  bb_state_kind kind;
  /* BB_SET: frame stack at block entry (root first).  */
  auto_vec<rvtt_cc_region *, 4> stack;
  /* BB_DRAIN: the two stacks and the drain block.  */
  auto_vec<rvtt_cc_region *, 4> long_stack;
  basic_block drain_pred;
};

} /* anonymous namespace */

/* Build the tree for FN.  Queries stay valid until the IL changes.  */

rvtt_cc_region_tree::rvtt_cc_region_tree (function *fn)
  : m_fn (fn), m_root (nullptr), m_map (nullptr), m_opened (nullptr)
{
  build ();
}

rvtt_cc_region_tree::~rvtt_cc_region_tree ()
{
  clear ();
}

/* Release the region nodes and statement maps.  */

void
rvtt_cc_region_tree::clear ()
{
  for (rvtt_cc_region *r : m_regions)
    delete r;
  m_regions.truncate (0);
  delete m_map;
  delete m_opened;
  m_map = nullptr;
  m_opened = nullptr;
  m_root = nullptr;
}

/* Recompute the tree from the current IL.  This invalidates all region
   pointers handed out before.  */

void
rvtt_cc_region_tree::rebuild ()
{
  clear ();
  build ();
}

/* Record poison BIT on R and in the subtree flags of its ancestors.  */

static void
region_record_poison (rvtt_cc_region *r, unsigned bit)
{
  r->flags |= bit;
  for (rvtt_cc_region *a = r->parent; a; a = a->parent)
    a->subtree_flags |= bit;
}

/* Mark every non-root frame on STACK unstructured.  */

static void
mark_stack_unstructured (const vec<rvtt_cc_region *> &stack)
{
  for (unsigned i = 1; i < stack.length (); i++)
    stack[i]->flags |= RVTT_CCR_UNSTRUCTURED;
}

/* Build the tree for m_fn in the three phases described at the top of
   the file.  Any structural break marks the open frames unstructured,
   leaves the affected statements unmapped and clears m_complete.  */

void
rvtt_cc_region_tree::build ()
{
  m_complete = true;
  m_map = new hash_map<const gimple *, rvtt_cc_region *>;
  m_opened = new hash_map<const gimple *, rvtt_cc_region *>;

  m_root = new rvtt_cc_region ();
  m_root->index = 0;
  m_root->parent = nullptr;
  m_root->depth = 0;
  m_root->entry = nullptr;
  m_root->flags = 0;
  m_root->subtree_flags = 0;
  m_root->drain_join = nullptr;
  m_regions.safe_push (m_root);

  /* The region PUSHC opens, created on first use under PARENT.  Reaching
     the same pushc with a different parent is a structural conflict.  */
  auto region_for = [&] (gcall *pushc,
			 rvtt_cc_region *parent) -> rvtt_cc_region *
    {
      if (rvtt_cc_region **slot = m_opened->get (pushc))
	{
	  rvtt_cc_region *r = *slot;
	  if (r->parent != parent)
	    {
	      r->flags |= RVTT_CCR_UNSTRUCTURED;
	      return nullptr;
	    }
	  return r;
	}
      rvtt_cc_region *r = new rvtt_cc_region ();
      r->index = m_regions.length ();
      r->parent = parent;
      r->depth = parent->depth + 1;
      r->entry = pushc;
      r->flags = 0;
      r->subtree_flags = 0;
      r->drain_join = nullptr;
      m_regions.safe_push (r);
      m_opened->put (pushc, r);
      return r;
    };

  /* --- Phase A: per-block summaries.  --- */

  unsigned last_bb = last_basic_block_for_fn (m_fn);
  bb_summary *summaries = new bb_summary[last_bb];
  bb_state *states = new bb_state[last_bb];
  for (unsigned i = 0; i < last_bb; i++)
    {
      summaries[i].breaker = false;
      summaries[i].entry_pops = 0;
      summaries[i].n_cc_events = 0;
      summaries[i].sole_popc = nullptr;
      states[i].kind = BB_UNSEEN;
      states[i].drain_pred = nullptr;
    }

  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    {
      bb_summary &s = summaries[bb->index];
      unsigned local_pushes = 0;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  stmt_cc_kind kind = classify_stmt (stmt);
	  if (kind == STMT_CC_NONE)
	    continue;
	  s.n_cc_events++;
	  switch (kind)
	    {
	    case STMT_CC_PUSHC:
	      s.net_pushes.safe_push (as_a <gcall *> (stmt));
	      local_pushes++;
	      break;
	    case STMT_CC_POPC:
	      if (local_pushes)
		{
		  s.net_pushes.pop ();
		  local_pushes--;
		}
	      else
		s.entry_pops++;
	      if (s.n_cc_events == 1)
		s.sole_popc = stmt;
	      break;
	    case STMT_CC_BREAKER:
	      s.breaker = true;
	      break;
	    default:
	      break;
	    }
	}
      if (s.n_cc_events != 1)
	s.sole_popc = nullptr;
    }

  /* --- Phase B: frame-stack dataflow to a fixpoint.  --- */

  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (m_fn));
  int n_rpo = pre_and_rev_post_order_compute (NULL, rpo, false);

  /* Transfer: entry stack -> exit stack.  Returns false on a break.  */
  auto apply_summary = [&] (const bb_summary &s,
			    const vec<rvtt_cc_region *> &in,
			    vec<rvtt_cc_region *> &out) -> bool
    {
      out.truncate (0);
      for (rvtt_cc_region *r : in)
	out.safe_push (r);
      if (s.breaker || s.entry_pops >= out.length ())
	return false;
      for (unsigned i = 0; i < s.entry_pops; i++)
	out.pop ();
      for (gcall *pushc : s.net_pushes)
	{
	  rvtt_cc_region *r = region_for (pushc, out.last ());
	  if (!r || r->depth > RVTT_CC_MAX_DEPTH)
	    {
	      if (r)
		r->flags |= RVTT_CCR_UNSTRUCTURED;
	      return false;
	    }
	  out.safe_push (r);
	}
      return true;
    };

  /* The frame stack PRED presents along its edge to SUCC, or null when
     PRED contributes nothing yet.  BROKEN_OUT reports a broken pred.  */
  auto_vec<rvtt_cc_region *, 8> scratch;
  auto edge_stack = [&] (basic_block pred, basic_block succ,
			 bool *broken_out) -> const vec<rvtt_cc_region *> *
    {
      *broken_out = false;
      if (pred == ENTRY_BLOCK_PTR_FOR_FN (m_fn))
	{
	  scratch.truncate (0);
	  scratch.safe_push (m_root);
	  return &scratch;
	}
      bb_state &ps = states[pred->index];
      switch (ps.kind)
	{
	case BB_UNSEEN:
	  return nullptr;
	case BB_BROKEN:
	  *broken_out = true;
	  return nullptr;
	case BB_SET:
	  {
	    const bb_summary &s = summaries[pred->index];
	    scratch.truncate (0);
	    if (!apply_summary (s, ps.stack, scratch))
	      {
		mark_stack_unstructured (ps.stack);
		*broken_out = true;
		return nullptr;
	      }
	    return &scratch;
	  }
	case BB_DRAIN:
	  /* A drain join passes the long stack to its drain block and the
	     popped stack to every other successor.  */
	  if (succ == ps.drain_pred)
	    return &ps.long_stack;
	  return &ps.stack;
	}
      return nullptr;
    };

  auto stacks_equal = [] (const vec<rvtt_cc_region *> &a,
			  const vec<rvtt_cc_region *> &b) -> bool
    {
      if (a.length () != b.length ())
	return false;
      for (unsigned i = 0; i < a.length (); i++)
	if (a[i] != b[i])
	  return false;
      return true;
    };

  /* A drain block: single pred JOIN, single succ JOIN, exactly one
     CC-relevant statement and it is an sfppopc (0).  */
  auto drain_block_p = [&] (basic_block p, basic_block join) -> bool
    {
      return summaries[p->index].sole_popc
	&& !summaries[p->index].breaker
	&& single_succ_p (p) && single_succ (p) == join
	&& single_pred_p (p) && single_pred (p) == join;
    };

  bool changed = true;
  unsigned rounds = 0;
  const unsigned round_cap = 32 + 4 * (unsigned) n_basic_blocks_for_fn (m_fn);
  bool capped = false;
  while (changed && !capped)
    {
      changed = false;
      if (++rounds > round_cap)
	{
	  capped = true;
	  break;
	}
      for (int i = 0; i < n_rpo; i++)
	{
	  basic_block jbb = BASIC_BLOCK_FOR_FN (m_fn, rpo[i]);
	  bb_state &st = states[jbb->index];

	  /* Meet over the incoming edges.  */
	  auto_vec<rvtt_cc_region *, 8> in_a, in_b;
	  bool have_a = false, have_b = false;
	  auto_vec<basic_block, 2> b_srcs;
	  bool broken = false;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, jbb->preds)
	    {
	      bool pred_broken;
	      const vec<rvtt_cc_region *> *v
		= edge_stack (e->src, jbb, &pred_broken);
	      if (pred_broken)
		{
		  broken = true;
		  break;
		}
	      if (!v)
		continue;
	      if (!have_a)
		{
		  in_a.truncate (0);
		  for (rvtt_cc_region *r : *v)
		    in_a.safe_push (r);
		  have_a = true;
		}
	      else if (stacks_equal (in_a, *v))
		;
	      else if (!have_b)
		{
		  in_b.truncate (0);
		  for (rvtt_cc_region *r : *v)
		    in_b.safe_push (r);
		  have_b = true;
		  b_srcs.safe_push (e->src);
		}
	      else if (stacks_equal (in_b, *v))
		b_srcs.safe_push (e->src);
	      else
		{
		  /* Three distinct frame stacks at one join.  */
		  mark_stack_unstructured (*v);
		  broken = true;
		  break;
		}
	    }

	  bb_state_kind new_kind;
	  auto_vec<rvtt_cc_region *, 8> new_stack, new_long;
	  basic_block new_drain = nullptr;
	  if (broken)
	    {
	      /* Frames open where structure broke are unproven.  */
	      if (have_a)
		mark_stack_unstructured (in_a);
	      if (have_b)
		mark_stack_unstructured (in_b);
	      new_kind = BB_BROKEN;
	    }
	  else if (!have_a)
	    new_kind = BB_UNSEEN;
	  else if (!have_b)
	    {
	      new_kind = BB_SET;
	      for (rvtt_cc_region *r : in_a)
		new_stack.safe_push (r);
	    }
	  else
	    {
	      /* Two distinct stacks are accepted only as a counted-pop diamond.
		 Identify the long and short stacks.  */
	      vec<rvtt_cc_region *> *lng = nullptr, *shrt = nullptr;
	      bool b_is_short = false;
	      if (in_a.length () == in_b.length () + 1)
		{
		  lng = &in_a;
		  shrt = &in_b;
		  b_is_short = true;
		}
	      else if (in_b.length () == in_a.length () + 1)
		{
		  lng = &in_b;
		  shrt = &in_a;
		}
	      bool ok = lng != nullptr;
	      if (ok)
		for (unsigned k = 0; k < shrt->length (); k++)
		  if ((*lng)[k] != (*shrt)[k])
		    {
		      ok = false;
		      break;
		    }
	      /* Every short-stack edge must come from a single drain block and
		 the join itself must have no CC events.  When the short stack is
		 B, b_srcs lists exactly its sources.  */
	      basic_block drain = nullptr;
	      if (ok && b_is_short)
		{
		  if (b_srcs.length () != 1
		      || !drain_block_p (b_srcs[0], jbb))
		    ok = false;
		  else
		    drain = b_srcs[0];
		}
	      else if (ok)
		{
		  /* The short stack is A, whose sources were not recorded.  Require
		     exactly one drain block among the preds, presenting the short
		     stack, and every other pred presenting the long stack.  */
		  basic_block cand = nullptr;
		  edge e2;
		  edge_iterator ei2;
		  bool multi = false;
		  FOR_EACH_EDGE (e2, ei2, jbb->preds)
		    if (drain_block_p (e2->src, jbb))
		      {
			if (cand)
			  multi = true;
			cand = e2->src;
		      }
		  if (!cand || multi)
		    ok = false;
		  else
		    FOR_EACH_EDGE (e2, ei2, jbb->preds)
		      {
			bool pb;
			const vec<rvtt_cc_region *> *v2
			  = edge_stack (e2->src, jbb, &pb);
			if (pb || (v2 && !stacks_equal (*v2, e2->src == cand
							? *shrt : *lng)))
			  {
			    ok = false;
			    break;
			  }
		      }
		  if (ok)
		    drain = cand;
		}
	      if (ok && summaries[jbb->index].n_cc_events != 0)
		ok = false;
	      if (ok)
		{
		  new_kind = BB_DRAIN;
		  for (rvtt_cc_region *r : *shrt)
		    new_stack.safe_push (r);
		  for (rvtt_cc_region *r : *lng)
		    new_long.safe_push (r);
		  new_drain = drain;
		  rvtt_cc_region *closed = lng->last ();
		  closed->drain_join = jbb;
		}
	      else
		{
		  mark_stack_unstructured (in_a);
		  mark_stack_unstructured (in_b);
		  new_kind = BB_BROKEN;
		}
	    }

	  /* Commit when changed.  */
	  bool differs = st.kind != new_kind;
	  if (!differs && new_kind == BB_SET)
	    differs = !stacks_equal (st.stack, new_stack);
	  else if (!differs && new_kind == BB_DRAIN)
	    differs = !stacks_equal (st.stack, new_stack)
	      || !stacks_equal (st.long_stack, new_long)
	      || st.drain_pred != new_drain;
	  if (differs)
	    {
	      /* Once broken, a block stays broken.  */
	      if (st.kind == BB_BROKEN)
		continue;
	      st.kind = new_kind;
	      st.stack.truncate (0);
	      for (rvtt_cc_region *r : new_stack)
		st.stack.safe_push (r);
	      st.long_stack.truncate (0);
	      for (rvtt_cc_region *r : new_long)
		st.long_stack.safe_push (r);
	      st.drain_pred = new_drain;
	      changed = true;
	    }
	}
    }

  if (capped)
    {
      /* No fixpoint within the bound: treat every block as broken.  */
      FOR_EACH_BB_FN (bb, m_fn)
	{
	  bb_state &st = states[bb->index];
	  if (st.kind == BB_SET || st.kind == BB_DRAIN)
	    mark_stack_unstructured (st.stack);
	  st.kind = BB_BROKEN;
	}
    }

  /* Frames still open on an edge to the function exit are never
     closed, so they are unstructured.  */
  {
    edge e;
    edge_iterator ei;
    FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (m_fn)->preds)
      {
	bool pb;
	const vec<rvtt_cc_region *> *v
	  = edge_stack (e->src, EXIT_BLOCK_PTR_FOR_FN (m_fn), &pb);
	if (v)
	  mark_stack_unstructured (*v);
      }
  }

  /* --- Phase C: statement mapping over the proven blocks.  --- */

  for (int i = 0; i < n_rpo; i++)
    {
      bb = BASIC_BLOCK_FOR_FN (m_fn, rpo[i]);
      bb_state &st = states[bb->index];
      if (st.kind == BB_BROKEN)
	/* A break with only the ambient frame open marks no region, so
	   record it here for ambient_preserving_fold_p.  */
	m_complete = false;
      if (st.kind != BB_SET)
	/* Drain joins execute under a varying frame; broken and unreached
	   blocks are unknown.  Leave them unmapped.  */
	continue;
      auto_vec<rvtt_cc_region *, 8> stack;
      for (rvtt_cc_region *r : st.stack)
	stack.safe_push (r);
      bool live = true;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (!live)
	    /* Statements after an in-block break stay unmapped.  */
	    continue;
	  stmt_cc_kind kind = classify_stmt (stmt);
	  rvtt_cc_region *top = stack.last ();
	  switch (kind)
	    {
	    case STMT_CC_PUSHC:
	      {
		rvtt_cc_region *r
		  = region_for (as_a <gcall *> (stmt), top);
		if (!r || r->depth > RVTT_CC_MAX_DEPTH)
		  {
		    if (r)
		      r->flags |= RVTT_CCR_UNSTRUCTURED;
		    mark_stack_unstructured (stack);
		    live = false;
		    m_complete = false;
		    break;
		  }
		stack.safe_push (r);
		m_map->put (stmt, r);
		break;
	      }
	    case STMT_CC_POPC:
	      if (stack.length () == 1)
		{
		  mark_stack_unstructured (stack);
		  live = false;
		  m_complete = false;
		  break;
		}
	      top->exits.safe_push (stmt);
	      m_map->put (stmt, top);
	      stack.pop ();
	      break;
	    case STMT_CC_REFINE:
	      top->refinements.safe_push (stmt);
	      m_map->put (stmt, top);
	      break;
	    case STMT_CC_ENCC:
	      region_record_poison (top, RVTT_CCR_ENCC);
	      if (rvtt_all_lanes_encc_p (stmt))
		top->flags |= RVTT_CCR_ALL_LANES;
	      m_map->put (stmt, top);
	      break;
	    case STMT_CC_OTHER:
	      region_record_poison (top, RVTT_CCR_VOCAB_EXTERNAL);
	      m_map->put (stmt, top);
	      break;
	    case STMT_CC_OPAQUE:
	      region_record_poison (top, RVTT_CCR_OPAQUE);
	      m_map->put (stmt, top);
	      break;
	    case STMT_CC_BREAKER:
	      mark_stack_unstructured (stack);
	      live = false;
	      m_complete = false;
	      break;
	    case STMT_CC_NONE:
	      m_map->put (stmt, top);
	      break;
	    }
	}
    }

  free (rpo);
  delete[] summaries;
  delete[] states;
}

/* ------------------------------------------------------------------ */
/* Queries.  */

/* See rvtt-cc-region.h.  */

rvtt_cc_region *
rvtt_cc_region_tree::region_of (gimple *stmt) const
{
  if (!stmt)
    return nullptr;
  rvtt_cc_region **slot = m_map->get (stmt);
  return slot ? *slot : nullptr;
}

/* See rvtt-cc-region.h.  */

rvtt_cc_region *
rvtt_cc_region_tree::region_opened_by (gimple *pushc) const
{
  if (!pushc)
    return nullptr;
  rvtt_cc_region **slot = m_opened->get (pushc);
  return slot ? *slot : nullptr;
}

/* See rvtt-cc-region.h.  A null R yields true, so callers must pass a
   region obtained from a mapping query.  */

bool
rvtt_cc_region_tree::structured_p (const rvtt_cc_region *r) const
{
  for (const rvtt_cc_region *a = r; a; a = a->parent)
    if (a->flags & RVTT_CCR_UNSTRUCTURED)
      return false;
  return true;
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_tree::same_frame_p (gimple *a, gimple *b) const
{
  rvtt_cc_region *ra = region_of (a);
  rvtt_cc_region *rb = region_of (b);
  return ra && ra == rb && structured_p (ra);
}

/* See rvtt-cc-region.h.  Checking INNER's frame also covers OUTER's,
   its parent.  */

bool
rvtt_cc_region_tree::parent_frame_p (gimple *outer, gimple *inner) const
{
  rvtt_cc_region *ro = region_of (outer);
  rvtt_cc_region *ri = region_of (inner);
  return ro && ri && ri->parent == ro && structured_p (ri);
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_tree::closes_frame_p (gimple *popc,
				     const rvtt_cc_region *r) const
{
  if (!popc || !r)
    return false;
  for (gimple *x : r->exits)
    if (x == popc)
      return true;
  return false;
}

/* See rvtt-cc-region.h.  Only R's own poison bits are checked.  */

bool
rvtt_cc_region_tree::refinements_pure_p (const rvtt_cc_region *r) const
{
  return r && structured_p (r)
    && !(r->flags
	 & (RVTT_CCR_ENCC | RVTT_CCR_VOCAB_EXTERNAL | RVTT_CCR_OPAQUE));
}

/* See rvtt-cc-region.h.  A null R counts as poisoned.  */

bool
rvtt_cc_region_tree::poisoned_p (const rvtt_cc_region *r) const
{
  if (!r)
    return true;
  return ((r->flags | r->subtree_flags)
	  & (RVTT_CCR_ENCC | RVTT_CCR_VOCAB_EXTERNAL | RVTT_CCR_OPAQUE)) != 0;
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_tree::region_all_lanes_p (const rvtt_cc_region *r) const
{
  return r && (r->flags & RVTT_CCR_ALL_LANES) != 0;
}

/* See rvtt-cc-region.h.  R must be non-null.  */

const vec<gimple *> &
rvtt_cc_region_tree::refinement_chain (const rvtt_cc_region *r) const
{
  return r->refinements;
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_tree::ambient_preserving_fold_p () const
{
  if (!m_complete || !m_root)
    return false;
  /* mark_stack_unstructured does not update subtree_flags, so scan
     every region.  */
  for (const rvtt_cc_region *r : m_regions)
    if (r->flags & (RVTT_CCR_UNSTRUCTURED | RVTT_CCR_OPAQUE))
      return false;
  /* The ambient frame itself must not change the mask.  */
  if (m_root->flags & (RVTT_CCR_ENCC | RVTT_CCR_VOCAB_EXTERNAL))
    return false;
  if (!m_root->refinements.is_empty ())
    return false;
  return true;
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_tree::loop_cc_ambient_preserving_p (class loop *loop) const
{
  bool ok = true;
  basic_block *body = get_loop_body (loop);
  for (unsigned ix = 0; ix != loop->num_nodes && ok; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi) && ok; gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	stmt_cc_kind kind = classify_stmt (stmt);
	if (kind == STMT_CC_NONE)
	  continue;
	rvtt_cc_region **slot = m_map->get (stmt);
	rvtt_cc_region *r = slot ? *slot : nullptr;
	if (!r || !structured_p (r))
	  {
	    /* Unmapped (broken block, drain join) or unstructured.  */
	    ok = false;
	    break;
	  }
	switch (kind)
	  {
	  case STMT_CC_PUSHC:
	    /* A frame left open across the backedge would be a join
	       mismatch and never be mapped.  */
	    break;
	  case STMT_CC_POPC:
	    /* Popping a save taken outside the loop would rewind past the
	       loop entry state.  */
	    if (!r->entry
		|| !flow_bb_inside_loop_p (loop, gimple_bb (r->entry)))
	      ok = false;
	    break;
	  case STMT_CC_REFINE:
	  case STMT_CC_OTHER:
	    /* These narrow relative to the enclosing frame's save (COMPC
	       included).  In the ambient frame there is no save to bound
	       them.  */
	    if (r == m_root)
	      ok = false;
	    break;
	  case STMT_CC_ENCC:
	    /* May widen beyond the entry state, even in the all-lanes form,
	       since the entry state is not known to be all lanes.  */
	    ok = false;
	    break;
	  case STMT_CC_OPAQUE:
	    /* A raw .ttinsn word that never touches the lane-enable state
	       (RVTT_RAW_CC_INERT, rvtt-raw-boundary.cc) is transparent here.
	       The ALL_LANES class widens and refuses, as do unknown calls.  */
	    {
	      gasm *a = dyn_cast <gasm *> (stmt);
	      uint32_t w;
	      if (!a || !rvtt_raw_ttinsn_word_p (a, &w)
		  || rvtt_raw_cc_word_class (w) != RVTT_RAW_CC_INERT)
		ok = false;
	    }
	    break;
	  case STMT_CC_BREAKER:
	    ok = false;
	    break;
	  case STMT_CC_NONE:
	    break;
	  }
      }
  free (body);
  return ok;
}

/* Full-lane proofs share the independent column-flag / LaneConfig
   must-analysis.  Function entry and an SFPENCC alone are insufficient.  */

bool
rvtt_cc_region_tree::block_entry_all_lanes_p (basic_block bb) const
{
  rvtt_gimple_lane_facts facts (m_fn);
  return facts.block_in (bb) == RVTT_LANE_ALL;
}

bool
rvtt_cc_region_tree::edge_entry_all_lanes_p (edge e) const
{
  if (!e || !e->src || (e->flags & EDGE_ABNORMAL)
      || e->src == ENTRY_BLOCK_PTR_FOR_FN (m_fn))
    return false;
  rvtt_gimple_lane_facts facts (m_fn);
  return facts.block_out (e->src) == RVTT_LANE_ALL;
}

/* See rvtt-cc-region.h.  */

bool
rvtt_cc_region_fn_ambient_preserving_p (function *fn)
{
  if (!fn || !fn->cfg)
    return false;
  rvtt_cc_region_tree tree (fn);
  return tree.ambient_preserving_fold_p ();
}
