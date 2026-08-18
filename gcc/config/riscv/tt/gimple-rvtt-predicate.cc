/* Pass to expand (lower) boolean SFPU operators
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

#define INCLUDE_MAP
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
#include "tree-into-ssa.h"
#include "rvtt.h"
#include <unordered_map>

/*

  At this point v_if predication is represented by:

    dep = sfpxvif ();
    a = sfpxcmpXXX (...);
    b = sfpxbool (..., a, b);
    sfpxcondb (b, dep);

  There may be other insns between those, but there will not be any other
  sfpxcmp, sfpxvif,  sfpxbool, or sfpxcondb -- these do not nest at this
  point.  By construction the inputs to an sfpxbool will immediately preceed it.
  (More precisely the tree representing the conditional will be layed out in
  depth-first post order.  And it will be a tree, not a DAG.)

  We also require this to be a single BB.

  The hardware can easily implement AND, but adhacent setcc insns.  The
  simplest way of doing ors is by employing De Morgan -- OR = NOT ((NOT A) AND
  (NOT B)). We apply that transform here, this may require inserting pushc/popc
  insns around the logical operation, which we discover. 

  Once we've finalized an sfpxcmp insn, we expand it to one or more builtins
  backed by target insns.

*/

using WorkMap = std::unordered_map<gcall *, int>;

static bool
simplify_node (tree node, gcall *&leftmost, gcall *&rightmost,
	       WorkMap &work_map, int unique_id, bool negate)
{
  gcall *call = as_a <gcall> (SSA_NAME_DEF_STMT (node));

  auto I = work_map.find (call);
  gcc_assert (I != work_map.end ()
	      && I->second == unique_id); // source error
  I->second = -I->second;

  auto *insnd = rvtt_get_insn_data (call);
  unsigned mod = TREE_INT_CST_LOW (gimple_call_arg (call, insnd->mod_arg ()));
  if (insnd->id != rvtt_insn_data::sfpxbool)
    {
      // A compare
      if (negate)
	{
	  mod ^= (SFPXCMP_MOD1_CC_EQ ^ SFPXCMP_MOD1_CC_NE);
	  gimple_call_set_arg (call, insnd->mod_arg (),
			       build_int_cst (unsigned_type_node, mod));
	}
      leftmost = rightmost = call;
      return false;
    }

  // Boolean operation
  tree val = gimple_call_arg (call, 1);
  if (mod == SFXBOOL_MOD1_NOT)
    return simplify_node (val, leftmost, rightmost, work_map, unique_id, !negate);

  // FIXME: This is the original expand behaviour, we can do better
  // 1) notice a || b || c -> !(!a && !b && !c)
  // 2) use the pushc that allows anding and oring into the stack, to avoid the
  // loadi behavior

  bool negate_result = mod == SFPXBOOL_MOD1_OR;
  negate ^= negate_result;

  // Simplify LHS
  gcall *lhs_rightmost = nullptr;
  bool left_negate = simplify_node (val, leftmost, lhs_rigntmost, work_map, unique_id, negate);

  // Simplify RHS
  tree rhs = gimple_call_arg (call, 1);
  gcall &rhs_leftmost = nullptr;
  bool right_negate = simplify_node (rhs, rhs_leftmost, rightmost, work_map, unique_id, negate);

  if (right_negate)
    {
      
    }

  
}

// Simplify the boolean tree starting at ROOT. Update work_map for every 

static void
simplify_boolean_tree (tree root, WorkMap &work_map, int unique_id)
{
  
}

static bool
simplify_booleans (basic_block bb, WorkMap &work_map, int &unique_id)
{
  gcall *vif = nullptr;

  for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi);)
    {
      if (auto const *insn = rvtt_get_insn_data (*gsi))
	{
	  gcall *call = as_a <gcall *> (*gsi);
	  switch (insn->id)
	    {
	    case rvtt_insn_data::sfpxvif:
	      unique_id++;
	      gcc_assert (!vif); // Source error
	      vif = call;
	      break;

	    case rvtt_insn_data::sfpxcondb:
	      {
		gimple *start = SSA_NAME_DEF_STMT (cimple_call_arg (call, SFPXCONDB_START_ARG_POS));
		gcc_assert (vif == start); // Source error

		simplify_boolean_tree (gimple_call_arg (call, SFPXCONDB_TREE_ARG_POS), work_map, unique_id);

		// Delete the vif and the condb
		unlink_stmt_vdef (start);
		auto vif_gsi = gsi_for_stmt (start);
		gsi_remove (&vif_gsi, true);
		unlink_stmt_vdef (call);
		gsi_remove (&gsi, true);
		changed = true;
		continue;
	      }

	    case rvtt_insn_data::sfpxbool:
	    case rvtt_insn_data::sfpxicmps:
	    case rvtt_insn_data::sfpxicmpv:
	    case rvtt_insn_data::sfpxfcmps:
	    case rvtt_insn_data::sfpxfcmpv:
	    work_map.emplace (call, vif ? unique_id : 0);
	    break;
	  }
      
      gsi_next (&next_gsi);
    }
}

static bool
pred_expand (function *fn)
{
  bool changed = false;
  CmpxMap cmpx_map;
  basic_block bb;
  int unique_id = 0;

  // Simplify all the booleans, deletes xvif, xcondb, populates cmpx_map
  FOR_EACH_BB_FN (bb, fn)
    if (simplify_booleans (bb, cmpx_map, unique_id))
      changed = true;

  for (auto const &cmpx : cmpx_map)
    {
      auto [call, id] = cmpx;

      if (id >= 0)
	; // ERROR

      
    }

  return changed;
}
	     
namespace {

const pass_data pass_data_rvtt_pred_expand =
{
  GIMPLE_PASS, /* type */
  "rvtt_pred_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_pred_expand : public gimple_opt_pass
{
public:
  pass_rvtt_pred_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_pred_expand, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    return pred_expand (fn) ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_pred_expand (gcc::context *ctxt)
{
  return new pass_rvtt_pred_expand (ctxt);
}
