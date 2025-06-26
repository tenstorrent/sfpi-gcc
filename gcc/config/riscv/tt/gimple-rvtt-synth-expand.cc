/* Pass to finalize the SFP synth instructions.
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#include "tree-into-ssa.h"
#include "rvtt.h"

#define DUMP(...) //fprintf (stderr, __VA_ARGS__)

static std::vector<tree> load_imm_map;

static long
get_int_arg (gcall *stmt, unsigned int arg)
{
  return TREE_INT_CST_LOW (gimple_call_arg (stmt, arg));
}

static void
finish_new_insn (gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt,
		 gimple *stmt)
{
  gcc_assert (new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
  update_stmt (new_stmt);
  if (insert_before)
    gsi_insert_before (gsip, new_stmt, GSI_SAME_STMT);
  else
    gsi_insert_after (gsip, new_stmt, GSI_SAME_STMT);
}

static tree
emit_sfpxloadi (int mod, const rvtt_insn_data *insnd,
		gcall *stmt, gimple_stmt_iterator *gsip)
{
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);

  tree addr = gimple_call_arg (stmt, 0);
  tree val = gimple_call_arg (stmt, insnd->nonimm_pos);
  unsigned int unique_id = get_int_arg (stmt, insnd->nonimm_pos + 2);

  gcall *new_stmt = gimple_build_call (new_insnd->decl, 5);
  gimple_call_set_arg (new_stmt, 0, addr);
  gimple_call_set_arg (new_stmt, 1, build_int_cst (integer_type_node, mod));
  gimple_call_set_arg (new_stmt, 2, val);
  gimple_call_set_arg (new_stmt, 3, integer_zero_node);
  gimple_call_set_arg (new_stmt, 4, integer_zero_node);
  tree lhs = make_ssa_name (build_vector_type (float_type_node, 64), new_stmt);
  gimple_call_set_lhs (new_stmt, lhs);
  finish_new_insn (gsip, true, new_stmt, stmt);

  rvtt_link_nonimm_prologue (load_imm_map, unique_id,
			     gimple_call_arg (stmt, insnd->nonimm_pos + 1),
			     new_insnd, new_stmt);

  return lhs;
}

static void
emit_sfpxloadi_lv (tree lhs, tree lower, unsigned int unique_id,
		   const rvtt_insn_data *insnd, gcall *stmt,
		   gimple_stmt_iterator *gsip, bool before)
{
  // Note: the 2nd part of the 2 step loadi is "_lv" to preserve the
  // register assignment of the first step.  This is "elegantly" (hackily?)
  // leveraged to handle the shifting/masking by ensuring the 2nd load
  // is the UPPER bits

  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi_lv);

  gcall *new_stmt = gimple_build_call (new_insnd->decl, 6);
  gimple_call_set_arg (new_stmt, 0, gimple_call_arg (stmt, 0));  // pointer
  gimple_call_set_arg (new_stmt, 1, lower);                     // live
  gimple_call_set_arg (new_stmt, 2, build_int_cst (integer_type_node, SFPLOADI_MOD0_UPPER)); // mod
  gimple_call_set_arg (new_stmt, 3, gimple_call_arg (stmt, insnd->nonimm_pos));  // nonimm orig
  gimple_call_set_arg (new_stmt, 4, integer_zero_node); // sum
  gimple_call_set_arg (new_stmt, 5, integer_zero_node); // id
  gimple_call_set_lhs (new_stmt, lhs);

  rvtt_link_nonimm_prologue (load_imm_map, unique_id,
			     gimple_call_arg (stmt, insnd->nonimm_pos + 1),
			     new_insnd, new_stmt);
  finish_new_insn (gsip, before, new_stmt, stmt);
}

static tree
emit_32bit_sfpxloads (const rvtt_insn_data *insnd, gcall *stmt,
		      gimple_stmt_iterator *gsip)
{
  tree tmp1 = emit_sfpxloadi (SFPLOADI_MOD0_LOWER, insnd, stmt, gsip);
  tree tmp2 = make_ssa_name (build_vector_type (float_type_node, 64), stmt);
  emit_sfpxloadi_lv (tmp2, tmp1, get_int_arg (stmt, insnd->nonimm_pos + 2) + 1, insnd, stmt, gsip, true);
  return tmp2;
}

static void
emit_sfpsetman_v (tree val, gimple *stmt, gimple_stmt_iterator *gsip)
{
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpsetman_v);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, 2);
  gimple_call_set_arg (new_stmt, 0, val);
  gimple_call_set_arg (new_stmt, 1, gimple_call_arg (stmt, 4));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (stmt));
  finish_new_insn (gsip, true, new_stmt, stmt);
}

static void
emit_sfpxfcmpv (tree val, const rvtt_insn_data *insnd,
		gcall *stmt, gimple_stmt_iterator *gsip)
{
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxfcmpv);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, 3);
  int mod = get_int_arg (stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK;
  gimple_call_set_arg (new_stmt, 0, gimple_call_arg (stmt, 1));
  gimple_call_set_arg (new_stmt, 1, val);
  gimple_call_set_arg (new_stmt, 2, build_int_cst (integer_type_node, mod));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (stmt));
  finish_new_insn (gsip, true, new_stmt, stmt);
}

static void
emit_sfpxicmpv (tree val, const rvtt_insn_data *insnd,
		gcall *stmt, gimple_stmt_iterator *gsip)
{
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxicmpv);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, 3);
  int mod = get_int_arg (stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK;
  gimple_call_set_arg (new_stmt, 0, val);
  gimple_call_set_arg (new_stmt, 1, gimple_call_arg (stmt, 1));
  gimple_call_set_arg (new_stmt, 2, build_int_cst (integer_type_node, mod));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (stmt));
  finish_new_insn (gsip, true, new_stmt, stmt);
}

static void
emit_sfpxiadd_v (tree val, int mod, gcall *stmt,
		 gimple_stmt_iterator *gsip)
{
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxiadd_v);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, 3);
  gimple_call_set_arg (new_stmt, 0, val);
  gimple_call_set_arg (new_stmt, 1, gimple_call_arg (stmt, 1));
  gimple_call_set_arg (new_stmt, 2, build_int_cst (integer_type_node, mod));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (stmt));
  finish_new_insn (gsip, true, new_stmt, stmt);
}

static void
expand_complex (gcall *stmt, const rvtt_insn_data *insnd, gimple_stmt_iterator *gsip)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxloadi:
      if ((get_int_arg (stmt, insnd->mod_pos) & SFPXLOADI_MOD0_32BIT_MASK) != 0)
	{
	  DUMP ("  expanding 32 bit %s, replace mod and emit %s\n", insnd->name, (insnd + 1)->name);

	  gimple_call_set_arg (stmt, insnd->mod_pos, build_int_cst (integer_type_node, SFPLOADI_MOD0_LOWER));
	  tree lhs = gimple_call_lhs (stmt);
	  tree tmp = make_ssa_name (build_vector_type (float_type_node, 64), stmt);
	  gimple_call_set_lhs (stmt, tmp);
	  emit_sfpxloadi_lv (lhs, tmp, get_int_arg (stmt, insnd->nonimm_pos + 2) + 1, insnd, stmt, gsip, false);
	}
      break;

    case rvtt_insn_data::sfpsetman_i:
      {
	DUMP ("  expanding %s to sfpxloadi+sfpsetman_v\n", insnd->name);
	tree tmp = emit_32bit_sfpxloads (insnd, stmt, gsip);
	emit_sfpsetman_v (tmp, stmt, gsip);
	gsi_remove (gsip, true);
	gsi_prev (gsip);
      }
      break;

    case rvtt_insn_data::sfpxicmps:
      {
	DUMP ("  expanding %s to sfpxloadi+sfpxicmpv\n", insnd->name);
	int mod = get_int_arg (stmt, insnd->mod_pos);
	tree tmp = mod & SFPXIADD_MOD1_16BIT
	  ? emit_sfpxloadi ((mod & SFPXIADD_MOD1_SIGNED) ? SFPLOADI_MOD0_SHORT : SFPLOADI_MOD0_USHORT,
			   insnd, stmt, gsip)
	  : emit_32bit_sfpxloads (insnd, stmt, gsip);
	emit_sfpxicmpv (tmp, insnd, stmt, gsip);
	gsi_remove (gsip, true);
	gsi_prev (gsip);
      }
    break;

    case rvtt_insn_data::sfpxfcmps:
      {
	DUMP ("  expanding %s to sfpxloadi+sfpxfcmpv\n", insnd->name);
	int fmt = get_int_arg (stmt, insnd->mod_pos) & SFPXSCMP_MOD1_FMT_MASK;
	tree tmp = (fmt == SFPXSCMP_MOD1_FMT_A || fmt == SFPXSCMP_MOD1_FMT_B)
	  ? emit_sfpxloadi ((fmt == SFPXSCMP_MOD1_FMT_A) ? SFPLOADI_MOD0_FLOATA : SFPLOADI_MOD0_FLOATB,
			    insnd, stmt, gsip)
	  : emit_32bit_sfpxloads (insnd, stmt, gsip);

	emit_sfpxfcmpv (tmp, insnd, stmt, gsip);
	gsi_remove (gsip, true);
	gsi_prev (gsip);
      }
    break;

    case rvtt_insn_data::sfpxiadd_i:
      {
	DUMP ("  expanding %s to sfpxloadi+sfpxiadd_v\n", insnd->name);
	int mod = get_int_arg (stmt, insnd->mod_pos);
	// Only supports 32 bit non-imm loads for wh so far...
	tree tmp = mod & SFPXIADD_MOD1_16BIT
	  ? emit_sfpxloadi ((mod & SFPXIADD_MOD1_SIGNED) ? SFPLOADI_MOD0_SHORT : SFPLOADI_MOD0_USHORT,
			   insnd, stmt, gsip)
	  : emit_32bit_sfpxloads (insnd, stmt, gsip);
	emit_sfpxiadd_v (tmp, mod & SFPXIADD_MOD1_IS_SUB, stmt, gsip);
	gsi_remove (gsip, true);
	gsi_prev (gsip);
	gsi_prev (gsip);
      }
    break;

    default:
      // Do nothing
      break;
    }

  // FIXME: Can this be deferred?
  update_ssa (TODO_update_ssa);
}

// This pass does 2 things:
//   - it "expands" (lowers) some non-imm insns from the wrapper to multiple
//     insns.  e.g., an integer add of a non-imm value must be converted to
//     an integer load followed by an add.  This must happen here so that the
//     correct shifting/masking of the nonimm can be done (this is done for
//     immediate values in the expand pass).
//   - any prologue incorrectly generated for immediate insns is deleted (see
//     nonimm tag pass)
static void
transform (function *fn)
{
  DUMP ("Expand-nonimm pass on: %s\n", function_name (fn));

  basic_block bb;

#if 0
  // Optimizations may have duplicated synth_opcode builtins. Renumber
  // the duplicates Using SSA def/use chains to locate their use (s).
  // ??? This might interfere with the relay optimization?? Make
  // sfpsynth_opcode intrinsic a const fn first.
  std::vector<gcall *> synth_opcodes;
  std::vector<gcall *> duplicates;
  FOR_EACH_BB_FN (bb, fn)
    for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gcall *stmt;
	const rvtt_insn_data *insnd;

	if (!rvtt_p (&insnd, &stmt, gsi)
	    || insnd->id != rvtt_insn_data::synth_opcode)
	  continue;

	unsigned id = TREE_INT_CST_LOW (gimple_call_arg (stmt, 1));
	if (synth_opcodes.size () <= id)
	  synth_opcodes.resize (id + 1);
	if (!synth_opcodes[id])
	  synth_opcodes[id] = stmt;
	else
	  duplicates.push_back (stmt);
      }
  if (synth_opcodes.empty ())
    // Nothing to do
    return;

  unsigned unique_id = synth_opcodes.size () + 1;
  for (auto *dup : duplicates)
    {
      tree id_cst = build_int_cst (integer_type_node, unique_id);
      // Find the use of this via the intermediate add.
      tree lhs = gimple_call_lhs (dup);
      gimple *sum;
      imm_use_iterator sum_iter;
      FOR_EACH_IMM_USE_STMT (sum, sum_iter, lhs)
	{
	  if (sum->code == GIMPLE_DEBUG)
	    continue;

	  if (tree sum_lhs = gimple_assign_lhs (sum))
	    {
	      gimple *use;
	      imm_use_iterator use_iter;
	      FOR_EACH_IMM_USE_STMT (use, use_iter, sum_lhs)
		{
		  if (use->code != GIMPLE_CALL)
		    continue;
		  auto *use_call = static_cast<gcall *> (use);
		  auto insnd = rvtt_get_insn_data (use_call);
		  gcc_assert (insnd && insnd->nonimm_pos >= 0);
		  gimple_call_set_arg (use_call, insnd->nonimm_pos + 2, id_cst);
		}
	    }
	}
      gimple_call_set_arg (dup, 1, id_cst);
      unique_id += 2;
    }
#endif
  
  load_imm_map.reserve (20);

  bool updated = false;
  FOR_EACH_BB_FN (bb, fn)
    {
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gcall *stmt;
	  const rvtt_insn_data *insnd;

	  if (rvtt_p (&insnd, &stmt, gsi) && insnd->nonimm_pos != -1)
	    {
	      tree immarg = gimple_call_arg (stmt, insnd->nonimm_pos);
	      if (TREE_CODE (immarg) == SSA_NAME)
		expand_complex (stmt, insnd, &gsi);
	      else
		{
		  // We have an immediate value, remove the unnecessary
		  // synth arg (other uses of it might remain)
		  gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, integer_zero_node);
		  update_stmt (stmt);
		}
	      updated = true;
	    }
	}
    }

  if (updated)
    {
      update_ssa (TODO_update_ssa);

      // Cleanup: remove unused insns created by the nonimm mess...
      // Find load_immediates which will be in 1 of 3 states:
      //  - no uses, delete
      //  - one use in an assignment which is a sum, which is unused, delete both
      //  - one use in an assignment which is a sum which is used
      DUMP ("  expand-nonimm cleanup...\n");
      rvtt_cleanup_nonimm_lis (fn);
      load_imm_map.resize (0);
      update_ssa (TODO_update_ssa);
    }
}

namespace {

const pass_data pass_data_rvtt_synth_expand =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_expand : public gimple_opt_pass
{
public:
  pass_rvtt_synth_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_synth_expand, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_RVTT;
  }
  virtual unsigned execute (function *fn) override
  {
    transform (fn);
    return 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_expand (gcc::context *ctxt)
{
  return new pass_rvtt_synth_expand (ctxt);
}
