/* Hoist loop-invariant Tensix immediate-vector materialization.
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

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "tree-ssa-loop-niter.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "rvtt-protos.h"
#include "rvtt.h"

#include <unordered_map>
#include <unordered_set>

namespace {

/* Return the unique non-abnormal loop entry edge, or null.  The loop is
   single-entry through this edge; the hoisted loads are placed on it (at
   the end of its source block when that block is a dedicated preheader,
   otherwise in a fresh block split from the edge at transform time, which
   never reshapes a loop this pass refuses).  */
static edge
loop_entry_edge (class loop *loop)
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

static bool
allowed_dst_effect_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpload_lv
    || insnd->id == rvtt_insn_data::sfpstore
    || insnd->id == rvtt_insn_data::ttincrwc;
}

/* Reject unrepresented calls, ordinary memory, CC changes, configuration,
   replay ownership, and every other volatile target effect.  Typed Dst
   load/store/counter operations are explicit architectural boundaries but do
   not change an invariant SFPLOADI value or the incoming CC state.  */
static bool
loop_has_barrier_p (class loop *loop)
{
  basic_block *body = get_loop_body (loop);
  bool barrier = false;
  for (unsigned ix = 0; ix != loop->num_nodes && !barrier; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	    || gimple_code (stmt) == GIMPLE_COND
	    || gimple_code (stmt) == GIMPLE_GOTO)
	  continue;

	const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	if (insnd)
	  {
	    gcall *call = as_a <gcall *> (stmt);
	    if (insnd->sets_cc (call)
		|| (insnd->has_side_effects (call)
		    && !allowed_dst_effect_p (insnd)))
	      barrier = true;
	    continue;
	  }

	if (gimple_code (stmt) == GIMPLE_ASM
	    || is_gimple_call (stmt)
	    || gimple_vuse (stmt) || gimple_vdef (stmt))
	  barrier = true;
      }
  free (body);
  return barrier;
}

/* Opaque assembly or an unrepresented call can own an architectural LREG
   without creating a vector SSA value.  Known RVTT calls expose their vector
   values and effects to the analyses below; everything else stays opaque —
   no content of any assembly statement is ever inspected.  */
static bool
opaque_stmt_p (gimple *stmt)
{
  return gimple_code (stmt) == GIMPLE_ASM
    || (is_gimple_call (stmt) && !rvtt_get_insn_data (stmt));
}

/* Region-scoped ownership proof.  The region that must be free of opaque
   state is {the dedicated preheader at/after the hoist insertion point}
   union {the loop body blocks}, and nothing more:

   - Every hoisted SFPLOADI value is consumed only inside the loop
     (all_uses_in_loop_p below), so the hoisted LREG writes are observable
     only between the hoist point and the loop's uses.
   - The loop is single-entry through its unique entry edge, so every path
     from the hoist point to any use lies entirely within the region.
     Opaque code before the hoist point or after loop exit cannot
     interleave with the hoisted live ranges: re-entering the loop
     re-executes the entry edge and therefore re-hoists.
   - Within the dedicated preheader, statements before the insertion point
     execute before the hoisted loads and are equally harmless.  With
     end-of-block insertion the only statement that can execute after the
     loads is a block-terminating statement (insertion goes before it);
     when that terminator is itself opaque the region is dirty and the
     loop refuses.  A block split from a shared entry edge is empty and
     trivially clean.

   This is CFG/dominance structure only: opacity is a single unexceptioned
   bit, and no assembly content or raw encoding is ever examined.  */
static bool
region_has_opaque_state_p (class loop *loop, edge entry)
{
  bool opaque = false;
  basic_block *body = get_loop_body (loop);
  for (unsigned ix = 0; ix != loop->num_nodes && !opaque; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi) && !opaque; gsi_next (&gsi))
      if (opaque_stmt_p (gsi_stmt (gsi)))
	opaque = true;
  free (body);
  if (opaque)
    return true;

  /* Preheader tail at/after the hoist insertion point.  */
  if (single_succ_p (entry->src))
    {
      gimple_stmt_iterator last = gsi_last_nondebug_bb (entry->src);
      if (!gsi_end_p (last) && stmt_ends_bb_p (gsi_stmt (last))
	  && opaque_stmt_p (gsi_stmt (last)))
	return true;
    }
  return false;
}

static bool
all_uses_in_loop_p (tree value, class loop *loop)
{
  imm_use_iterator iter;
  use_operand_p use_p;
  FOR_EACH_IMM_USE_FAST (use_p, iter, value)
    {
      gimple *use = USE_STMT (use_p);
      if (!is_gimple_debug (use)
	  && (!gimple_bb (use)
	      || !flow_bb_inside_loop_p (loop, gimple_bb (use))))
	return false;
    }
  return true;
}

/* The public SFPI wrappers pass the address of the architectural instruction
   buffer to sfpxloadi.  Direct builtin tests historically used a null pointer
   because the operand is not otherwise part of the SFPLOADI semantics.  Admit
   both canonical forms, but not an arbitrary buffer whose memory ownership
   has not been established by the target ABI.  */
static bool
canonical_insn_buffer_p (tree addr)
{
  if (integer_zerop (addr))
    return true;

  STRIP_NOPS (addr);
  if (TREE_CODE (addr) != ADDR_EXPR)
    return false;

  tree decl = TREE_OPERAND (addr, 0);
  return VAR_P (decl)
    && DECL_EXTERNAL (decl)
    && TREE_PUBLIC (decl)
    && DECL_ASSEMBLER_NAME (decl)
    && !strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)),
		"__instrn_buffer");
}

static bool
constant_load_p (gcall *call, class loop *loop)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfpxloadi)
    return false;

  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || !canonical_insn_buffer_p (gimple_call_arg (call, 0))
      || !all_uses_in_loop_p (lhs, loop))
    return false;

  for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
    if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
      return false;
  return true;
}

/* Keep the transformed loop within the architectural eight-LREG file before
   IRA.  Every hoisted value is live across the loop, as is each vector PHI
   (a loop-carried value) and each vector value defined outside the loop that
   is consumed directly by a non-PHI statement in it.  This is intentionally
   conservative: refuse the whole loop before changing virtual operands or
   statement placement when the bound is exceeded.  */
static bool
pressure_legal_p (class loop *loop, const auto_vec<gcall *> &loads,
		  bool report = true)
{
  constexpr unsigned LREG_COUNT = 8;
  std::unordered_set<tree> candidates;
  std::unordered_set<tree> pinned;
  std::unordered_set<tree> live;
  std::unordered_map<tree, unsigned> remaining;
  for (gcall *call : loads)
    {
      tree lhs = gimple_call_lhs (call);
      candidates.insert (lhs);
      pinned.insert (lhs);
      live.insert (lhs);
    }

  /* A vector can occupy an LREG throughout the loop without appearing in
     the loop at all: for example, a value read before the loop and stored
     after it.  Account for every vector SSA definition available at loop
     entry that has any non-debug use outside the loop.  This deliberately
     over-approximates values used only before the loop; false refusal is
     preferable to creating an unspillable LREG live range.  */
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!VECTOR_TYPE_P (TREE_TYPE (name)) || candidates.count (name))
	continue;
      bool outside_use = false;
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (!is_gimple_debug (use)
	    && (!gimple_bb (use)
		|| !flow_bb_inside_loop_p (loop, gimple_bb (use))))
	  {
	    outside_use = true;
	    break;
	  }
      if (outside_use)
	{
	  pinned.insert (name);
	  gimple *def = SSA_NAME_DEF_STMT (name);
	  basic_block def_bb = gimple_bb (def);
	  if (!def_bb
	      || (!flow_bb_inside_loop_p (loop, def_bb)
		  && dominated_by_p (CDI_DOMINATORS, loop->header, def_bb)))
	    live.insert (name);
	}
    }

  basic_block *body = get_loop_body (loop);
  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
    {
      basic_block bb = body[ix];
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	{
	  gphi *phi = psi.phi ();
	  tree lhs = gimple_phi_result (phi);
	  if (lhs && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    live.insert (lhs);
	  for (unsigned argno = 0; argno != gimple_phi_num_args (phi); ++argno)
	    {
	      tree use = gimple_phi_arg_def (phi, argno);
	      if (TREE_CODE (use) == SSA_NAME
		  && VECTOR_TYPE_P (TREE_TYPE (use)))
		++remaining[use];
	    }
	}

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  ssa_op_iter iter;
	  tree use;
	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	    if (VECTOR_TYPE_P (TREE_TYPE (use)))
	      {
		++remaining[use];
		gimple *def = SSA_NAME_DEF_STMT (use);
		basic_block def_bb = gimple_bb (def);
		if (!def_bb || !flow_bb_inside_loop_p (loop, def_bb))
		  {
		    pinned.insert (use);
		    live.insert (use);
		  }
	      }
	}
    }

  size_t peak = live.size ();
  basic_block latch = loop->latch;
  for (gimple_stmt_iterator gsi = gsi_start_bb (latch);
       !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      ssa_op_iter iter;
      tree use;
      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	if (VECTOR_TYPE_P (TREE_TYPE (use)))
	  {
	    auto found = remaining.find (use);
	    gcc_assert (found != remaining.end () && found->second);
	    if (!--found->second && !pinned.count (use))
	      live.erase (use);
	  }

      tree lhs = gimple_get_lhs (stmt);
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && VECTOR_TYPE_P (TREE_TYPE (lhs))
	  && !candidates.count (lhs))
	live.insert (lhs);
      peak = MAX (peak, live.size ());
    }
  free (body);

  if (peak <= LREG_COUNT)
    return true;
  if (report && dump_file)
    fprintf (dump_file,
	     "Invariant SFPU immediate hoist refused: loop LREG pressure %zu exceeds %u\n",
	     peak, LREG_COUNT);
  return false;
}

/* Estimate the number of SFPLOADI issues needed to materialize CALL's
   constant after the later immediate-shortening passes run.  Prefer keeping
   two-issue constants live when pressure prevents hoisting every invariant;
   one-issue values remain cheap to rematerialize in the loop.  This models
   only the target's immediate encodings.  It deliberately does not recognize
   particular values or source patterns.  */
static unsigned
materialization_cost (gcall *call)
{
  uint32_t value = TREE_INT_CST_LOW (gimple_call_arg (call, 1));
  unsigned upper = value >> 16;
  unsigned lower = value & 0xffff;
  if (!lower || !upper || (upper == 0xffff && (lower >> 15)))
    return 1;

  /* A full value whose low thirteen bits are zero and whose exponent fits
     binary16 can use the single-issue FLOATA encoding.  */
  unsigned exponent = (value >> 23) & 0xff;
  return !(value & 0x1fff)
    && exponent > 127 - 15 && exponent < (127 - 15) + 31 ? 1 : 2;
}

/* Select the most expensive invariant materializations which fit the
   architectural LREG pressure bound.  The old all-or-nothing policy left
   every constant in a counted loop when only one live range exceeded the
   bound.  Greedy selection is safe because pressure_legal_p re-runs the full
   conservative liveness proof after every addition; it is also deterministic
   because equal-cost candidates retain source order.  */
static auto_vec<gcall *>
select_pressure_legal_loads (class loop *loop, auto_vec<gcall *> &loads)
{
  std::stable_sort (loads.begin (), loads.end (),
		    [] (gcall *a, gcall *b)
		    {
		      return materialization_cost (a) > materialization_cost (b);
		    });

  auto_vec<gcall *> selected;
  for (gcall *call : loads)
    {
      selected.safe_push (call);
      if (!pressure_legal_p (loop, selected, false))
	{
	  selected.pop ();
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Invariant SFPU immediate left in loop by LREG pressure: ");
	      print_gimple_stmt (dump_file, call, 0);
	    }
	}
    }
  return selected;
}

/* Return the header phi of LOOP from which X is derived by a chain of
   non-memory assignments whose other operands are all constants, or null.
   This mirrors the niter brute-force chain discovery without requiring
   canonical preheaders, which this pass never establishes.  */
static gphi *
constant_chain_phi (class loop *loop, tree x)
{
  while (TREE_CODE (x) == SSA_NAME)
    {
      gimple *stmt = SSA_NAME_DEF_STMT (x);
      basic_block bb = gimple_bb (stmt);
      if (!bb || !flow_bb_inside_loop_p (loop, bb))
	return nullptr;
      if (gphi *phi = dyn_cast <gphi *> (stmt))
	return bb == loop->header ? phi : nullptr;
      if (!is_gimple_assign (stmt)
	  || gimple_assign_rhs_class (stmt) == GIMPLE_TERNARY_RHS
	  || gimple_references_memory_p (stmt))
	return nullptr;
      tree use = SINGLE_SSA_TREE_OPERAND (stmt, SSA_OP_USE);
      if (!use)
	return nullptr;
      x = use;
    }
  return nullptr;
}

/* Value of X in a header evaluation of the loop whose chain phi (as found by
   constant_chain_phi) carries the constant BASE.  A null X denotes the phi
   itself.  Returns NULL_TREE whenever the value does not fold to a constant,
   so callers refuse instead of speculating.  */
static tree
constant_chain_value (tree x, tree base)
{
  if (!x)
    return base;
  if (is_gimple_min_invariant (x))
    return x;

  gimple *stmt = SSA_NAME_DEF_STMT (x);
  if (gimple_code (stmt) == GIMPLE_PHI)
    return base;

  tree_code code = gimple_assign_rhs_code (stmt);
  tree type = TREE_TYPE (gimple_assign_lhs (stmt));
  tree value = NULL_TREE;
  if (gimple_assign_ssa_name_copy_p (stmt))
    value = constant_chain_value (gimple_assign_rhs1 (stmt), base);
  else if (gimple_assign_rhs_class (stmt) == GIMPLE_UNARY_RHS
	   && TREE_CODE (gimple_assign_rhs1 (stmt)) == SSA_NAME)
    {
      tree rhs = constant_chain_value (gimple_assign_rhs1 (stmt), base);
      value = rhs ? fold_unary (code, type, rhs) : NULL_TREE;
    }
  else if (gimple_assign_rhs_class (stmt) == GIMPLE_BINARY_RHS)
    {
      tree rhs1 = gimple_assign_rhs1 (stmt);
      tree rhs2 = gimple_assign_rhs2 (stmt);
      if (TREE_CODE (rhs1) == SSA_NAME)
	rhs1 = constant_chain_value (rhs1, base);
      else if (TREE_CODE (rhs2) == SSA_NAME)
	rhs2 = constant_chain_value (rhs2, base);
      value = rhs1 && rhs2 ? fold_binary (code, type, rhs1, rhs2) : NULL_TREE;
    }
  return value && is_gimple_min_invariant (value) ? value : NULL_TREE;
}

/* Ask the generic complete-unroller to expose a short, exactly counted loop
   when replay formation is also requested.  The replay pass can then compress
   identical copies into launches without retaining scalar induction control.
   The trip count is proved by bounded constant evaluation of the header test
   reached through the unique ENTRY edge; scalar-evolution niter analysis is
   not usable here because this pass must not reshape an ineligible CFG and so
   never guarantees canonical preheaders.  Refuse whenever any step fails to
   fold.  Keep a hard structural size bound because final replay-buffer
   eligibility is intentionally decided later, after lowering and
   allocation.  */
static bool
short_constant_replay_loop_p (class loop *loop, edge entry)
{
  constexpr unsigned MAX_REPLAY_UNROLL_ITERATIONS = 16;

  gimple_stmt_iterator last = gsi_last_bb (loop->header);
  gcond *cond = gsi_end_p (last)
    ? nullptr : dyn_cast <gcond *> (gsi_stmt (last));
  edge latch = loop->latch ? find_edge (loop->latch, loop->header) : nullptr;
  if (!cond || !entry || !latch)
    return false;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (loop->header, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return false;

  tree op[2] = { gimple_cond_lhs (cond), gimple_cond_rhs (cond) };
  tree value[2], next[2];
  for (unsigned j = 0; j < 2; j++)
    {
      if (is_gimple_min_invariant (op[j]))
	{
	  value[j] = op[j];
	  next[j] = NULL_TREE;
	  op[j] = NULL_TREE;
	  continue;
	}
      gphi *phi = constant_chain_phi (loop, op[j]);
      if (!phi)
	return false;
      value[j] = PHI_ARG_DEF_FROM_EDGE (phi, entry);
      next[j] = PHI_ARG_DEF_FROM_EDGE (phi, latch);
      if (!is_gimple_min_invariant (value[j]))
	return false;
      if (TREE_CODE (next[j]) == SSA_NAME
	  && constant_chain_phi (loop, next[j]) != phi)
	return false;
    }

  /* Evaluate the header test iteration by iteration; the number of times it
     branches back into the body is the exact backedge count.  */
  bool short_loop = false;
  fold_defer_overflow_warnings ();
  for (unsigned backedges = 0;
       backedges < MAX_REPLAY_UNROLL_ITERATIONS; backedges++)
    {
      tree lhs = constant_chain_value (op[0], value[0]);
      tree rhs = constant_chain_value (op[1], value[1]);
      tree test = lhs && rhs
	? fold_binary (gimple_cond_code (cond), boolean_type_node, lhs, rhs)
	: NULL_TREE;
      if (!test || TREE_CODE (test) != INTEGER_CST)
	break;

      edge taken = integer_zerop (test) ? false_edge : true_edge;
      if (taken->dest != loop->latch)
	{
	  short_loop = backedges >= 1;
	  break;
	}

      value[0] = constant_chain_value (next[0], value[0]);
      value[1] = constant_chain_value (next[1], value[1]);
      if (!value[0] || !value[1])
	break;
    }
  fold_undefer_and_ignore_overflow_warnings ();
  return short_loop;
}

/* Prove that the loop's first header test enters its sole body block.  This
   avoids speculating an architectural LREG write out of a zero-trip loop,
   without requesting loop normalization (which could perturb an ineligible
   function).  */
static bool
first_iteration_executes_p (class loop *loop, edge entry)
{
  gimple_stmt_iterator last = gsi_last_bb (loop->header);
  gcond *cond = gsi_end_p (last)
    ? nullptr : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond || !entry)
    return false;

  auto initial_value = [loop, entry] (tree value) -> tree
    {
      if (TREE_CODE (value) != SSA_NAME)
	return value;
      gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (value));
      if (!phi || gimple_bb (phi) != loop->header)
	return value;
      return PHI_ARG_DEF_FROM_EDGE (phi, entry);
    };

  tree lhs = initial_value (gimple_cond_lhs (cond));
  tree rhs = initial_value (gimple_cond_rhs (cond));
  tree value = fold_binary (gimple_cond_code (cond), boolean_type_node,
			    lhs, rhs);
  if (!value || TREE_CODE (value) != INTEGER_CST)
    return false;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (loop->header, &true_edge, &false_edge);
  edge taken = integer_zerop (value) ? false_edge : true_edge;
  return taken && taken->dest == loop->latch;
}

static bool
transform (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb
	  || loop->num_nodes != 2)
	continue;

      edge entry = loop_entry_edge (loop);
      if (!entry)
	continue;

      /* Refuse whenever opaque state exists inside the hoist region (see
	 region_has_opaque_state_p).  Opacity elsewhere in the function
	 cannot interleave with the hoisted live ranges and is no reason
	 to refuse an otherwise proven loop.  */
      if (region_has_opaque_state_p (loop, entry))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Invariant SFPU immediate hoist refused: function has opaque LREG state\n");
	  continue;
	}

      /* A dedicated preheader receives the loads at its end; any other
	 entry block keeps its own statements and a fresh block is split
	 from the entry edge at transform time.  A dedicated preheader
	 ending in a non-opaque block terminator cannot receive an
	 insertion after that terminator; refuse structurally.  */
      basic_block src = entry->src;
      bool dedicated = single_succ_p (src)
	&& src != ENTRY_BLOCK_PTR_FOR_FN (fn);
      if (dedicated)
	{
	  gimple_stmt_iterator last = gsi_last_nondebug_bb (src);
	  if (!gsi_end_p (last) && stmt_ends_bb_p (gsi_stmt (last)))
	    continue;
	}

      if (!first_iteration_executes_p (loop, entry)
	  || loop_has_barrier_p (loop))
	continue;

      /* SFPLOADI writes an architectural LREG even though its SSA result is
	 local.  Do not speculate it out of a loop that may execute zero times;
	 require a structurally proven first iteration and at least one expected
	 backedge for profitability.  */
      if (expected_loop_iterations_unbounded (loop) < 1)
	continue;

      auto_vec<gcall *> loads;
      basic_block body = loop->latch;
      for (gimple_stmt_iterator gsi = gsi_start_bb (body);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	if (is_a <gcall *> (gsi_stmt (gsi)))
	  {
	    gcall *call = as_a <gcall *> (gsi_stmt (gsi));
	    if (constant_load_p (call, loop))
	      loads.safe_push (call);
	  }

      if (loads.is_empty ())
	continue;

      auto_vec<gcall *> selected = select_pressure_legal_loads (loop, loads);
      if (selected.is_empty ())
	continue;

      /* Never overwrite an explicit user unroll request (loop->unroll is
	 nonzero once "#pragma GCC unroll N" has been recorded during CFG
	 construction); in particular "#pragma GCC unroll 1" must keep its
	 scalar loop.  Only the unroll request defers to the pragma — the
	 invariant hoist below is independent and still proceeds.  */
      if (riscv_tt_opt_replay_hoist > 0
	  && !loop->unroll
	  && short_constant_replay_loop_p (loop, entry))
	{
	  loop->unroll = USHRT_MAX;
	  if (dump_file)
	    fprintf (dump_file,
		     "Requested complete unroll for constant replay loop bb %d\n",
		     bb->index);
	}

      /* Commit: all proofs hold and at least one load will move.  Split
	 a shared entry edge now (and only now) so refusals above remain
	 byte-identical to the flag-off compilation.  split_edge keeps
	 loop membership and any dominance info consistent, and moves the
	 header PHI arguments onto the new edge.  */
      basic_block preheader = dedicated ? src : split_edge (entry);

      for (gcall *call : selected)
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

	  gimple_stmt_iterator from = gsi_for_stmt (call);
	  gsi_move_to_bb_end (&from, preheader);
	  changed = true;
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Hoisted invariant SFPU immediate from loop bb %d to preheader bb %d: ",
		       bb->index, preheader->index);
	      print_gimple_stmt (dump_file, call, 0);
	    }
	}
    }
  return changed;
}

const pass_data pass_data_rvtt_invariant =
{
  GIMPLE_PASS,
  "rvtt_invariant",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_invariant : public gimple_opt_pass
{
public:
  pass_rvtt_invariant (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_invariant, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
      && riscv_tt_opt_invariant_loadi > 0;
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "Invariant SFPU immediate hoist refused on QSR\n");
	return 0;
      }
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_invariant (gcc::context *ctxt)
{
  return new pass_rvtt_invariant (ctxt);
}
