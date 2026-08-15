/* Dump-only legality discovery for adjacent Dst-register iterations.
   Copyright (C) 2026 Tenstorrent Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "ssa-iterators.h"
#include "tree-ssa-operands.h"
#include "rvtt.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct dst_iteration
{
  std::vector<gcall *> calls;
  gcall *increment = nullptr;
  bool rejected = false;
};

static bool
integer_cst_eq (tree a, tree b)
{
  return TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST
    && tree_int_cst_equal (a, b);
}

static bool
vector_value_p (tree value)
{
  return value && VECTOR_TYPE_P (TREE_TYPE (value));
}

static bool
typed_dst_access_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpstore;
}

static bool
body_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (typed_dst_access_p (insnd))
    return true;
  if (insnd->id == rvtt_insn_data::synth_opcode)
    return false;
  return !insnd->has_side_effects (call) && !insnd->is_live ()
    && !insnd->sets_cc (call);
}

static bool
dst_access_legal_p (gcall *call, HOST_WIDE_INT increment,
		    unsigned address_bits)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  unsigned addr_arg = insnd->id == rvtt_insn_data::sfpload ? 1 : 2;
  unsigned mod_arg = insnd->id == rvtt_insn_data::sfpload ? 4 : 5;
  unsigned mode_arg = mod_arg + 1;
  tree addr = gimple_call_arg (call, addr_arg);
  tree mod = gimple_call_arg (call, mod_arg);
  tree mode = gimple_call_arg (call, mode_arg);
  unsigned no_increment_mode = TARGET_XTT_TENSIX_WH ? 3 : 7;
  if (!tree_fits_uhwi_p (addr) || !integer_zerop (mod)
      || TREE_CODE (mode) != INTEGER_CST
      || wi::to_wide (mode) != no_increment_mode)
    return false;
  unsigned HOST_WIDE_INT address = tree_to_uhwi (addr);
  unsigned HOST_WIDE_INT limit = (HOST_WIDE_INT_1U << address_bits) - 1;
  return address <= limit - increment;
}

static bool
increment_p (gcall *call, HOST_WIDE_INT *amount)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::ttincrwc
      || gimple_call_num_args (call) != 4)
    return false;
  for (unsigned i : { 0u, 2u, 3u })
    if (!integer_zerop (gimple_call_arg (call, i)))
      return false;
  tree dst = gimple_call_arg (call, 1);
  if (TREE_CODE (dst) != INTEGER_CST)
    return false;
  *amount = tree_to_shwi (dst);
  return *amount > 0 && *amount <= 3; // doubled value must fit signed CS4
}

static bool
same_body_p (const dst_iteration &a, const dst_iteration &b,
	     HOST_WIDE_INT increment, unsigned address_bits)
{
  if (a.rejected || b.rejected || a.calls.size () != b.calls.size ()
      || a.calls.empty ())
    return false;

  std::unordered_map<tree, tree> values;
  unsigned loads = 0;
  unsigned stores = 0;
  for (unsigned i = 0; i != a.calls.size (); ++i)
    {
      gcall *ca = a.calls[i];
      gcall *cb = b.calls[i];
      const rvtt_insn_data *ia = rvtt_get_insn_data (ca);
      const rvtt_insn_data *ib = rvtt_get_insn_data (cb);
      if (ia->id != ib->id
	  || gimple_call_num_args (ca) != gimple_call_num_args (cb))
	return false;

      bool access = typed_dst_access_p (ia);
      if (access)
	{
	  if (!dst_access_legal_p (ca, increment, address_bits)
	      || !dst_access_legal_p (cb, increment, address_bits))
	    return false;
	  ia->id == rvtt_insn_data::sfpload ? ++loads : ++stores;
	}

      unsigned addr_arg = ia->id == rvtt_insn_data::sfpload ? 1
	: ia->id == rvtt_insn_data::sfpstore ? 2 : ~0u;
      unsigned var_arg = access ? ia->var_arg () : ~0u;
      unsigned id_arg = access ? ia->id_arg () : ~0u;
      for (unsigned argno = 0; argno != gimple_call_num_args (ca); ++argno)
	{
	  if (argno == var_arg || argno == id_arg)
	    continue;
	  tree va = gimple_call_arg (ca, argno);
	  tree vb = gimple_call_arg (cb, argno);
	  if (argno == addr_arg)
	    {
	      if (!integer_cst_eq (va, vb))
		return false;
	      continue;
	    }
	  if (vector_value_p (va))
	    {
	      auto it = values.find (va);
	      if ((it == values.end () && va != vb)
		  || (it != values.end () && it->second != vb))
		return false;
	    }
	  else if (va != vb && !integer_cst_eq (va, vb))
	    return false;
	}

      tree la = gimple_call_lhs (ca);
      tree lb = gimple_call_lhs (cb);
      if (!!la != !!lb)
	return false;
      if (vector_value_p (la))
	values.emplace (la, lb);
    }
  return loads >= 2 && stores >= 1;
}

static bool
defs_closed_p (const dst_iteration &iteration)
{
  std::unordered_set<gimple *> members;
  for (gcall *call : iteration.calls)
    members.insert (call);
  members.insert (iteration.increment);

  for (gcall *call : iteration.calls)
    if (tree lhs = gimple_call_lhs (call))
      if (vector_value_p (lhs))
	{
	  imm_use_iterator iter;
	  gimple *use;
	  FOR_EACH_IMM_USE_STMT (use, iter, lhs)
	    if (!members.count (use))
	      return false;
	}
  return true;
}

static unsigned
discover (function *fn)
{
  const unsigned address_bits = TARGET_XTT_TENSIX_WH ? 14
    : TARGET_XTT_TENSIX_BH ? 13 : 10;
  unsigned candidates = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      dst_iteration previous;
      dst_iteration current;
      bool started = false;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  gcall *call = dyn_cast<gcall *> (stmt);
	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  HOST_WIDE_INT increment;
	  if (call && increment_p (call, &increment))
	    {
	      current.increment = call;
	      if (started && previous.increment)
		{
		  HOST_WIDE_INT prior;
		  bool legal = increment_p (previous.increment, &prior)
		    && prior == increment
		    && same_body_p (previous, current, increment, address_bits)
		    && defs_closed_p (previous) && defs_closed_p (current);
		  if (legal)
		    {
		      ++candidates;
		      if (dump_file)
			fprintf (dump_file,
				 "Dst-iteration candidate: bb=%d ops=%zu "
				 "addr-delta=%ld final-rwc=%ld target=%s emit=no\n",
				 bb->index, current.calls.size (), (long) increment,
				 (long) (increment * 2),
				 TARGET_XTT_TENSIX_WH ? "wh" :
				 TARGET_XTT_TENSIX_BH ? "bh" : "qsr");
		      previous = dst_iteration ();
		      current = dst_iteration ();
		      started = false;
		      continue;
		    }
		}
	      previous = current;
	      current = dst_iteration ();
	      started = true;
	      continue;
	    }

	  if (!insnd)
	    {
	      if (started && (gimple_has_volatile_ops (stmt)
			      || is_gimple_call (stmt)
			      || gimple_vuse (stmt) || gimple_vdef (stmt)))
		current.rejected = true;
	      continue;
	    }
	  if (insnd->id == rvtt_insn_data::synth_opcode)
	    continue;
	  if (!call || !body_call_p (call, insnd))
	    {
	      if (started)
		current.rejected = true;
	      continue;
	    }
	  if (typed_dst_access_p (insnd))
	    started = true;
	  if (started)
	    current.calls.push_back (call);
	}
    }
  return candidates;
}

const pass_data pass_data_rvtt_dst_iteration = {
  GIMPLE_PASS, "rvtt_dst_iteration", OPTGROUP_NONE, TV_NONE,
  PROP_cfg | PROP_ssa, 0, 0, 0, 0
};

class pass_rvtt_dst_iteration : public gimple_opt_pass
{
public:
  pass_rvtt_dst_iteration (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_dst_iteration, ctxt) {}
  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_dst_iteration_fusion;
  }
  unsigned execute (function *fn) final override
  {
    discover (fn);
    return 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_dst_iteration (gcc::context *ctxt)
{
  return new pass_rvtt_dst_iteration (ctxt);
}
