/* Tensix per-function IPA summaries (whole-body rescans).
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

/* Storage, cgraph hooks and staleness checks for the per-function
   summaries described in rvtt-ipa-summary.h, plus the CC summary and the
   translation-unit entry-point facts.  The classifiers that fill in the
   other parts of a summary live in the passes that use them.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "tree-cfg.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt.h"
#include "rvtt-cc-region.h"
#include "rvtt-refuse.h"
#include "rvtt-ipa-summary.h"

/* Free every cached per-face digest (init events, mop blocks, CC
   carry) and mark them all uncomputed, so the next consult recomputes
   from the current body.  The staleness signature fields are left for
   the caller to reset.  */

void
rvtt_ipa_fn_summary::release_faces ()
{
  init_events.release ();
  init_computed = false;
  for (rvtt_ipa_mop_block &b : mop_blocks)
    {
      b.preds.release ();
      b.events.release ();
    }
  mop_blocks.release ();
  mop_nblocks = 0;
  mop_computed = false;
  cc_computed = false;
  cc_ambient_preserving = false;
}

namespace {

/* Summaries keyed by node.  The removal hook below ensures a reused node
   pointer never finds a stale record.  */

hash_map<cgraph_node *, rvtt_ipa_fn_summary *> *summaries;
cgraph_node_hook_list *removal_hook;

/* cgraph removal hook: destroy NODE's summary record so a later node
   reusing the same storage can never observe a stale digest.  */

void
summary_node_removed (cgraph_node *node, void *)
{
  if (!summaries)
    return;
  if (rvtt_ipa_fn_summary **slot = summaries->get (node))
    {
      (*slot)->release_faces ();
      delete *slot;
      summaries->remove (node);
    }
}

/* Allocate the summary map and wire the cgraph removal hook, once.  */

void
ensure_storage ()
{
  if (summaries)
    return;
  summaries = new hash_map<cgraph_node *, rvtt_ipa_fn_summary *>;
  /* Only removal needs a hook.  Clones start with no record and compute
     their own on first use; copying the original's would describe the
     wrong body.  */
  removal_hook
    = symtab->add_cgraph_removal_hook (summary_node_removed, nullptr);
}

/* Set *BLOCKS and *EDGES to the block and edge counts of FN, a cheap
   staleness check used in every build.  */

void
body_signature (function *fn, int *blocks, int *edges)
{
  *blocks = n_basic_blocks_for_fn (fn);
  *edges = n_edges_for_fn (fn);
}

/* Return a hash of the statements and phis of FN, including each
   statement's code, used as a stronger staleness check under
   flag_checking.  */

unsigned
body_stmt_signature (function *fn)
{
  unsigned sig = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	sig = sig * 33 + 1;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	sig = sig * 33 + (unsigned) gimple_code (gsi_stmt (gsi)) + 2;
    }
  return sig;
}

} /* anonymous namespace */

/* See rvtt-ipa-summary.h.  */

rvtt_ipa_fn_summary *
rvtt_ipa_summary_get (cgraph_node *node)
{
  if (!node || !node->decl)
    return nullptr;
  function *fn = DECL_STRUCT_FUNCTION (node->decl);
  if (!fn || !fn->cfg)
    return nullptr;

  ensure_storage ();
  bool existed;
  rvtt_ipa_fn_summary *&slot = summaries->get_or_insert (node, &existed);
  if (!existed)
    slot = new rvtt_ipa_fn_summary;
  rvtt_ipa_fn_summary *s = slot;

  int blocks, edges;
  body_signature (fn, &blocks, &edges);
  if (s->sig_blocks < 0)
    {
      s->sig_blocks = blocks;
      s->sig_edges = edges;
      if (flag_checking)
	s->sig_stmts = body_stmt_signature (fn);
      return s;
    }

  bool stale = s->sig_blocks != blocks || s->sig_edges != edges;
  if (!stale && flag_checking)
    {
      /* A body change that was not followed by
	 rvtt_ipa_summary_invalidate is a compiler bug.  */
      unsigned stmts = body_stmt_signature (fn);
      if (stmts != s->sig_stmts)
	{
	  fprintf (stderr,
		   "ipa-summary-stale: %s: body mutated without "
		   "invalidation (stmt signature)\n", node->dump_name ());
	  gcc_assert (stmts == s->sig_stmts);
	}
    }
  if (stale)
    {
      /* The body changed without an invalidation.  Drop the cached data
	 so it is recomputed from the current body; under flag_checking
	 this is also an error.  */
      rvtt_refusal_fire_by_name ("ipa-summary-stale");
      if (flag_checking)
	{
	  fprintf (stderr,
		   "ipa-summary-stale: %s: body mutated without "
		   "invalidation (block/edge signature)\n",
		   node->dump_name ());
	  gcc_assert (!stale);
	}
      s->release_faces ();
      s->sig_blocks = blocks;
      s->sig_edges = edges;
      if (flag_checking)
	s->sig_stmts = body_stmt_signature (fn);
    }
  return s;
}

/* See rvtt-ipa-summary.h.  */

void
rvtt_ipa_summary_invalidate (function *fn)
{
  if (!summaries || !fn || !fn->decl)
    return;
  cgraph_node *node = cgraph_node::get (fn->decl);
  if (!node)
    return;
  if (rvtt_ipa_fn_summary **slot = summaries->get (node))
    {
      (*slot)->release_faces ();
      delete *slot;
      summaries->remove (node);
    }
}

/* See rvtt-ipa-summary.h.  */

bool
rvtt_ipa_cc_ambient_preserving_p (cgraph_node *node)
{
  rvtt_ipa_fn_summary *s = rvtt_ipa_summary_get (node);
  if (!s)
    return false;		/* No body to examine.  */
  if (!s->cc_computed)
    {
      function *fn = DECL_STRUCT_FUNCTION (node->decl);
      s->cc_ambient_preserving
	= rvtt_cc_region_fn_ambient_preserving_p (fn);
      s->cc_computed = true;
    }
  return s->cc_ambient_preserving;
}

/* Translation-unit entry-point facts.  These use only decl and symtab
   flags, never function bodies, so they can be computed in any pass.  The
   root criteria match those used by cross-call hoisting.  */

static rvtt_ipa_tu_anchor_facts anchor_facts;

/* Compute the TU anchor facts into F: whether a defined `_start' or a
   public `main' exists, how many defined functions qualify as entry
   roots (the anchor itself; every externally visible non-comdat
   definition when no anchor exists; forced-output/interrupt
   definitions), and whether anything roots the TU at all (an entry
   root, a static ctor/dtor, or an address-taken definition).  */

static void
compute_tu_anchors (rvtt_ipa_tu_anchor_facts *f)
{
  cgraph_node *anchor_start = nullptr, *anchor_main = nullptr;
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition)
	continue;
      const char *name = DECL_ASSEMBLER_NAME (node->decl)
	? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (node->decl)) : nullptr;
      if (name && !strcmp (name, "_start"))
	anchor_start = node;
      else if (DECL_NAME (node->decl) && MAIN_NAME_P (DECL_NAME (node->decl))
	       && TREE_PUBLIC (node->decl))
	anchor_main = node;
    }
  f->has_start = anchor_start != nullptr;
  f->has_main = anchor_main != nullptr;
  cgraph_node *anchor = anchor_start ? anchor_start : anchor_main;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition)
	continue;
      bool externally_visible
	= TREE_PUBLIC (node->decl) && !DECL_COMDAT (node->decl);
      bool forced = DECL_PRESERVE_P (node->decl)
	|| node->force_output || node->forced_by_abi
	|| lookup_attribute ("interrupt", DECL_ATTRIBUTES (node->decl));
      bool entry = node == anchor
	|| (!anchor && externally_visible)
	|| forced;
      if (entry)
	++f->n_entry_roots;
      if (entry
	  || DECL_STATIC_CONSTRUCTOR (node->decl)
	  || DECL_STATIC_DESTRUCTOR (node->decl)
	  || node->address_taken)
	f->rooted = true;
    }
}

/* The TU anchor facts, computed once on first consult and cached for
   the rest of the compilation (decl/symtab flags only, stable after
   IPA).  Under flag_checking every later consult recomputes and
   asserts the cached facts did not drift.  */

const rvtt_ipa_tu_anchor_facts &
rvtt_ipa_tu_anchors ()
{
  if (!anchor_facts.computed)
    {
      compute_tu_anchors (&anchor_facts);
      anchor_facts.computed = true;
      return anchor_facts;
    }
  if (flag_checking)
    {
      /* The facts depend only on symtab flags and must not change.  */
      rvtt_ipa_tu_anchor_facts again;
      compute_tu_anchors (&again);
      gcc_assert (again.has_start == anchor_facts.has_start
		  && again.has_main == anchor_facts.has_main
		  && again.n_entry_roots == anchor_facts.n_entry_roots
		  && again.rooted == anchor_facts.rooted);
    }
  return anchor_facts;
}
