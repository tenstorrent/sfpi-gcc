/* Pass to eliminate dead SFPU intrinsic calls
   Copyright (C) 2026 Tenstorrent Inc.
   Originated Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* SFPU intrinsic calls carry virtual operands (the intrinsics are
   declared with side effects so that generic optimizers do not reorder
   them), which hides genuinely dead intrinsic computations from the
   generic tree-ssa DCE.  This pass performs mark-and-sweep dead-code
   elimination specialized to SFPU intrinsic calls:

     1. Collect every SFPU intrinsic call in the function.  Calls whose
	semantics really are effectful (stores, config writes, ...) per
	rvtt_insn_data::has_side_effects seed the live worklist; all
	others start out presumed dead.
     2. Propagate liveness backwards: for each live call, every SSA
	input's defining intrinsic call becomes live, walking through
	PHI nodes (with a visited set to terminate on cycles).
     3. Delete the calls that were never marked live, first unlinking
	any PHI nodes that use their results (such PHIs are themselves
	dead, or step 2 would have reached the call through them).

   The propagation is complete because SFPU vector values flow only
   between intrinsic calls and PHI nodes -- the vector types have no
   other consumers in the IL -- so walking those two statement kinds
   reaches every use-def chain that could keep a call alive.

   Runs under -mtt-tensix-optimize-dce (default on).  */

#define INCLUDE_VECTOR
#define INCLUDE_UNORDERED_SET
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
#include "tree-ssa.h"
#include "diagnostic-core.h"
#include "rvtt.h"

/* Populate INSNS with every side-effect-free SFPU intrinsic call in FN
   (the presumed-dead set) and WORKLIST with the effectful ones (the
   liveness seeds).  Synthetic-opcode markers are skipped: generic DCE
   handles them, this pass does not.  */

static void
gather_insns (function *fn, std::unordered_set<gcall *> &insns, std::vector<gcall *> &worklist)
{
  basic_block bb;

  if (dump_file)
    fprintf (dump_file, "Necessarily reachable insns\n");

  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	if (auto *insnd = rvtt_get_insn_data (*gsi))
	  {
	    auto *call = as_a <gcall *> (*gsi);
	    if (insnd->id == rvtt_insn_data::synth_opcode)
	      ; // Usual DCE works for this, (and this pass does not)
	    else if (insnd->has_side_effects (call))
	      {
		if (dump_file)
		  print_gimple_stmt (dump_file, call, 0);
		worklist.push_back (call);
	      }
	    else
	      insns.insert (call);
	  }
      }
}

/* Liveness propagation for one SSA input VAR of a live call: if VAR is
   defined by a presumed-dead intrinsic call, move that call from INSNS
   to WORKLIST (it is now known live); if VAR is defined by a PHI, walk
   all the PHI's arguments, using PHIS as the visited set so cyclic PHI
   webs terminate.  */

static void
gather_var_defs (std::unordered_set<gcall *> &insns, std::vector<gcall *> &worklist,
		 std::unordered_set<gphi *> &phis, tree var)
{
  auto *stmt = SSA_NAME_DEF_STMT (var);
  if (auto *phi = dyn_cast <gphi *> (stmt))
    {
      if (!phis.insert (phi).second)
	return;

      if (dump_file)
	print_gimple_stmt (dump_file, phi, 0);

      use_operand_p arg_p;
      ssa_op_iter ix;
      FOR_EACH_PHI_ARG (arg_p, phi, ix, SSA_OP_USE)
	{
	  tree arg = USE_FROM_PTR (arg_p);
	  if (SSA_VAR_P (arg))
	    gather_var_defs (insns, worklist, phis, arg);
	}
    }
  else if (auto *call = dyn_cast <gcall *> (stmt))
    if (insns.erase (call))
      {
	if (dump_file)
	  print_gimple_stmt (dump_file, call, 0);
	worklist.push_back (as_a <gcall *> (stmt));
      }
}

/* VAR is the result of a call about to be deleted.  Remove any PHI
   nodes using it (recursively, since a removed PHI's result may itself
   feed further PHIs).  Such PHIs are necessarily dead: were any of
   them live, liveness would have propagated back to VAR's defining
   call.  */

static void
remove_phi_uses (tree var)
{
  gimple *stmt;
  imm_use_iterator iter;
  FOR_EACH_IMM_USE_STMT (stmt, iter, var)
    if (auto *phi = dyn_cast <gphi *> (stmt))
      {
	if (dump_file)
	  print_gimple_stmt (dump_file, phi, 0);
	tree res = gimple_phi_result (phi);
	auto gsi = gsi_for_stmt (phi);
	remove_phi_node (&gsi, true);
	remove_phi_uses (res);
      }
}

namespace {

const pass_data pass_data_rvtt_dce =
{
  GIMPLE_PASS, /* type */
  "rvtt_dce", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_dce : public gimple_opt_pass
{
public:
  pass_rvtt_dce (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_dce, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_dce;
  }
  virtual unsigned execute (function *fn) override
  {
    std::unordered_set<gcall *> insns; // all the processed calls
    std::vector<gcall *> worklist; // calls to process
    std::unordered_set<gphi *> phis; // loop detection

    gather_insns (fn, insns, worklist);
    while (!worklist.empty ())
      {
	gcall *call = worklist.back ();
	worklist.pop_back ();

	if (dump_file)
	  {
	    fprintf (dump_file, "\nReachable from ");
	    print_gimple_stmt (dump_file, call, 0);
	  }
	for (unsigned ix = gimple_call_num_args (call); ix--;)
	  {
	    auto arg = gimple_call_arg (call, ix);
	    if (SSA_VAR_P (arg))
	      {
		gather_var_defs (insns, worklist, phis, arg);
		phis.clear ();
	      }
	  }
      }

    if (insns.empty ())
      return 0;

    if (dump_file)
      fprintf (dump_file, "\nDeleting unreachable\n");
    for (auto *call : insns)
      {
	if (tree var = gimple_call_lhs (call))
	  remove_phi_uses (var);

	if (dump_file)
	  print_gimple_stmt (dump_file, call, 0);
	auto gsi = gsi_for_stmt (call);
	unlink_stmt_vdef (call);
	gsi_remove (&gsi, true);
      }
    if (dump_file)
      fprintf (dump_file, "\n");

    return TODO_update_ssa;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_dce (gcc::context *ctxt)
{
  return new pass_rvtt_dce (ctxt);
}
