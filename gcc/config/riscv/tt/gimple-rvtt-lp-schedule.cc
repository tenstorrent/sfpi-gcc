/* Opt-in pressure scheduling for straight-line SFPU arithmetic regions.
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
#include "gimple-pretty-print.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "attribs.h"
#include "rvtt.h"
#include "rvtt-schedule.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct value_info
{
  int def = -1;
  unsigned uses = 0;
  bool live_out = false;
};

struct schedule_solution
{
  std::vector<gcall *> order;
  unsigned claimed_peak = 0;
};

static bool
sfpu_vector_p (tree type)
{
  return TREE_CODE (type) == VECTOR_TYPE
    && lookup_attribute ("__xtt_vector", TYPE_ATTRIBUTES (type))
    && TYPE_MODE (type) == XTT32SImode;
}

/* Constant LREGs are encoded as operands and do not consume L0--L7.  */
static bool
constant_lreg_read_p (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfpreadlreg
      || gimple_call_num_args (call) != 1)
    return false;

  tree reg = gimple_call_arg (call, 0);
  return TREE_CODE (reg) == INTEGER_CST && TREE_INT_CST_LOW (reg) >= 8;
}

/* Phase one deliberately uses a tiny positive allowlist.  */
static bool
schedulable_p (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->is_live () || insnd->has_side_effects (call))
    return false;
  if (!gimple_call_nothrow_p (call))
    return false;

  tree lhs = gimple_call_lhs (call);
  if (!lhs || !sfpu_vector_p (TREE_TYPE (lhs)))
    return false;
  for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
    {
      tree arg = gimple_call_arg (call, argno);
      if (VECTOR_TYPE_P (TREE_TYPE (arg)))
	{
	  if (!sfpu_vector_p (TREE_TYPE (arg)))
	    return false;
	}
      else if (TREE_CODE (arg) != INTEGER_CST)
	return false;
    }

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmad:
      return true;

    default:
      return false;
    }
}

/* Phase one has no CC-state proof.  Reject an entire basic block if it
   contains predicated live-value forms or any explicit CC epoch operation;
   treating those operations merely as region boundaries is not sufficient.  */
static bool
unconditional_bb_p (basic_block bb)
{
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
       !gsi_end_p (gsi); gsi_next (&gsi))
    if (const rvtt_insn_data *insnd = rvtt_get_insn_data (*gsi))
      {
	if (insnd->is_live ())
	  return false;
	if (gcall *call = dyn_cast <gcall *> (*gsi))
	  if (insnd->sets_cc (call)
	      || insnd->id == rvtt_insn_data::sfppushc
	      || insnd->id == rvtt_insn_data::sfppopc)
	    return false;
      }
  return true;
}

/* The first implementation has no CFG liveness proof.  Restrict it to the
   sole real basic block of a straight-line function.  */
static bool
straight_line_bb_p (basic_block bb)
{
  return gimple_seq_empty_p (phi_nodes (bb))
    && single_pred_p (bb) && single_pred (bb)->index == ENTRY_BLOCK
    && single_succ_p (bb) && single_succ (bb)->index == EXIT_BLOCK;
}

static bool
free_constant_p (tree value)
{
  if (TREE_CODE (value) != SSA_NAME)
    return false;

  gimple *def = SSA_NAME_DEF_STMT (value);
  return is_a <gcall *> (def) && constant_lreg_read_p (as_a <gcall *> (def));
}

using value_map = std::unordered_map<tree, value_info>;

static value_map
build_values (basic_block bb, const std::vector<gcall *> &ops)
{
  value_map values;
  std::unordered_set<gimple *> region_stmts;
  region_stmts.reserve (ops.size ());
  for (gcall *call : ops)
    region_stmts.insert (call);

  for (unsigned i = 0; i != ops.size (); ++i)
    {
      gcall *call = ops[i];
      tree lhs = gimple_call_lhs (call);
      if (lhs && sfpu_vector_p (TREE_TYPE (lhs)))
	values[lhs].def = i;

      for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
	{
	  tree arg = gimple_call_arg (call, argno);
	  if (TREE_CODE (arg) != SSA_NAME || !sfpu_vector_p (TREE_TYPE (arg))
	      || free_constant_p (arg))
	    continue;
	  ++values[arg].uses;
	}
    }

  for (auto &entry : values)
    {
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, entry.first)
	if (!region_stmts.count (use))
	  {
	    entry.second.live_out = true;
	    break;
	}
    }

  /* Account for vector SSA values that are untouched by the region but live
     across it.  They still occupy LREGs.  This is especially important when
     a hard barrier splits a basic block into multiple scheduling regions.  */
  std::unordered_map<gimple *, unsigned> positions;
  unsigned position = 0;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
       !gsi_end_p (gsi); gsi_next (&gsi))
    if (!is_gimple_debug (*gsi))
      positions[*gsi] = position++;

  const unsigned first = positions[ops.front ()];
  const unsigned last = positions[ops.back ()];
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!sfpu_vector_p (TREE_TYPE (name)) || free_constant_p (name)
	  || values.count (name))
	continue;

      gimple *def = SSA_NAME_DEF_STMT (name);
      if (def && gimple_bb (def) == bb)
	{
	  auto def_pos = positions.find (def);
	  if (def_pos == positions.end () || def_pos->second >= first)
	    continue;
	}

      bool used_after = false;
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (gimple_bb (use) == bb)
	  {
	    auto use_pos = positions.find (use);
	    if (use_pos != positions.end () && use_pos->second > last)
	      {
		used_after = true;
		break;
	      }
	  }

      if (used_after)
	values[name].live_out = true;
    }

  return values;
}

/* Convert the GIMPLE region into stable solver-independent integer IDs.  The
   unordered pressure map must not leak its iteration order into the MILP.  */
static rvtt_sched_problem
build_solver_problem (const std::vector<gcall *> &ops,
		      const value_map &values,
		      const std::vector<gcall *> &preferred_order,
		      bool preferred_feasible)
{
  rvtt_sched_problem problem;
  problem.operation_count = ops.size ();
  problem.preferred_feasible = preferred_feasible;
  problem.preferred_slot.assign (ops.size (), 0);

  std::unordered_map<tree, unsigned> def_op;
  std::unordered_map<gcall *, unsigned> op_id;
  for (unsigned i = 0; i != ops.size (); ++i)
    {
      op_id[ops[i]] = i;
      tree lhs = gimple_call_lhs (ops[i]);
      if (lhs && sfpu_vector_p (TREE_TYPE (lhs)))
	def_op[lhs] = i;
    }
  for (unsigned slot = 0; slot != preferred_order.size (); ++slot)
    problem.preferred_slot[op_id.at (preferred_order[slot])] = slot;

  for (unsigned i = 0; i != ops.size (); ++i)
    {
      std::unordered_set<unsigned> seen;
      for (unsigned argno = 0; argno != gimple_call_num_args (ops[i]);
	   ++argno)
	{
	  auto found = def_op.find (gimple_call_arg (ops[i], argno));
	  if (found != def_op.end () && seen.insert (found->second).second)
	    problem.dependencies.emplace_back (found->second, i);
	}
    }

  std::vector<tree> stable_values;
  stable_values.reserve (values.size ());
  for (const auto &entry : values)
    stable_values.push_back (entry.first);
  std::sort (stable_values.begin (), stable_values.end (),
	     [] (tree lhs, tree rhs)
	     { return SSA_NAME_VERSION (lhs) < SSA_NAME_VERSION (rhs); });

  for (tree name : stable_values)
    {
      const value_info &info = values.find (name)->second;
      rvtt_sched_value value;
      value.def = info.def;
      value.live_out = info.live_out;
      for (unsigned i = 0; i != ops.size (); ++i)
	for (unsigned argno = 0; argno != gimple_call_num_args (ops[i]);
	     ++argno)
	  if (gimple_call_arg (ops[i], argno) == name)
	    {
	      value.uses.push_back (i);
	      break;
	    }
      problem.values.push_back (std::move (value));
    }
  return problem;
}

/* Model a destructive result as becoming live after all operands have been
   read in an issue slot.  Thus an output may legally reuse an operand whose
   final use is that same operation.  */
static unsigned
pressure_for_order (const std::vector<gcall *> &order,
		    const value_map &values)
{
  std::unordered_map<tree, unsigned> remaining;
  unsigned live = 0;
  for (const auto &entry : values)
    {
      remaining[entry.first]
	= entry.second.uses + (entry.second.live_out ? 1 : 0);
      if (entry.second.def < 0)
	++live;
    }

  unsigned peak = live;
  for (gcall *call : order)
    {
      for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
	{
	  tree arg = gimple_call_arg (call, argno);
	  auto found = remaining.find (arg);
	  if (found != remaining.end () && --found->second == 0)
	    --live;
	}

      tree lhs = gimple_call_lhs (call);
      auto found = remaining.find (lhs);
      if (found != remaining.end () && found->second != 0)
	++live;
      if (live > peak)
	peak = live;
    }
  return peak;
}

/* Independently rebuild the vector liveness model and validate a proposed
   order before mutating GIMPLE.  This deliberately does not consume the
   scheduler's predecessor, remaining-use, or peak bookkeeping.  */
static bool
validate_schedule (basic_block bb, const std::vector<gcall *> &ops,
		   const schedule_solution &solution, unsigned expected_old_peak,
		   unsigned &verified_peak, const char *&reason)
{
  reason = "unknown";
  verified_peak = expected_old_peak;

  if (solution.order.size () != ops.size ())
    {
      reason = "size";
      return false;
    }

  std::unordered_set<gcall *> original;
  original.reserve (ops.size ());
  for (gcall *call : ops)
    if (!original.insert (call).second)
      {
	reason = "duplicate-original";
	return false;
      }

  std::unordered_map<gimple *, unsigned> scheduled_position;
  scheduled_position.reserve (solution.order.size ());
  for (unsigned i = 0; i != solution.order.size (); ++i)
    {
      gcall *call = solution.order[i];
      if (!original.count (call))
	{
	  reason = "foreign-op";
	  return false;
	}
      if (!scheduled_position.emplace (call, i).second)
	{
	  reason = "duplicate-op";
	  return false;
	}
      if (gimple_bb (call) != bb || !schedulable_p (call))
	{
	  reason = "eligibility";
	  return false;
	}
    }

  /* Region collection treats every non-schedulable statement except a pure
     constant-LREG read as a boundary.  Check scalar as well as vector source
     availability against the actual insertion point used by apply_schedule.  */
  std::unordered_map<gimple *, unsigned> bb_position;
  unsigned position = 0;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
       !gsi_end_p (gsi); gsi_next (&gsi))
    bb_position[*gsi] = position++;

  const unsigned region_first = bb_position[ops.front ()];
  const unsigned region_last = bb_position[ops.back ()];
  for (unsigned i = 0; i != solution.order.size (); ++i)
    {
      gcall *call = solution.order[i];
      for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
	{
	  tree arg = gimple_call_arg (call, argno);
	  if (TREE_CODE (arg) != SSA_NAME)
	    continue;

	  gimple *def = SSA_NAME_DEF_STMT (arg);
	  auto internal = scheduled_position.find (def);
	  if (internal != scheduled_position.end ())
	    {
	      if (internal->second >= i)
		{
		  reason = "def-use";
		  return false;
		}
	      continue;
	    }

	  if (def && gimple_bb (def) == bb)
	    {
	      auto found = bb_position.find (def);
	      /* apply_schedule places every operation immediately after the last
		 original operation.  A skipped constant-LREG read inside the span
		 therefore still precedes every scheduled consumer.  */
	      if (found == bb_position.end () || found->second > region_last)
		{
		  reason = "source-availability";
		  return false;
		}
	    }
	}
    }

  /* Reconstruct pressure from scratch, including untouched values that are
     physically live through the region.  */
  value_map checked_values;
  for (unsigned i = 0; i != ops.size (); ++i)
    {
      gcall *call = ops[i];
      tree lhs = gimple_call_lhs (call);
      if (lhs && sfpu_vector_p (TREE_TYPE (lhs)))
	{
	  value_info &value = checked_values[lhs];
	  if (value.def >= 0)
	    {
	      reason = "duplicate-def";
	      return false;
	    }
	  value.def = i;
	}

      for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
	{
	  tree arg = gimple_call_arg (call, argno);
	  if (TREE_CODE (arg) == SSA_NAME
	      && sfpu_vector_p (TREE_TYPE (arg)) && !free_constant_p (arg))
	    ++checked_values[arg].uses;
	}
    }

  for (auto &entry : checked_values)
    {
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, entry.first)
	if (!is_gimple_debug (use)
	    && !original.count (dyn_cast <gcall *> (use)))
	  {
	    entry.second.live_out = true;
	    break;
	  }
    }

  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!sfpu_vector_p (TREE_TYPE (name)) || free_constant_p (name)
	  || checked_values.count (name))
	continue;

      gimple *def = SSA_NAME_DEF_STMT (name);
      if (def && gimple_bb (def) == bb)
	{
	  auto def_pos = bb_position.find (def);
	  if (def_pos == bb_position.end () || def_pos->second >= region_first)
	    continue;
	}

      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (!is_gimple_debug (use) && gimple_bb (use) == bb)
	  {
	    auto use_pos = bb_position.find (use);
	    if (use_pos != bb_position.end () && use_pos->second > region_last)
	      {
		checked_values[name].live_out = true;
		break;
	      }
	  }
    }

  auto checked_pressure = [&checked_values] (const std::vector<gcall *> &order,
					      unsigned &peak) -> bool
    {
      std::unordered_map<tree, unsigned> remaining;
      unsigned live = 0;
      for (const auto &entry : checked_values)
	{
	  remaining[entry.first]
	    = entry.second.uses + (entry.second.live_out ? 1 : 0);
	  if (entry.second.def < 0)
	    ++live;
	}

      peak = live;
      for (gcall *call : order)
	{
	  for (unsigned argno = 0; argno != gimple_call_num_args (call);
	       ++argno)
	    {
	      tree arg = gimple_call_arg (call, argno);
	      auto found = remaining.find (arg);
	      if (found == remaining.end ())
		continue;
	      if (found->second == 0)
		return false;
	      if (--found->second == 0)
		--live;
	    }

	  tree lhs = gimple_call_lhs (call);
	  auto found = remaining.find (lhs);
	  if (found != remaining.end () && found->second != 0)
	    ++live;
	  if (live > peak)
	    peak = live;
	}
      return true;
    };

  unsigned verified_old_peak;
  if (!checked_pressure (ops, verified_old_peak)
      || verified_old_peak != expected_old_peak)
    {
      reason = "old-peak";
      return false;
    }
  if (!checked_pressure (solution.order, verified_peak))
    {
      reason = "liveness";
      return false;
    }
  if (verified_peak != solution.claimed_peak)
    {
      reason = "claimed-peak";
      return false;
    }
  if (verified_old_peak <= 8 || verified_peak >= verified_old_peak
	|| verified_peak > 8)
    {
      reason = "profitability";
      return false;
    }

  reason = "ok";
  return true;
}

/* Exercise the rejection path with deliberately malformed certificates
   derived from every accepted real schedule.  This is kept separate from
   scheduler construction: a bug in ready-list or MILP bookkeeping cannot
   make these malformed permutations valid.  */
static bool
validator_rejection_selftest (basic_block bb,
			      const std::vector<gcall *> &ops,
			      const schedule_solution &valid,
			      unsigned old_peak)
{
  if (valid.order.size () < 2)
    return false;

  unsigned ignored_peak;
  const char *reason;

  schedule_solution duplicate = valid;
  duplicate.order[1] = duplicate.order[0];
  bool rejected_duplicate
    = !validate_schedule (bb, ops, duplicate, old_peak, ignored_peak, reason)
      && !strcmp (reason, "duplicate-op");

  schedule_solution false_peak = valid;
  ++false_peak.claimed_peak;
  bool rejected_peak
    = !validate_schedule (bb, ops, false_peak, old_peak, ignored_peak, reason)
      && !strcmp (reason, "claimed-peak");

  std::unordered_map<gimple *, unsigned> position;
  for (unsigned i = 0; i != valid.order.size (); ++i)
    position[valid.order[i]] = i;

  schedule_solution use_before_def = valid;
  bool made_bad_edge = false;
  for (unsigned use_position = 0;
       use_position != valid.order.size () && !made_bad_edge; ++use_position)
    for (unsigned argno = 0;
	 argno != gimple_call_num_args (valid.order[use_position]); ++argno)
      {
	tree arg = gimple_call_arg (valid.order[use_position], argno);
	if (TREE_CODE (arg) != SSA_NAME)
	  continue;
	auto found = position.find (SSA_NAME_DEF_STMT (arg));
	if (found == position.end () || found->second >= use_position)
	  continue;

	gcall *consumer = use_before_def.order[use_position];
	use_before_def.order.erase (use_before_def.order.begin () + use_position);
	use_before_def.order.insert (use_before_def.order.begin () + found->second,
				     consumer);
	made_bad_edge = true;
	break;
      }

  /* A region containing only independent operations has no internal edge to
     corrupt; duplicate/permutation and false-peak rejection still apply.  */
  bool rejected_edge
    = !made_bad_edge
      || (!validate_schedule (bb, ops, use_before_def, old_peak, ignored_peak,
			      reason)
	  && !strcmp (reason, "def-use"));

  gcc_checking_assert (rejected_duplicate && rejected_peak && rejected_edge);
  return rejected_duplicate && rejected_peak && rejected_edge;
}

/* A deterministic pressure-first list scheduler.  This is deliberately the
   pre-solver phase: it provides a materializable upper bound and a small,
   independently checkable rescue for graphs that exceed eight LREGs only
   because independent work is issued too late.  */
static bool
make_pressure_schedule (const std::vector<gcall *> &ops,
			const value_map &values,
			std::vector<gcall *> &schedule,
			unsigned &new_peak)
{
  const unsigned count = ops.size ();
  if (count > 32)
    return false;

  std::unordered_map<tree, unsigned> def_op;
  for (unsigned i = 0; i != count; ++i)
    {
      tree lhs = gimple_call_lhs (ops[i]);
      if (lhs && sfpu_vector_p (TREE_TYPE (lhs)))
	def_op[lhs] = i;
    }

  std::vector<unsigned> predecessors (count, 0);
  std::vector<std::vector<unsigned>> successors (count);
  for (unsigned i = 0; i != count; ++i)
    {
      std::unordered_set<unsigned> seen;
      for (unsigned argno = 0; argno != gimple_call_num_args (ops[i]); ++argno)
	{
	  auto found = def_op.find (gimple_call_arg (ops[i], argno));
	  if (found != def_op.end () && seen.insert (found->second).second)
	    {
	      ++predecessors[i];
	      successors[found->second].push_back (i);
	    }
	}
    }

  std::unordered_map<tree, unsigned> remaining;
  unsigned live = 0;
  for (const auto &entry : values)
    {
      remaining[entry.first]
	= entry.second.uses + (entry.second.live_out ? 1 : 0);
      if (entry.second.def < 0)
	++live;
    }

  unsigned peak = live;
  std::vector<bool> issued (count, false);
  schedule.clear ();
  schedule.reserve (count);

  while (schedule.size () != count)
    {
      int best = -1;
      unsigned best_peak = ~0u;
      unsigned best_live = ~0u;

      for (unsigned i = 0; i != count; ++i)
	{
	  if (issued[i] || predecessors[i] != 0)
	    continue;

	  std::unordered_map<tree, unsigned> operand_uses;
	  for (unsigned argno = 0; argno != gimple_call_num_args (ops[i]);
	       ++argno)
	    {
	      tree arg = gimple_call_arg (ops[i], argno);
	      if (remaining.count (arg))
		++operand_uses[arg];
	    }

	  unsigned deaths = 0;
	  for (const auto &use : operand_uses)
	    if (remaining[use.first] == use.second)
	      ++deaths;

	  tree lhs = gimple_call_lhs (ops[i]);
	  const bool result_live
	    = lhs && remaining.count (lhs) && remaining[lhs] != 0;
	  const unsigned candidate_live = live - deaths + result_live;
	  const unsigned candidate_peak
	    = candidate_live > peak ? candidate_live : peak;

	  if (best < 0 || candidate_peak < best_peak
	      || (candidate_peak == best_peak && candidate_live < best_live))
	    {
	      best = i;
	      best_peak = candidate_peak;
	      best_live = candidate_live;
	    }
	}

      if (best < 0)
	return false;

      gcall *call = ops[best];
      issued[best] = true;
      schedule.push_back (call);

      for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
	{
	  tree arg = gimple_call_arg (call, argno);
	  auto found = remaining.find (arg);
	  if (found != remaining.end () && --found->second == 0)
	    --live;
	}

      tree lhs = gimple_call_lhs (call);
      auto found = remaining.find (lhs);
      if (found != remaining.end () && found->second != 0)
	++live;
      if (live > peak)
	peak = live;

      for (unsigned next : successors[best])
	--predecessors[next];
    }

  new_peak = peak;
  return true;
}

static bool
apply_schedule (const std::vector<gcall *> &ops,
		const std::vector<gcall *> &schedule)
{
  bool different = false;
  for (unsigned i = 0; i != ops.size (); ++i)
    different |= ops[i] != schedule[i];
  if (!different)
    return false;

  gimple_stmt_iterator boundary = gsi_for_stmt (ops.back ());
  gsi_next (&boundary);
  if (gsi_end_p (boundary))
    return false;

  /* RVTT builtins conservatively carry virtual operands even though the
     positive allowlist above is target-pure.  Rebuild virtual SSA after the
     transactional reorder rather than leaving a stale program-order chain.  */
  for (gcall *call : ops)
    {
      if (tree vdef = gimple_vdef (call))
	{
	  unlink_stmt_vdef (call);
	  release_ssa_name (vdef);
	  gimple_set_vdef (call, NULL_TREE);
	}
      if (gimple_vuse (call))
	{
	  gimple_set_vuse (call, NULL_TREE);
	  update_stmt (call);
	}
    }

  for (gcall *call : schedule)
    {
      gimple_stmt_iterator from = gsi_for_stmt (call);
      gsi_move_before (&from, &boundary);
    }
  return true;
}

static bool
analyze_region (basic_block bb, const std::vector<gcall *> &ops)
{
  if (ops.size () < 2)
    return false;

  value_map values = build_values (bb, ops);
  const unsigned old_peak = pressure_for_order (ops, values);

  std::vector<gcall *> schedule;
  unsigned new_peak = old_peak;
  bool scheduled
    = old_peak > 8 && make_pressure_schedule (ops, values, schedule, new_peak);
  const bool list_scheduled = scheduled;
  rvtt_solver_solution solver_solution;
  bool solver_selected = false;

  if (old_peak > 8 && riscv_tt_pressure_schedule_use_milp
      && rvtt_lpsolve_available ())
    {
      solver_solution
	= rvtt_lpsolve_schedule (build_solver_problem (ops, values, schedule,
					      new_peak <= 8));
      if (solver_solution.status == rvtt_solver_status::optimal)
	{
	  std::vector<gcall *> solver_order;
	  solver_order.reserve (ops.size ());
	  bool valid_ids = solver_solution.order.size () == ops.size ();
	  for (unsigned id : solver_solution.order)
	    if (id >= ops.size ())
	      {
		valid_ids = false;
		break;
	      }
	    else
	      solver_order.push_back (ops[id]);

	  /* A malformed OPTIMAL certificate is never allowed to fall through to
	     mutation.  The independent validator below remains authoritative.  */
	  if (valid_ids)
	    {
	      schedule = std::move (solver_order);
	      new_peak = pressure_for_order (schedule, values);
	      scheduled = true;
	      solver_selected = true;
	    }
	  else
	    {
	      schedule.clear ();
	      new_peak = old_peak;
	      scheduled = false;
	      solver_solution.status = rvtt_solver_status::internal_error;
	      solver_solution.diagnostic = "gimple-ids";
	    }
	}
      else
	/* Solver absence/caps/failure preserve the already validated list
	   scheduler behavior.  */
	scheduled = list_scheduled;
    }
  schedule_solution solution { schedule, new_peak };
  unsigned verified_peak = old_peak;
  const char *validation_reason = scheduled ? "unknown" : "not-run";
  const bool validated
    = scheduled && validate_schedule (bb, ops, solution, old_peak,
				      verified_peak, validation_reason);
  const bool rejection_selftest
    = validated
      && validator_rejection_selftest (bb, ops, solution, old_peak);
  const bool applied
    = validated && rejection_selftest && apply_schedule (ops, solution.order);

  unsigned live_in = 0;
  for (const auto &entry : values)
    if (entry.second.def < 0)
      ++live_in;

  if (dump_file)
    {
      fprintf (dump_file,
	       "\nSFPU pressure region: bb=%d ops=%zu live-in=%u peak=%u\n",
	       bb->index, ops.size (), live_in, old_peak);
      fprintf (dump_file,
	       "SFPU pressure schedule: old-peak=%u new-peak=%u "
	       "validated=%s reason=%s rejection-selftest=%s applied=%s\n",
	       old_peak, verified_peak, validated ? "yes" : "no",
	       validation_reason, rejection_selftest ? "passed" : "not-run",
	       applied ? "yes" : "no");
      fprintf (dump_file,
	       "SFPU MILP: requested=%s available=%s status=%s detail=%s nodes=%u "
	       "selected=%s\n",
	       riscv_tt_pressure_schedule_use_milp ? "yes" : "no",
	       rvtt_lpsolve_available () ? "yes" : "no",
	       rvtt_solver_status_name (solver_solution.status),
	       solver_solution.diagnostic,
	       solver_solution.solver_nodes, solver_selected ? "yes" : "no");

      const std::vector<gcall *> &shown
	= scheduled ? solution.order : ops;
      for (unsigned i = 0; i != shown.size (); ++i)
	{
	  const rvtt_insn_data *insnd = rvtt_get_insn_data (shown[i]);
	  fprintf (dump_file, "  %2u %-12s ", i, insnd->name);
	  print_gimple_stmt (dump_file, shown[i], 0, TDF_SLIM);
	}
    }

  return applied;
}

static bool
flush_region (basic_block bb, std::vector<gcall *> &ops)
{
  bool changed = analyze_region (bb, ops);
  ops.clear ();
  return changed;
}

static bool
analyze_function (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (!straight_line_bb_p (bb))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "\nSFPU pressure region: bb=%d rejected=cfg\n",
		     bb->index);
	  continue;
	}
      if (!unconditional_bb_p (bb))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "\nSFPU pressure region: bb=%d rejected=cc-epoch\n",
		     bb->index);
	  continue;
	}

      std::vector<gcall *> ops;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = *gsi;
	  if (is_gimple_debug (stmt))
	    {
	      changed |= flush_region (bb, ops);
	      continue;
	    }

	  if (gcall *call = dyn_cast <gcall *> (stmt))
	    {
	      if (schedulable_p (call))
		{
		  ops.push_back (call);
		  continue;
		}
	      if (constant_lreg_read_p (call))
		continue;
	    }

	  changed |= flush_region (bb, ops);
	}
      changed |= flush_region (bb, ops);
    }
  return changed;
}

const pass_data pass_data_rvtt_lp_schedule =
{
  GIMPLE_PASS, /* type */
  "rvtt_lp_schedule", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_lp_schedule : public gimple_opt_pass
{
public:
  pass_rvtt_lp_schedule (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_lp_schedule, ctxt)
  {}

  bool gate (function *) final override
  {
    return optimize > 0 && debug_info_level == DINFO_LEVEL_NONE
      && (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
      && riscv_tt_opt_pressure_schedule;
  }

  unsigned execute (function *fn) final override
  {
    if (!analyze_function (fn))
      return 0;
    mark_virtual_operands_for_renaming (fn);
    return TODO_update_ssa_only_virtuals | TODO_verify_all;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_lp_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_lp_schedule (ctxt);
}
