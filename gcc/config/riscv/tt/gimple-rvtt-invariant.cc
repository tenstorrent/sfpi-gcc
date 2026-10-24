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

/* Loop-invariant SFPLOADI hoisting (-mtt-tensix-optimize-invariant-loadi).

   SFPI kernels materialize vector constants where they are used, so an
   SFPLOADI (or a short SFPLOADI sequence for a 32-bit immediate) in a
   loop body re-executes every iteration.  This pass moves such
   materializations to the loop preheader.  It runs before
   pass_rvtt_immvar_expand, while the values are still SSA names, so the
   later pressure, residency and scheduling passes see the hoisted form.
   The approach follows tree-ssa-loop-im.cc (innermost loops first, one
   level at a time, creating the preheader only when something moves),
   but LIM cannot do this itself: these are volatile builtin calls, and
   their effect is an LREG write under a lane mask, not just an SSA value.

   SFPLOADI writes only enabled lanes (SFPLOADI.md), so moving it must
   preserve both when it runs and which lanes it writes.  Each loop must
   have:
     - an entry edge whose preheader can take an insertion; a shared
       entry edge is split only when something is committed;
     - no opaque LREG state in the preheader tail or the loop body
       (rvtt_loop_hoist_region_opaque_p);
     - a proven first iteration and an expected backedge, so the LREG
       write is never speculated out of a loop that may not run;
     - for each candidate, a block that runs on every iteration;
     - the structured-CC-restore proof (see "Structured-CC-restore proof"
       below): if every CC write in the loop is inside balanced plain
       PUSHC/POPC regions, the lane-enable state at depth zero equals the
       preheader's, so a depth-zero hoist writes exactly the same lanes;
       inside a region the writers can only narrow the mask, so the
       preheader writes a superset of the lanes the original position
       wrote (cc-position-widening-unproven if that is not proven);
     - room in the register file (select_pressure_legal_loads): each
       hoist keeps one of the eight LREGs live across the loop, and they
       cannot be spilled.  A CC-carrying loop that will be unrolled
       further refuses (cc-restore-unroll-pressure-unmodeled), since
       unrolling would multiply the live ranges after this pass.

   A hoisted load moves out of its own loop into the enclosing body, where
   the enclosing loop decides again.  A short constant-count replay loop
   with no CC activity may instead be marked for complete unrolling,
   unless the user gave "#pragma GCC unroll".  QSR is not supported.

   With -mtt-tensix-optimize-park-ordering and the late const-residency
   walk enabled, in-region candidates of a CC-restore loop are left to
   that walk (residency-walk-ordering) and depth-zero candidates stay here
   (depth-zero-hoist-dominant); see transform for the details and the
   whole-loop exceptions.  */

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
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-pressure.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"

#include <unordered_map>
#include <unordered_set>

namespace {

/* Return true if INSND is a typed Dst load, store or RWC/face counter
   operation.  These change neither an invariant SFPLOADI value nor the CC
   state, so the barrier walk admits them.  */

static bool
allowed_dst_effect_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpload_lv
    || insnd->id == rvtt_insn_data::sfpstore
    || insnd->id == rvtt_insn_data::ttincrwc
    || insnd->id == rvtt_insn_data::ttdstface;
}

/* Return true if STMT is a structured predicate marker that emits nothing: the
   v_if/v_else/v_endif frame token, or a condition token whose condition
   pass_rvtt_vif has already lowered into explicit statements (it then zeroes
   the condition operand).  These expand to a scalar move of the dependency
   token.  An unlowered condition token still stands for CC writes and does not
   qualify.  */

static bool
inert_predicate_marker_p (const rvtt_insn_data *insnd, gcall *call)
{
  if (insnd->id == rvtt_insn_data::sfpxpred)
    return true;
  return insnd->id == rvtt_insn_data::sfpxcond
	 && gimple_call_num_args (call) == 3
	 && integer_zerop (gimple_call_arg (call, 2));
}

/* Return true if every non-debug use of VALUE is inside LOOP, so hoisting its
   definition does not extend its live range past the loop.  */

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
} /* anonymous namespace */

/* Loop checks shared with the LUT coefficient placement (declared in
   rvtt-macro-ownership.h).  */

/* Reject unrepresented calls, ordinary memory, CC changes, configuration,
   replay ownership, and every other volatile target effect.  Typed Dst
   load/store/counter operations are explicit architectural boundaries but do
   not change an invariant SFPLOADI value or the incoming CC state.  */
bool
rvtt_loop_has_sfpu_barrier_p (class loop *loop)
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
	    if (inert_predicate_marker_p (insnd, call))
	      continue;
	    if (insnd->sets_cc (call)
		|| (insnd->has_side_effects (call)
		    && !allowed_dst_effect_p (insnd)))
	      barrier = true;
	    continue;
	  }

	if (gimple_code (stmt) == GIMPLE_ASM)
	  {
	    /* A raw `.ttinsn' word that decodes as a pure Dst/RWC counter
	       operation (rvtt-raw-boundary.cc) is admitted like the typed
	       counter operations above.  Any other asm is a barrier.  */
	    if (!rvtt_raw_pure_dst_rwc_gimple (stmt))
	      barrier = true;
	    continue;
	  }
	if (is_gimple_call (stmt)
	    || gimple_vuse (stmt) || gimple_vdef (stmt))
	  barrier = true;
      }
  free (body);
  return barrier;
}

/* Return true if STMT is an SFPENCC whose constant operands encode exactly the
   all-lanes enable word (rvtt_macro::sfpencc_all_lanes_word; the RTL
   equivalent is cc_write_all_lanes in rvtt_insn_effects).  Argument 0 is the
   mod1 and argument 1 the imm12, as pass_rvtt_cc emits it.  */

bool
rvtt_all_lanes_encc_p (gimple *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd || insnd->id != rvtt_insn_data::sfpencc)
    return false;
  gcall *call = as_a <gcall *> (stmt);
  if (gimple_call_num_args (call) < 2)
    return false;
  tree mod1 = gimple_call_arg (call, 0);
  tree imm12 = gimple_call_arg (call, 1);
  if (TREE_CODE (mod1) != INTEGER_CST || TREE_CODE (imm12) != INTEGER_CST)
    return false;
  uint32_t word;
  return rvtt_macro::sfpencc_encode (TREE_INT_CST_LOW (imm12),
				     TREE_INT_CST_LOW (mod1), &word)
    && word == rvtt_macro::sfpencc_all_lanes_word ()
    && rvtt_gimple_all_lanes_after_p (stmt);
}

/* Single-block body check for CC-carrying loops (see rvtt-macro-ownership.h).
   It classifies statements as rvtt_loop_has_sfpu_barrier_p does, except that
   CC writers are admitted provided:
   - the body is one basic block (header == latch), so program order is
     execution order;
   - the last CC-writing statement is the all-lanes SFPENCC, so every later
     statement, and the backedge, runs with all lanes enabled.  This says
     nothing about the first iteration's lane state: consumers must peel the
     first iteration and place any lane-sensitive write after the peeled copy's
     trailing SFPENCC.  */

rvtt_cc_canonical_body
rvtt_loop_cc_canonical_body (class loop *loop)
{
  rvtt_cc_canonical_body out = { false, nullptr, "multi-block-body" };
  if (loop->num_nodes != 1 || !loop->latch || loop->header != loop->latch)
    return out;

  basic_block bb = loop->header;
  gimple *first_cc = nullptr;
  gimple *last_cc = nullptr;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (insnd)
	{
	  gcall *call = as_a <gcall *> (stmt);
	  /* Markers emit nothing; they neither limit placement nor count as
	     the tail.  */
	  if (inert_predicate_marker_p (insnd, call))
	    continue;
	  /* SFPPUSHC/SFPPOPC only move state between the flags and the flag
	     stack (SFPPUSHC.md, SFPPOPC.md), and the trailing all-lanes
	     SFPENCC then overwrites the flags, so the next iteration still
	     starts with all lanes enabled; the peel repeats the same stack
	     traffic.  Treat them like CC writers.  */
	  if (insnd->sets_cc (call)
	      || insnd->id == rvtt_insn_data::sfppushc
	      || insnd->id == rvtt_insn_data::sfppopc)
	    {
	      if (!first_cc)
		first_cc = stmt;
	      last_cc = stmt;
	    }
	  else if (insnd->has_side_effects (call)
		   && !allowed_dst_effect_p (insnd))
	    {
	      out.why = insnd->name;	/* volatile-non-dst-effect */
	      return out;
	    }
	  continue;
	}

      /* As in the barrier walk, raw `.ttinsn' words must decode as pure
	 Dst/RWC operations, and other asm, unknown calls and scalar memory
	 accesses refuse.  The remaining assignments can be peeled.  */
      if (gimple_code (stmt) == GIMPLE_ASM)
	{
	  if (!rvtt_raw_pure_dst_rwc_gimple (stmt))
	    {
	      out.why = "opaque-asm";
	      return out;
	    }
	  continue;
	}
      if (is_gimple_call (stmt) || gimple_vuse (stmt) || gimple_vdef (stmt))
	{
	  out.why = "memory-or-unrepresented-call";
	  return out;
	}
      if (!is_gimple_assign (stmt))
	{
	  out.why = "unduplicable-statement";
	  return out;
	}
    }

  if (!last_cc)
    {
      out.why = "no-cc-writer";
      return out;
    }
  if (!rvtt_all_lanes_encc_p (last_cc))
    {
      out.why = "tail-not-all-lanes-encc";
      return out;
    }
  out.proven = true;
  out.first_cc_writer = first_cc;
  out.why = nullptr;
  return out;
}

/* Return true if CALL is a constant materialization that can be hoisted from
   LOOP: the sfpxloadi form (or, if ALLOW_SHORTENED, also the single-issue
   sfploadi form produced by pass_rvtt_immload_shorten) with the canonical
   instruction-buffer operand, constant scalar operands, and every non-debug
   use inside LOOP.  */

/* If CALL is the SFPLOADI_LV tail of the pair emit_loadimm issues for a 32-bit
   constant whose halves are both significant, return the SFPLOADI root, else
   null.  Both halves must have the canonical instruction-buffer operand and
   constant scalars, and the tail must be the root's only use.  */

gcall *
rvtt_chained_loadi_root (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfploadi_lv)
    return nullptr;
  tree link = gimple_call_arg (call, 1);
  if (TREE_CODE (link) != SSA_NAME || !has_single_use (link))
    return nullptr;
  gcall *root = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (link));
  const rvtt_insn_data *rootd = root ? rvtt_get_insn_data (root) : nullptr;
  if (!rootd || rootd->id != rvtt_insn_data::sfploadi
      || !rvtt_canonical_buffer_arg_p (gimple_call_arg (root, 0)))
    return nullptr;
  for (unsigned ix = 1; ix != gimple_call_num_args (root); ++ix)
    if (TREE_CODE (gimple_call_arg (root, ix)) != INTEGER_CST)
      return nullptr;
  return root;
}

bool
rvtt_invariant_constant_load_p (gcall *call, class loop *loop,
				bool allow_shortened)
{
  /* The invariant pass runs before immediate shortening and must not accept
     direct sfploadi calls; later users (LUT coefficient placement) also accept
     the shortened form, whose operand layout is the same.  */
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  gcall *root = nullptr;
  if (allow_shortened && insnd
      && insnd->id == rvtt_insn_data::sfploadi_lv)
    root = rvtt_chained_loadi_root (call);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpxloadi
	  && !(allow_shortened && insnd->id == rvtt_insn_data::sfploadi)
	  && !root))
    return false;

  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || !rvtt_canonical_buffer_arg_p (gimple_call_arg (call, 0))
      || !all_uses_in_loop_p (lhs, loop))
    return false;

  /* The root of a pair is never a candidate by itself; the pair is handled
     through its tail, and only if the tail is the root's sole use.  Moving
     just the root would split the materialization across the loop
     boundary.  */
  if (!root && insnd->id == rvtt_insn_data::sfploadi)
    {
      imm_use_iterator iter;
      gimple *use_stmt;
      FOR_EACH_IMM_USE_STMT (use_stmt, iter, lhs)
	if (gcall *tail = dyn_cast <gcall *> (use_stmt))
	  {
	    const rvtt_insn_data *taild = rvtt_get_insn_data (tail);
	    if (taild && taild->id == rvtt_insn_data::sfploadi_lv
		&& gimple_call_arg (tail, 1) == lhs)
	      return false;
	  }
    }

  for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
    {
      /* The tail's vector operand is the root; every other operand must be a
	 scalar constant.  */
      if (root && ix == 1)
	continue;
    if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
      return false;
    }
  return true;
}

/* The loop-scoped candidate-set pressure proof this pass uses lives in
   the unified pressure engine, tt/rvtt-pressure.cc
   (rvtt_pressure_loop_legal_p and the incremental rvtt_loop_pressure
   profile).  */

/* Return true if the first header test of LOOP is proven to enter the body, so
   that a hoisted LREG write is never speculated out of a zero-trip loop.  This
   avoids requesting loop normalization, which would change the CFG of
   functions that do not qualify.  */
bool
rvtt_loop_first_iteration_executes_p (class loop *loop, edge entry)
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
  return taken && taken->dest != loop->header
    && flow_bb_inside_loop_p (loop, taken->dest);
}

/* Return true if BB runs on every iteration of LOOP that enters the body: BB
   dominates the latch, and every exit leaves either from the header test or
   from a block BB dominates.  */
bool
rvtt_stmt_executes_every_entered_iteration_p (class loop *loop,
					      basic_block bb)
{
  /* With AVOID_CFG_MODIFICATIONS a multi-latch loop has a null latch; the
     check below needs a single latch.  */
  if (!loop->latch)
    return false;

  if (!dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
    return false;

  basic_block *body = get_loop_body (loop);
  bool ok = true;
  for (unsigned ix = 0; ix != loop->num_nodes && ok; ++ix)
    {
      basic_block src = body[ix];
      if (src == loop->header || dominated_by_p (CDI_DOMINATORS, src, bb))
	continue;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, src->succs)
	if (!flow_bb_inside_loop_p (loop, e->dest))
	  {
	    ok = false;
	    break;
	  }
    }
  free (body);
  return ok;
}

/* Return the number of SFPLOADI issues needed for CALL's constant after
   immediate shortening.  When pressure prevents hoisting every invariant,
   two-issue constants are preferred, since one-issue values are cheap to leave
   in the loop.  An already shortened sfploadi costs one issue.  */
unsigned
rvtt_sfpxloadi_materialization_cost (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (insnd && insnd->id == rvtt_insn_data::sfploadi)
    return 1;
  if (insnd && insnd->id == rvtt_insn_data::sfploadi_lv
      && rvtt_chained_loadi_root (call))
    return 2;

  uint32_t value = TREE_INT_CST_LOW (gimple_call_arg (call, 1));
  /* See loadi_issue_words in rvtt-delivery-cost-core.h.  */
  unsigned issues = rvtt_dcost_loadi_issue_words (value);
  return issues;
}

namespace {

/* Cost of a candidate for the greedy selection below.  */
static unsigned
materialization_cost (gcall *call)
{
  return rvtt_sfpxloadi_materialization_cost (call);
}

/* Select the most expensive invariant materializations that fit the LREG
   pressure bound, so that one oversized live range does not keep every
   constant inside the loop.  The pressure check is redone after each addition,
   and equal-cost candidates keep source order.  */
static auto_vec<gcall *>
select_pressure_legal_loads (class loop *loop, auto_vec<gcall *> &loads,
			     bool cc_transients)
{
  std::stable_sort (loads.begin (), loads.end (),
		    [] (gcall *a, gcall *b)
		    {
		      return materialization_cost (a)
			     > materialization_cost (b);
		    });

  /* Compute one base profile and query it incrementally for each candidate
     (checked against the full computation under flag_checking).  */
  rvtt_loop_pressure profile (loop, cc_transients);
  auto_vec<gcall *> selected;
  for (gcall *call : loads)
    {
      selected.safe_push (call);
      if (!profile.legal_with (selected))
	{
	  selected.pop ();
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Invariant SFPU immediate left in loop"
		       " by LREG pressure: ");
	      print_gimple_stmt (dump_file, call, 0);
	    }
	}
    }
  return selected;
}

/* Return the header phi of LOOP from which X is derived by a chain of
   non-memory assignments whose other operands are constants, or null.  This is
   like the niter brute-force chain discovery, but does not need canonical
   preheaders.  */
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

/* Return the value of X in a header evaluation where the chain phi (see
   constant_chain_phi) holds the constant BASE; a null X is the phi itself.
   Return NULL_TREE if the value does not fold to a constant.  */
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

/* When replay formation is also enabled, ask the complete unroller to unroll a
   short loop with an exact constant trip count, so the replay pass can turn
   the identical copies into launches without scalar loop control.  The trip
   count is found by evaluating the header test from the ENTRY edge with
   constant folding; niter analysis needs canonical preheaders, which this pass
   does not create.  The size bound is structural because replay-buffer
   eligibility is only decided after allocation.  */
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

  /* Evaluate the header test iteration by iteration to count the
     backedges.  */
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

/* ------------------- Structured-CC-restore proof -------------------

   rvtt_loop_has_sfpu_barrier_p refuses a loop with any CC writer, because
   SFPLOADI writes only enabled lanes (SFPLOADI.md) and hoisting it from a
   position whose mask differs from the preheader's changes which lanes it
   writes.  That restriction can be lifted when the loop restores its CC
   state.

   The lane-enable state is the pair {LaneFlags,
   UseLaneFlagsForLaneEnable} (VectorUnit.md IsLaneEnabled); SFPPUSHC
   mod 0 pushes exactly that pair and SFPPOPC mod 0 pops it back
   (SFPPUSHC.md, SFPPOPC.md).  So if, in the loop body,
     (a) every SFPPUSHC is the plain push (other mods change the saved
	 entry),
     (b) every SFPPOPC is the plain pop (the peek modes rewrite the flags
	 without popping),
     (c) the push depth agrees at every join, never underflows and is
	 zero on the backedge, and
     (d) no other CC writer runs at depth zero,
   then the state at every depth-zero position equals the loop-entry
   state, which is the state at the preheader, and a depth-zero hoist
   writes the same lanes.

   Inside a region (depth > 0) the hoist is still valid if every
   in-region CC writer can only narrow the enable set relative to the
   region entry: SFPSETCC and the CC-writing SFPIADD forms update flags
   only in enabled lanes (SFPSETCC.md, SFPIADD.md); SFPCOMPC computes
   Top.LaneFlags && !LaneFlags against the region-entry save
   (SFPCOMPC.md); and the structured condition markers lower to those
   (gimple-rvtt-pred.cc).  The hoisted load then writes a superset of the
   lanes.  The extra lanes of its fresh SSA value were never written by
   the original program, and every consumer's write is itself
   lane-predicated, so they are not observed (a merge consumer,
   sfpassign_lv, lowers to the predicated SFPMOV merge).  SFPENCC can
   widen the set (SFPENCC.md): inside a region it keeps the restore
   proof, since the POPC discards it, but disables in-region candidates.

   The hoisted definition gets its own register, and the loop pressure
   proof (rvtt-pressure.cc) checks that one is free.

   The sfpi v_endif emits its POPCs through a small counted loop (the CC
   object's destructor), which appears here as a subloop whose body is
   the POPC block.  Such a subloop is summarized by proving its trip
   count, as for the replay-unroll request, and counting POPCs * trips;
   any other subloop with CC activity refuses.  */

struct cc_restore_analysis
{
  bool has_cc = false;		/* any CC machinery in the loop */
  bool narrow_ok = true;	/* all in-region modifiers only narrow  */
  const char *why = nullptr;	/* named refusal when the proof fails */
  /* Push depth on entry to each top-level body block.  */
  std::unordered_map<basic_block, int> entry_depth;
  /* Exact POPC executions per full execution of a summarized subloop,
     and its single in-loop continuation block.  */
  std::unordered_map<class loop *, int> sub_pops;
  std::unordered_map<class loop *, basic_block> sub_exit;
};

/* Return true if CALL is a CC writer that can only narrow the enable set
   relative to the region entry (see the comment above).  Any other CC writer
   disables in-region candidates.  */
static bool
cc_narrowing_modifier_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpsetcc:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfpxcmp:
    /* The CC-writing iadd builtin is only admitted below, under
       -mtt-tensix-optimize-cc-region-general.  */
      return true;

    /* Under -mtt-tensix-optimize-cc-region-general also admit the remaining
       typed CC writers, each of which updates flags only in enabled lanes
       (in the reference simulator they decide flags only inside the
       enable-masked lane loop).  SFPENCC, which sets the enable state
       outright, stays excluded.  */
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
      return riscv_tt_opt_cc_region_general > 0;

    default:
      return false;
    }
}

/* Return the constant integer mod operand of CALL, or -1.  */
static long
const_mod_arg (const rvtt_insn_data *insnd, gcall *call)
{
  if (!insnd->has_mod ()
      || (unsigned) insnd->mod_arg () >= gimple_call_num_args (call))
    return -1;
  tree mod = gimple_call_arg (call, insnd->mod_arg ());
  return TREE_CODE (mod) == INTEGER_CST ? (long) TREE_INT_CST_LOW (mod) : -1;
}

/* Classify STMT of the loop body for the restore proof, updating *DEPTH.  A
   null DEPTH means no CC activity is allowed (subloop bodies other than the
   destructor-pop shape).  On refusal set A.why and return false.  */
static bool
cc_restore_classify_stmt (gimple *stmt, int *depth, cc_restore_analysis &a)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_COND
      || gimple_code (stmt) == GIMPLE_GOTO)
    return true;

  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (insnd)
    {
      gcall *call = as_a <gcall *> (stmt);
      switch (insnd->id)
	{
	case rvtt_insn_data::sfppushc:
	  a.has_cc = true;
	  if (!depth)
	    a.why = "cc-restore-subloop-shape";
	  else if (const_mod_arg (insnd, call) != SFPPUSHCC_MOD1_PUSH)
	    /* A non-plain push changes the saved entry, so the POPC would not
	       restore the region-entry state.  */
	    a.why = "cc-restore-pushc-mod";
	  else if (++*depth > 8)
	    /* Architectural stack capacity (SFPPUSHC.md).  */
	    a.why = "cc-restore-depth-overflow";
	  return !a.why;

	case rvtt_insn_data::sfppopc:
	  a.has_cc = true;
	  if (!depth)
	    a.why = "cc-restore-subloop-shape";
	  else if (const_mod_arg (insnd, call) != SFPPOPCC_MOD1_POP)
	    /* Peek modes rewrite the live flags without popping.  */
	    a.why = "cc-restore-popc-mod";
	  else if (--*depth < 0)
	    /* Pops a save pushed outside the loop.  */
	    a.why = "cc-restore-unbalanced";
	  return !a.why;

	case rvtt_insn_data::sfpxpred:
	case rvtt_insn_data::sfpxlogic:
	case rvtt_insn_data::sfpxcond:
	  /* Structured condition markers expand to CC writes confined to the
	     enclosing region, so they are refused outside one.  sfpxcond is
	     always such a marker (expand_vif in gimple-rvtt-pred.cc rejects
	     one outside a predication region).  */
	  a.has_cc = true;
	  if (!depth || *depth == 0)
	    a.why = "cc-restore-marker-ambient";
	  return !a.why;

	default:
	  if (insnd->sets_cc (call))
	    {
	      a.has_cc = true;
	      if (!depth)
		a.why = "cc-restore-subloop-shape";
	      else if (*depth == 0)
		/* A flag write at depth zero changes the state seen by the
		   next iteration.  */
		a.why = "cc-restore-ambient-cc-write";
	      else if (!cc_narrowing_modifier_p (insnd))
		{
		  /* The restore still holds, but in-region candidates are
		     disabled.  */
		  a.narrow_ok = false;
		  /* Name the writer in the refusal.  */
		  if (dump_file)
		    fprintf (dump_file,
			     "cc-restore: in-region modifier %s outside "
			     "the audited narrowing set\n", insnd->name);
		}
	      return !a.why;
	    }
	  if (insnd->has_side_effects (call) && !allowed_dst_effect_p (insnd))
	    {
	      a.why = insnd->name;	/* volatile-non-dst-effect */
	      return false;
	    }
	  return true;
	}
    }

  if (gimple_code (stmt) == GIMPLE_ASM)
    {
      if (!rvtt_raw_pure_dst_rwc_gimple (stmt))
	{
	  a.why = "opaque-asm";
	  return false;
	}
      return true;
    }
  if (is_gimple_call (stmt) || gimple_vuse (stmt) || gimple_vdef (stmt))
    {
      a.why = "memory-or-unrepresented-call";
      return false;
    }
  return true;
}

/* Return the exact number of times the latch of the two-block subloop S
   executes, found by evaluating its header test as
   short_constant_replay_loop_p does, or -1.  */
static int
destructor_pop_trip_count (class loop *s, edge entry)
{
  constexpr int MAX_POP_TRIPS = 8;	/* flag stack capacity */

  gimple_stmt_iterator last = gsi_last_bb (s->header);
  gcond *cond = gsi_end_p (last)
    ? nullptr : dyn_cast <gcond *> (gsi_stmt (last));
  edge latch_e = s->latch ? find_edge (s->latch, s->header) : nullptr;
  if (!cond || !entry || !latch_e)
    return -1;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (s->header, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return -1;

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
      gphi *phi = constant_chain_phi (s, op[j]);
      if (!phi)
	return -1;
      value[j] = PHI_ARG_DEF_FROM_EDGE (phi, entry);
      next[j] = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
      if (!is_gimple_min_invariant (value[j]))
	return -1;
      if (TREE_CODE (next[j]) == SSA_NAME
	  && constant_chain_phi (s, next[j]) != phi)
	return -1;
    }

  int trips = -1;
  fold_defer_overflow_warnings ();
  for (int taken_count = 0; taken_count <= MAX_POP_TRIPS; taken_count++)
    {
      tree lhs = constant_chain_value (op[0], value[0]);
      tree rhs = constant_chain_value (op[1], value[1]);
      tree test = lhs && rhs
	? fold_binary (gimple_cond_code (cond), boolean_type_node, lhs, rhs)
	: NULL_TREE;
      if (!test || TREE_CODE (test) != INTEGER_CST)
	break;

      edge taken = integer_zerop (test) ? false_edge : true_edge;
      if (taken->dest != s->latch)
	{
	  trips = taken_count;
	  break;
	}

      value[0] = constant_chain_value (next[0], value[0]);
      value[1] = constant_chain_value (next[1], value[1]);
      if (!value[0] || !value[1])
	break;
    }
  fold_undefer_and_ignore_overflow_warnings ();
  return trips;
}

/* Summarize the direct subloop S of the loop being analyzed: either it has no
   CC activity, or it is the v_endif destructor-pop shape (a two-block counted
   loop whose latch does only plain POPCs and scalar work) with a proven trip
   count.  Also check every statement for the ordinary barriers.  On refusal
   set A.why and return false.  */
static bool
summarize_cc_subloop (class loop *s, cc_restore_analysis &a)
{
  /* First pass: any CC machinery in S?  */
  bool s_has_cc = false;
  basic_block *body = get_loop_body (s);
  for (unsigned ix = 0; ix != s->num_nodes && !s_has_cc; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi) && !s_has_cc; gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	if (!insnd)
	  continue;
	switch (insnd->id)
	  {
	  case rvtt_insn_data::sfppushc:
	  case rvtt_insn_data::sfppopc:
	  case rvtt_insn_data::sfpxpred:
	  case rvtt_insn_data::sfpxcond:
	  case rvtt_insn_data::sfpxlogic:
	    s_has_cc = true;
	    break;
	  default:
	    if (insnd->sets_cc (as_a <gcall *> (stmt)))
	      s_has_cc = true;
	    break;
	  }
      }

  if (!s_has_cc)
    {
      /* Transparent: classify for barrier classes only.  */
      for (unsigned ix = 0; ix != s->num_nodes; ++ix)
	for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	     !gsi_end_p (gsi); gsi_next (&gsi))
	  if (!cc_restore_classify_stmt (gsi_stmt (gsi), nullptr, a))
	    {
	      free (body);
	      return false;
	    }
      free (body);
      a.sub_pops[s] = 0;
      a.sub_exit[s] = nullptr;	/* all exits transparent */
      return true;
    }
  free (body);

  /* A subloop with CC activity must be the destructor-pop shape: two blocks,
     the header holding only the counter test and the latch the plain POPCs and
     scalar work.  */
  a.has_cc = true;
  edge s_entry = rvtt_loop_entry_edge (s);
  auto_vec<edge> exits = get_loop_exit_edges (s);
  if (s->num_nodes != 2 || !s->latch || !s_entry || exits.length () != 1
      || !flow_bb_inside_loop_p (loop_outer (s), exits[0]->dest))
    {
      a.why = "cc-restore-subloop-shape";
      return false;
    }

  int pops_per_trip = 0;
  for (gimple_stmt_iterator gsi = gsi_start_bb (s->header); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (insnd)
	{
	  /* No CC activity in the header, which runs once more than the
	     latch.  */
	  a.why = "cc-restore-subloop-shape";
	  return false;
	}
      if (!cc_restore_classify_stmt (stmt, nullptr, a))
	return false;
    }
  for (gimple_stmt_iterator gsi = gsi_start_bb (s->latch); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (insnd)
	{
	  if (insnd->id != rvtt_insn_data::sfppopc
	      || const_mod_arg (insnd, as_a <gcall *> (stmt))
		 != SFPPOPCC_MOD1_POP)
	    {
	      a.why = "cc-restore-subloop-shape";
	      return false;
	    }
	  ++pops_per_trip;
	  continue;
	}
      if (!cc_restore_classify_stmt (stmt, nullptr, a))
	return false;
    }

  int trips = pops_per_trip ? destructor_pop_trip_count (s, s_entry) : 0;
  if (trips < 0)
    {
      a.why = "cc-restore-pop-trips-unproven";
      return false;
    }
  a.sub_pops[s] = pops_per_trip * trips;
  a.sub_exit[s] = exits[0]->dest;
  return true;
}

/* The restore proof for LOOP: propagate push depth over the top-level body
   blocks (summarizing subloops), requiring agreement at joins and zero on the
   backedge, and fill A.  Return false with A.why set if the proof fails or a
   barrier other than CC is present.  */
static bool
analyze_cc_restore (class loop *loop, cc_restore_analysis &a)
{
  for (class loop *s = loop->inner; s; s = s->next)
    if (!summarize_cc_subloop (s, a))
      return false;

  std::vector<std::pair<basic_block, int>> work;
  auto visit = [&a, &work] (basic_block bb, int d) -> bool
    {
      auto it = a.entry_depth.find (bb);
      if (it == a.entry_depth.end ())
	{
	  a.entry_depth.emplace (bb, d);
	  work.emplace_back (bb, d);
	  return true;
	}
      if (it->second != d)
	{
	  a.why = "cc-restore-unstructured";
	  return false;
	}
      return true;
    };

  if (!visit (loop->header, 0))
    return false;
  while (!work.empty ())
    {
      basic_block bb = work.back ().first;
      int d = work.back ().second;
      work.pop_back ();

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	if (!cc_restore_classify_stmt (gsi_stmt (gsi), &d, a))
	  return false;

      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  basic_block dest = e->dest;
	  if (!flow_bb_inside_loop_p (loop, dest))
	    continue;		/* loop exit */
	  if (dest == loop->header)
	    {
	      if (d != 0)
		{
		  a.why = "cc-restore-backedge-depth";
		  return false;
		}
	      continue;
	    }
	  if (dest->loop_father != loop)
	    {
	      /* Entering a summarized subloop.  */
	      class loop *s = dest->loop_father;
	      while (loop_outer (s) != loop)
		s = loop_outer (s);
	      if (dest != s->header)
		{
		  a.why = "cc-restore-unstructured";
		  return false;
		}
	      /* Check the depth at the subloop entry, apply its pops and
		 continue at its single exit; a subloop without CC activity
		 passes the depth unchanged to every exit.  */
	      auto sit = a.entry_depth.find (dest);
	      if (sit != a.entry_depth.end ())
		{
		  if (sit->second != d)
		    {
		      a.why = "cc-restore-unstructured";
		      return false;
		    }
		  continue;	/* already summarized and propagated */
		}
	      a.entry_depth.emplace (dest, d);
	      int pops = a.sub_pops.find (s)->second;
	      basic_block cont = a.sub_exit.find (s)->second;
	      if (pops > d)
		{
		  a.why = "cc-restore-unbalanced";
		  return false;
		}
	      if (cont)
		{
		  if (cont == loop->header)
		    {
		      if (d - pops != 0)
			{
			  a.why = "cc-restore-backedge-depth";
			  return false;
			}
		    }
		  else if (cont->loop_father != loop)
		    {
		      /* A subloop exiting directly into another subloop's
			 header is not handled.  */
		      a.why = "cc-restore-unstructured";
		      return false;
		    }
		  else if (!visit (cont, d - pops))
		    return false;
		}
	      else
		{
		  auto_vec<edge> sub_exits = get_loop_exit_edges (s);
		  for (edge xe : sub_exits)
		    {
		      if (!flow_bb_inside_loop_p (loop, xe->dest))
			continue;
		      if (xe->dest == loop->header)
			{
			  if (d != 0)
			    {
			      a.why = "cc-restore-backedge-depth";
			      return false;
			    }
			}
		      else if (xe->dest->loop_father != loop)
			{
			  a.why = "cc-restore-unstructured";
			  return false;
			}
		      else if (!visit (xe->dest, d))
			return false;
		    }
		}
	      continue;
	    }
	  if (!visit (dest, d))
	    return false;
	}
    }
  return true;
}

/* Return the push depth at CALL: its block's entry depth plus the pushes and
   pops before it in the block.  */
static int
cc_depth_at_stmt (const cc_restore_analysis &a, gcall *call)
{
  basic_block bb = gimple_bb (call);
  auto it = a.entry_depth.find (bb);
  gcc_assert (it != a.entry_depth.end ());
  int d = it->second;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == call)
	return d;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	continue;
      if (insnd->id == rvtt_insn_data::sfppushc)
	++d;
      else if (insnd->id == rvtt_insn_data::sfppopc)
	--d;
    }
  gcc_unreachable ();
}

/* Hoist invariant loads in FN, innermost loops first, as described at the top
   of the file.  Return true if the IL changed.  */

static bool
transform (function *fn)
{
  bool changed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  /* Innermost first, so a hoisted load is reconsidered by the enclosing
     loop.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      basic_block bb = loop->header;

      edge entry = rvtt_loop_entry_edge (loop);
      if (!entry)
	continue;

      /* Refuse if the preheader tail or the loop body has opaque LREG state;
	 opaque state elsewhere does not overlap the hoisted live ranges.  */
      if (rvtt_loop_hoist_region_opaque_p (loop, entry))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Invariant SFPU immediate hoist refused:"
		     " function has opaque LREG state\n");
	  continue;
	}

      /* Nothing can be inserted after a preheader's control statement.  */
      if (rvtt_preheader_insertion_blocked_p (entry))
	continue;

      if (!rvtt_loop_first_iteration_executes_p (loop, entry))
	continue;

      /* Barrier check, with CC writers handled by the structured-CC-restore
	 proof above.  */
      cc_restore_analysis cc;
      if (!analyze_cc_restore (loop, cc))
	{
	  /* The loop's header index distinguishes refusals of several loops in
	     the dump.  */
	  rvtt_refuse_by_name (cc.why ? cc.why : "sfpu-barrier", dump_file,
			       "Invariant SFPU immediate hoist refused:"
			       " %s (loop bb %d)\n",
			       cc.why ? cc.why : "sfpu-barrier",
			       loop->header->index);
	  continue;
	}

      /* Do not speculate the LREG write out of a loop that may run zero times,
	 and require an expected backedge for profitability.  */
      if (expected_loop_iterations_unbounded (loop) < 1)
	continue;

      /* Collect candidates from the loop's own body blocks (a load still in a
	 subloop was refused there) whose block runs on every iteration.  */
      auto_vec<gcall *> loads;
      basic_block *body_blocks = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block body = body_blocks[ix];
	  if (body->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, body))
	    continue;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (body);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    if (is_a <gcall *> (gsi_stmt (gsi)))
	      {
		gcall *call = as_a <gcall *> (gsi_stmt (gsi));
		if (!rvtt_invariant_constant_load_p (call, loop))
		  continue;
		/* A candidate inside a CC region needs every in-region CC
		   writer in the loop to be narrowing (see the restore proof
		   above).  */
		if (cc_depth_at_stmt (cc, call) > 0 && !cc.narrow_ok)
		  {
		    if (dump_file)
		      {
			rvtt_refuse (RVTT_REF_CC_POSITION_WIDENING_UNPROVEN,
				     dump_file,
				     "Invariant SFPU immediate left in loop:"
				     " cc-position-widening-unproven: ");
			print_gimple_stmt (dump_file, call, 0);
		      }
		    continue;
		  }
		loads.safe_push (call);
	      }
	}
      free (body_blocks);

      if (loads.is_empty ())
	continue;

      /* An explicit unroll request multiplies live ranges after this pass;
	 the single-body pressure proof does not model that overlap.  */
      if (cc.has_cc && loop->unroll > 1)
	{
	  rvtt_refuse (RVTT_REF_CC_RESTORE_UNROLL_PRESSURE_UNMODELED, dump_file,
		       "Invariant SFPU immediate hoist refused:"
		       " cc-restore-unroll-pressure-unmodeled\n");
	  continue;
	}

      auto_vec<gcall *> selected
	= select_pressure_legal_loads (loop, loads, cc.has_cc);
      if (selected.is_empty ())
	continue;

      /* At -O3 the complete unroller can turn a short, exact-trip CC loop into
	 straight-line code after this pass.  If the single-body model already
	 rejects some candidates, the accepted subset is at the pressure limit
	 and may not fit after unrolling, so refuse.  */
      if (optimize >= 3 && cc.has_cc
	  && selected.length () != loads.length ()
	  && short_constant_replay_loop_p (loop, entry))
	{
	  rvtt_refuse (RVTT_REF_CC_RESTORE_UNROLL_PRESSURE_UNMODELED, dump_file,
		       "Invariant SFPU immediate hoist refused:"
		       " cc-restore-unroll-pressure-unmodeled"
		       " (pressure-limited short constant trip count)\n");
	  continue;
	}

      /* Commit: at least one load will move.  Only now split a shared
	 entry edge, so that refusals leave the function unchanged.  */
      basic_block preheader = rvtt_commit_hoist_preheader (entry);

      /* Never override "#pragma GCC unroll N" (loop->unroll is then nonzero);
	 "#pragma GCC unroll 1" must keep its loop.  The hoist below still
	 proceeds.  */
      /* Request unrolling only for loops with no CC activity.  */
      if (riscv_tt_opt_replay_hoist > 0
	  && !cc.has_cc
	  && !loop->unroll
	  && short_constant_replay_loop_p (loop, entry))
	{
	  loop->unroll = USHRT_MAX;
	  if (dump_file)
	    fprintf (dump_file,
		     "Requested complete unroll for constant replay"
		     " loop bb %d\n",
		     bb->index);
	}

      for (gcall *call : selected)
	{
	  /* A load hoisted from an inner loop earlier in this execution has
	     virtual operands that are not yet renamed (bare .MEM), with no
	     uses to unlink or name to release.  */
	  if (tree vdef = gimple_vdef (call))
	    {
	      if (TREE_CODE (vdef) == SSA_NAME)
		{
		  unlink_stmt_vdef (call);
		  release_ssa_name (vdef);
		}
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
		       "Hoisted invariant SFPU immediate from loop bb %d"
		       " to preheader bb %d: ",
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
  OPTGROUP_OTHER,
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
	  fprintf (dump_file,
		   "Invariant SFPU immediate hoist refused on QSR\n");
	return 0;
      }
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the invariant-loadi pass.  rvtt-passes.def places it before
   pass_rvtt_immvar_expand.  */

gimple_opt_pass *
make_pass_rvtt_invariant (gcc::context *ctxt)
{
  return new pass_rvtt_invariant (ctxt);
}
