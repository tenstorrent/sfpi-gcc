/* Pass to restore lost memory space pointer attributes
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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

/* The SFPI intrinsic headers tag pointer types with the memory-space
   attributes "rvtt_l1_ptr" (L1 scratch memory) and "rvtt_reg_ptr"
   (memory-mapped configuration registers).  Several consumers key off
   these tags: the pipeline description assigns per-space load
   latencies (rvtt-tune.md, via rvtt_l1_load_p/rvtt_reg_load_p), and
   the Wormhole read-after-write hazard workaround exempts
   register-space stores (rtl-rvtt-fix-raw.cc).

   Generic middle-end passes freely rebuild pointer SSA names (copy
   propagation, PRE, forwprop) using the bare pointer type, so by the
   end of the GIMPLE pipeline many pointer-typed SSA names have lost
   the attribute even though every value that can reach them derives
   from a tagged pointer.  Recovering the tags later, from RTL, proved
   unreliable -- the tree structures reachable from MEM_EXPRs are no
   longer maintained at that point -- so this pass runs as the last
   GIMPLE pass, immediately before expand, while the SSA web is still
   intact.

   Algorithm: for every pointer-typed SSA definition lacking both
   attributes, walk backwards through the defining statements (assigns
   through their operands, PHIs through all arguments) with a
   memoization map until a tagged source pointer is found; if one is,
   rebuild the definition's type with the source's attribute list.  The
   walk is a heuristic: a first-found tag wins, calls terminate the
   walk, and failure to find a tag simply leaves the type bare --
   untagged pointers are correct, just conservatively scheduled.

   FIXME: whether the attribute loss could instead be prevented at its
   sources (the intrinsic type machinery mishandling the attribute
   variants) has not been established; if it were, this pass could be
   retired.

   The transform is applied only for Wormhole Tensix
   (TARGET_XTT_TENSIX_WH), the only architecture whose downstream
   consumers currently depend on recovered tags.  */

#define INCLUDE_UNORDERED_MAP
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "cfghooks.h"
#include "tree-pass.h"
#include "ssa.h"
#include "cgraph.h"
#include "gimple-pretty-print.h"
#include "diagnostic-core.h"
#include "fold-const.h"
#include "trans-mem.h"
#include "stor-layout.h"
#include "print-tree.h"
#include "cfganal.h"
#include "tree-eh.h"
#include "gimple-iterator.h"
#include "gimple-fold.h"
#include "gimplify-me.h"
#include "gimple-walk.h"
#include "tree-cfg.h"
#include "tree-ssa-loop-manip.h"
#include "tree-ssa-loop-niter.h"
#include "tree-into-ssa.h"
#include "tree-dfa.h"
#include "tree-ssa.h"
#include "except.h"
#include "cfgloop.h"
#include "tree-ssa-propagate.h"
#include "value-prof.h"
#include "tree-inline.h"
#include "tree-ssa-live.h"
#include "omp-general.h"
#include "omp-expand.h"
#include "tree-cfgcleanup.h"
#include "gimplify.h"
#include "attribs.h"
#include "selftest.h"
#include "opts.h"
#include "asan.h"
#include "profile.h"
#include "rvtt.h"

static tree process_node (std::unordered_map<gimple *, tree> &stmts, gimple *stmt);

/* Return true if NODE's type carries the attribute named ATTRIB.  */

static bool
node_has_attrib (tree node, const char *attrib)
{
  return node && lookup_attribute (attrib, TYPE_ATTRIBUTES (TREE_TYPE (node)));
}

/* Examine one operand NODE of a statement being walked.  If NODE is an
   SSA name that already carries a memory-space attribute, it is the
   tagged source we are looking for; otherwise continue the backwards
   walk through its defining statement.  Returns the tagged source
   pointer, or NULL_TREE.  STMTS is the walk's memoization map.  */

static tree
check_node (std::unordered_map<gimple *, tree> &stmts, tree node)
{
  if (node != NULL_TREE
      && TREE_CODE (node) == SSA_NAME)
    {
      if (node_has_attrib (node, "rvtt_l1_ptr")
	  || node_has_attrib (node, "rvtt_reg_ptr"))
	return node;

      return process_node (stmts, SSA_NAME_DEF_STMT (node));
    }

  return NULL_TREE;
}

/* Backwards-walk one defining statement STMT looking for a tagged
   source pointer: PHIs walk all arguments, assignments walk their
   (up to three) operands, calls terminate the walk.  STMTS memoizes
   visited statements (seeded NULL_TREE on entry, which also breaks
   cycles through PHI webs).  Returns the tagged source pointer, or
   NULL_TREE.  */

static tree
process_node (std::unordered_map<gimple *, tree> &stmts, gimple *stmt)
{
  if (stmt == nullptr)
    return NULL_TREE;

  auto cached = stmts.find (stmt);
  if (cached != stmts.end ())
    return cached->second;
  stmts.insert (std::pair<gimple *, tree> (stmt, NULL_TREE));

  if (stmt->code == GIMPLE_PHI)
    {
      for (unsigned int i = 0; i < gimple_phi_num_args (stmt); i++)
	{
	  tree rhs = gimple_phi_arg_def (stmt, i);
	  if (TREE_CODE (rhs) == SSA_NAME)
	    {
	      gimple *origin = SSA_NAME_DEF_STMT (rhs);
	      if (tree ret = process_node (stmts, origin))
		return ret;
	    }
	}

      return NULL_TREE;
    }
  else if (stmt->code == GIMPLE_ASSIGN)
    {
      if (tree ret = check_node (stmts, gimple_assign_rhs1 (stmt)))
	return ret;
      if (tree ret = check_node (stmts, gimple_assign_rhs2 (stmt)))
	return ret;
      return check_node (stmts, gimple_assign_rhs3 (stmt));
    }

  // Calls (and anything else) terminate the walk.
  return NULL_TREE;
}

/* Walk FN: for each pointer-typed SSA definition without a memory
   space attribute, search its reaching definitions for a tagged
   pointer and copy the attribute list onto the definition's type.  */
static void
transform (function *fn)
{
  if (dump_file)
    fprintf (dump_file, "TT attrib pass on %s\n", function_name (fn));

  std::unordered_map<gimple *, tree> stmts;
  stmts.reserve (40);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
	tree lhs = gimple_get_lhs (gsi_stmt (gsi));
	if (lhs != NULL_TREE && TREE_CODE (lhs) == SSA_NAME && POINTER_TYPE_P (TREE_TYPE (lhs)))
	  {
	    tree type = TREE_TYPE (lhs);
	    if (!lookup_attribute ("rvtt_l1_ptr", TYPE_ATTRIBUTES (type))
		&& !lookup_attribute ("rvtt_reg_ptr", TYPE_ATTRIBUTES (type)))
	      {
		if (dump_file)
		  fprintf (dump_file, "no attrib, searching\n");
		if (tree result = process_node (stmts, gsi_stmt (gsi)))
		  {
		    type = build_type_attribute_variant (type, TYPE_ATTRIBUTES (TREE_TYPE (result)));
		    TREE_TYPE (lhs) = type;
		    if (dump_file)
		      fprintf (dump_file, "found one , adding attribute\n");
		  }
	      }
	    else
	      {
	        if (dump_file)
	          fprintf (dump_file, "found attrib, skipping\n");
	      }
	  }
      }
}

namespace {

const pass_data pass_data_rvtt_attrib =
{
  GIMPLE_PASS, /* type */
  "rvtt_attrib", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_attrib : public gimple_opt_pass
{
public:
  pass_rvtt_attrib (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_attrib, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_rvtt_attrib

} // anon namespace

/* Entry point to rvtt_attrib pass.	*/
unsigned int
pass_rvtt_attrib::execute (function *fun)
{
  if (TARGET_XTT_TENSIX_WH)
    transform (fun);

  return 0;
}

gimple_opt_pass *
make_pass_rvtt_attrib (gcc::context *ctxt)
{
  return new pass_rvtt_attrib (ctxt);
}
