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
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"

#include <unordered_map>
#include <unordered_set>

namespace {

static bool
allowed_dst_effect_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpload_lv
    || insnd->id == rvtt_insn_data::sfpstore
    || insnd->id == rvtt_insn_data::ttincrwc
    || insnd->id == rvtt_insn_data::ttdstface;
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

} // anonymous namespace

/* Shared loop invariant-materialization proofs (declared in
   rvtt-macro-ownership.h): the invariant-loadi pass below and the LUT
   selection's coefficient placement consume the same discipline.  */

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
	    if (insnd->sets_cc (call)
		|| (insnd->has_side_effects (call)
		    && !allowed_dst_effect_p (insnd)))
	      barrier = true;
	    continue;
	  }

	if (gimple_code (stmt) == GIMPLE_ASM)
	  {
	    /* Raw `.ttinsn' constant words: the audited architectural
	       decode (rvtt-raw-boundary.cc) proves the pure Dst/RWC
	       counter class -- the same explicit architectural boundary
	       as the typed Dst counter operations admitted above, and
	       equally unable to change an invariant SFPLOADI value or
	       the incoming CC state.  Every other asm is a barrier.  */
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

/* The typed all-lanes SFPENCC: both operands constant and the encoded
   word EXACTLY the capability table's architectural all-lanes enable
   (rvtt_macro::sfpencc_all_lanes_word, the single derivation every
   lane-state proof shares -- the RTL twin is rvtt_insn_effects's
   cc_write_all_lanes).  Operand roles follow the builtin's emission:
   pass_rvtt_cc builds the canonical call as
   sfpencc (SFPENCC_MOD1_EI_RI, SFPENCC_IMM12_BOTH), i.e. argument 0 is
   the encoded mod1 and argument 1 the encoded imm12
   (gimple-rvtt-cc.cc; the rvtt_sfpencc template prints "%1, %0" for
   assembler "SFPENCC imm12, mod1").  Any other CC writer, non-constant
   operand, or non-all-lanes word refuses.  */

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
    && word == rvtt_macro::sfpencc_all_lanes_word ();
}

/* CC-canonical single-block body proof (contract in
   rvtt-macro-ownership.h).  The walk mirrors
   rvtt_loop_has_sfpu_barrier_p statement class by statement class; the
   ONLY admitted difference is CC writers, and those only under the
   linear-path canonical-tail discipline:

   - the body is one basic block (header == latch), so program order is
     the unique execution order and "before"/"after" are line facts;
   - the LAST CC-writing statement is the all-lanes SFPENCC (word-exact
     against the capability table); every statement after it therefore
     executes -- and the loop backedge is taken -- in the architectural
     all-lanes state (craq-sim TENSIX_EXECUTE_SFPENCC writes cc/cc_en
     from the immediate; nothing after the last CC writer changes
     them);
   - everything else that would be a barrier still is: opaque
     statements, unrepresented calls, memory-touching scalar code, and
     volatile target effects outside the typed Dst load/store/counter
     class all refuse.

   The proof deliberately says nothing about the FIRST iteration's
   lane state (function-entry ambient): consumers must reproduce
   iteration one exactly (peel) and place any lane-sensitive write
   after the peeled copy's trailing SFPENCC.  */

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
	  /* SFPPUSHC/SFPPOPC are CC-stack machinery (a nested v_if
	     region the lowering kept): PUSHC copies the live flags to
	     the stack, POPC restores them from it (craq-sim
	     TENSIX_EXECUTE_SFPPUSHC/SFPPOPC; SFPPUSHC.md/SFPPOPC.md).
	     Both only move state between the flags and the flag stack
	     -- and the body's trailing all-lanes SFPENCC then
	     OVERWRITES cc/cc_en from its immediates, so the mask
	     entering the next iteration is the architectural all-lanes
	     state regardless of any stack traffic before it.  The
	     stack-depth side effect itself is reproduced exactly by
	     the peel (the copied iteration performs the identical
	     pushes and pops).  They therefore classify exactly like CC
	     writers: admitted, position-limiting for candidates, and
	     required to precede the canonical tail.  */
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

      /* Same classes as the barrier walk: raw `.ttinsn' words are
	 admitted only through the audited pure-Dst/RWC decode; any
	 other assembly, unrepresented call, or memory-touching scalar
	 statement refuses.  What remains -- pure scalar/vector
	 assignments -- is exactly what a first-iteration peel can
	 duplicate.  */
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

bool
rvtt_invariant_constant_load_p (gcall *call, class loop *loop,
				bool allow_shortened)
{
  /* The early invariant pass runs before immediate shortening and sees
     only the canonical sfpxloadi form; consumers running after
     pass_rvtt_immload_shorten (LUT coefficient placement) opt in to
     the single-issue shortened form, whose operand layout is
     identical.  The early pass must not opt in: admitting direct
     sfploadi builtin calls there would change its established
     decisions.  */
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpxloadi
	  && !(allow_shortened && insnd->id == rvtt_insn_data::sfploadi)))
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

/* The loop-scoped candidate-set pressure proof this pass (and the
   crossloop, crosscall and LUT-placement consumers) uses lives in the
   unified pressure engine, tt/rvtt-pressure.cc
   (rvtt_pressure_loop_legal_p and the incremental rvtt_loop_pressure
   profile; FABLE_GOES_BURR.md item #10).  */

/* Prove that the loop's first header test enters the loop body.  This
   avoids speculating an architectural LREG write out of a zero-trip loop,
   without requesting loop normalization (which could perturb an ineligible
   function).  */
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

/* A hoisted load must not be speculated: its block must provably execute
   on every iteration that enters the loop body.  BB must dominate the
   latch, and every loop exit must leave either from the header test
   (before any body work of that iteration) or from a block BB dominates
   (after the load has executed).  Pure CFG dominance structure; no
   statement content is examined.  */
bool
rvtt_stmt_executes_every_entered_iteration_p (class loop *loop,
					      basic_block bb)
{
  /* Callers initialize loops with AVOID_CFG_MODIFICATIONS, which keeps
     multi-latch loops as-is with loop->latch == NULL rather than
     canonicalizing them.  Without a unique latch there is no single block
     that ends every iteration, so the dominance proof below has no anchor
     (and dominated_by_p on a NULL block is undefined); refuse, mirroring
     the NULL-latch check in short_constant_replay_loop_p.  */
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

/* Estimate the number of SFPLOADI issues needed to materialize CALL's
   constant after the later immediate-shortening passes run.  Prefer keeping
   two-issue constants live when pressure prevents hoisting every invariant;
   one-issue values remain cheap to rematerialize in the loop.  This models
   only the target's immediate encodings.  It deliberately does not recognize
   particular values or source patterns.  A load already shortened to the
   single-issue sfploadi form (consumers running after
   pass_rvtt_immload_shorten) costs one issue by construction.  */
unsigned
rvtt_sfpxloadi_materialization_cost (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (insnd && insnd->id == rvtt_insn_data::sfploadi)
    return 1;

  uint32_t value = TREE_INT_CST_LOW (gimple_call_arg (call, 1));
  /* The one value-classification spelling (rvtt-delivery-cost-core.h
     loadi_issue_words; FABLE_GOES_BURR #12 -- this function's prior
     inline spelling and the macro-planner's config_word_loadi_issues
     were proven equivalent term-by-term at migration: the halfword
     cases match set-for-set and both FLOATA exponent windows are
     [113, 143)).  */
  unsigned issues = rvtt_dcost_loadi_issue_words (value);
  /* One-pin recompute-assert of the migrated inline spelling
     (item #12 discipline; delete next pin).  */
  if (flag_checking)
    {
      unsigned upper = value >> 16;
      unsigned lower = value & 0xffff;
      unsigned old;
      if (!lower || !upper || (upper == 0xffff && (lower >> 15)))
	old = 1;
      else
	{
	  /* A full value whose low thirteen bits are zero and whose
	     exponent fits binary16 can use the single-issue FLOATA
	     encoding.  */
	  unsigned exponent = (value >> 23) & 0xff;
	  old = !(value & 0x1fff)
	    && exponent > 127 - 15 && exponent < (127 - 15) + 31 ? 1 : 2;
	}
      gcc_assert (old == issues);
    }
  return issues;
}

namespace {

/* Local spelling kept for the greedy selection below.  */
static unsigned
materialization_cost (gcall *call)
{
  return rvtt_sfpxloadi_materialization_cost (call);
}

/* Select the most expensive invariant materializations which fit the
   architectural LREG pressure bound.  The old all-or-nothing policy left
   every constant in a counted loop when only one live range exceeded the
   bound.  Greedy selection is safe because the pressure proof re-runs the full
   conservative liveness proof after every addition; it is also deterministic
   because equal-cost candidates retain source order.  */
static auto_vec<gcall *>
select_pressure_legal_loads (class loop *loop, auto_vec<gcall *> &loads,
			     bool cc_transients)
{
  std::stable_sort (loads.begin (), loads.end (),
		    [] (gcall *a, gcall *b)
		    {
		      return materialization_cost (a) > materialization_cost (b);
		    });

  /* One base profile; each verdict is an incremental residual query
     (verdict-identical to the full proof, asserted under
     flag_checking) instead of a full-function walk per candidate.  */
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

/* ---------------- Structured-CC-restore proof (EC-F1) ----------------

   rvtt_loop_has_sfpu_barrier_p refuses a loop on ANY CC writer.  That
   obligation is genuine -- SFPLOADI is lane-predicated (`if
   (LaneEnabled)` in the functional model, tt-isa-documentation
   SFPLOADI.md), so hoisting a load out of a loop whose CC state at the
   load's position could differ from the preheader's would change which
   lanes are written -- but it is DISCHARGEABLE when the loop provably
   RESTORES the CC state:

   The architectural lane-enable state is the pair {LaneFlags,
   UseLaneFlagsForLaneEnable} (VectorUnit.md IsLaneEnabled).  SFPPUSHC
   mod 0 pushes exactly that pair onto the flag stack and SFPPOPC mod 0
   pops it back VERBATIM (SFPPUSHC.md / SFPPOPC.md functional models;
   craq-sim TENSIX_EXECUTE_SFPPUSHC/SFPPOPC agree).  Therefore, in a
   loop body where

     (a) every SFPPUSHC is the plain push (gimple mod
	 SFPPUSHCC_MOD1_PUSH; any other mod mutates the saved stack
	 entry and breaks the restore),
     (b) every SFPPOPC is the plain pop (SFPPOPCC_MOD1_POP; the peek
	 modes rewrite the live flags without popping),
     (c) push/pop depth is consistent at every control-flow join,
	 never underflows, and returns to zero on the loop backedge,
	 and
     (d) no other CC-writing statement executes at push depth zero,

   the lane-enable state at every depth-zero position equals the
   loop-entry state on every iteration -- which is exactly the state at
   the preheader insertion point.  Hoisting a depth-zero invariant
   SFPLOADI to the preheader writes the same lanes it wrote in place.

   For a candidate INSIDE a balanced region (depth > 0) the masks are
   not equal, but hoisting is still sound when every in-region CC
   modifier can only NARROW the enable set relative to the region
   entry:

     - SFPSETCC and the CC-writing SFPIADD forms update LaneFlags only
       in enabled lanes (`if (LaneEnabled)`, SFPSETCC.md / SFPIADD.md),
       so disabled lanes stay disabled;
     - SFPCOMPC computes LaneFlags = Top.LaneFlags && !LaneFlags
       against the stack top -- the region-entry save -- so its result
       is contained in the region-entry enable set (SFPCOMPC.md);
     - the structured condition markers (sfpxvif / sfpxcondb /
       sfpxbool) lower in pass_rvtt_expand to exactly this class --
       compare + SFPSETCC/SFPCOMPC chains, plus balanced internal
       PUSHC/POPC pairs for De Morgan reworks -- all confined between
       the region's PUSHC and the condition anchor
       (gimple-rvtt-expand.cc process_tree/process_bool_tree audit).

   Then the enable set at the candidate's position is a SUBSET of the
   preheader's.  The hoisted load writes the constant to a superset of
   the lanes the in-place load wrote; the extra lanes belong to the
   candidate's own fresh SSA definition, whose content in those lanes
   was never written by the original program (an all-constant,
   non-live-value load) and is therefore an RA-dependent indeterminate
   value no defined consumer can rely on: every SFPU consumer's write
   is itself lane-predicated, so lanes outside its own mask do not
   propagate, and a merge consumer (sfpassign_lv) keeps its own
   position and mask and lowers to the lane-predicated SFPMOV merge
   when the load no longer directly precedes it (rvtt.md
   *rvtt_sfpassign_lv_int).  SFPENCC can WIDEN the enable set
   (SFPENCC.md) and is not in the audited narrowing set: an ENCC (or
   any unaudited CC writer) inside a region keeps the restore proof --
   the POPC discards it -- but forfeits in-region candidate admission.

   The remaining EE-obligations are discharged by existing machinery:
   rename-to-free-LREG is inherent in hoisting the SSA definition (the
   preheader definition gets its own register, live across the loop;
   whether a free LREG exists is exactly the
   loop pressure proof (rvtt-pressure.cc), which refuses per-candidate
   by name), and CC-position placement is the preheader itself, which
   this proof shows carries the loop-entry mask.

   The sfpi frontend's v_endif emits its POPCs through a small counted
   scalar loop (the CC object's destructor); at this pass's position
   that loop survives as a subloop of the row loop whose body is the
   POPC block.  The analysis summarizes such a subloop by proving its
   exact trip count with the same bounded constant evaluation the
   replay-unroll request uses, then charges depth for POPC-count *
   trips.  Any other CC-containing subloop shape refuses.  */

struct cc_restore_analysis
{
  bool has_cc = false;		/* any CC machinery in the loop */
  bool narrow_ok = true;	/* all in-region modifiers audited-narrowing */
  const char *why = nullptr;	/* named refusal when the proof fails */
  /* Push depth on entry to each top-level body block.  */
  std::unordered_map<basic_block, int> entry_depth;
  /* Exact POPC executions per full execution of a summarized subloop,
     and its single in-loop continuation block.  */
  std::unordered_map<class loop *, int> sub_pops;
  std::unordered_map<class loop *, basic_block> sub_exit;
};

/* CC modifiers whose eventual hardware flag writes provably only
   narrow the enable set relative to the enclosing region entry (see
   the audit in the block comment above).  Everything else -- SFPENCC,
   the exponent/priority-encode CC forms, and any future CC writer --
   refuses in-region candidates until audited.  */
static bool
cc_narrowing_modifier_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
      return true;
    default:
      return false;
    }
}

/* Constant integer mod operand of CALL, or -1.  */
static long
const_mod_arg (const rvtt_insn_data *insnd, gcall *call)
{
  if (!insnd->has_mod ()
      || (unsigned) insnd->mod_arg () >= gimple_call_num_args (call))
    return -1;
  tree mod = gimple_call_arg (call, insnd->mod_arg ());
  return TREE_CODE (mod) == INTEGER_CST ? (long) TREE_INT_CST_LOW (mod) : -1;
}

/* Classify one statement of LOOP's body for the restore proof,
   adjusting *DEPTH.  DEPTH == nullptr means "no CC machinery allowed
   here" (subloop bodies outside the audited destructor-pop shape).
   Returns false and sets A.why on refusal.  */
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
	    /* Non-plain push mutates the saved stack entry: the later
	       POPC would restore a value that is not the region-entry
	       state.  */
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
	    /* Pops a save pushed outside the loop: iteration 2 would
	       pop yet another -- no restore fact exists.  */
	    a.why = "cc-restore-unbalanced";
	  return !a.why;

	case rvtt_insn_data::sfpxvif:
	case rvtt_insn_data::sfpxcondb:
	case rvtt_insn_data::sfpxbool:
	  /* Structured condition markers: their expander-inserted CC
	     effects are confined to the enclosing balanced region (see
	     block comment).  Outside a region there is no PUSHC to
	     confine them: refuse.  */
	  a.has_cc = true;
	  if (!depth || *depth == 0)
	    a.why = "cc-restore-marker-ambient";
	  return !a.why;

	case rvtt_insn_data::sfpxcondi:
	  /* Condition-value materialization: its expansion inserts CC
	     writes at its own position outside any user region; not
	     audited here.  Fail closed.  */
	  a.has_cc = true;
	  a.why = "cc-restore-cond-value-unaudited";
	  return false;

	default:
	  if (insnd->sets_cc (call))
	    {
	      a.has_cc = true;
	      if (!depth)
		a.why = "cc-restore-subloop-shape";
	      else if (*depth == 0)
		/* A live-flag write with no enclosing save: the state
		   entering the next iteration (and every later
		   depth-zero position) is not the loop-entry state.  */
		a.why = "cc-restore-ambient-cc-write";
	      else if (!cc_narrowing_modifier_p (insnd))
		/* Restore still holds (the POPC discards it), but
		   in-region candidates lose the containment fact.  */
		a.narrow_ok = false;
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

/* Exact number of times the latch of the two-block subloop S executes,
   proven by bounded constant evaluation of its header test through the
   unique entry edge (the same discipline as
   short_constant_replay_loop_p), or -1.  */
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

/* Summarize the direct subloop S of the loop under analysis: either it
   contains no CC machinery at all (transparent, zero pops), or it is
   the frontend's v_endif destructor-pop shape -- a two-block counted
   loop whose latch performs only plain POPCs and scalar bookkeeping --
   with a proven trip count.  Also classifies every statement for the
   ordinary barrier classes.  Returns false and sets A.why on
   refusal.  */
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
	  case rvtt_insn_data::sfpxvif:
	  case rvtt_insn_data::sfpxcondb:
	  case rvtt_insn_data::sfpxbool:
	  case rvtt_insn_data::sfpxcondi:
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

  /* CC-containing subloop: only the audited destructor-pop shape is
     admitted.  Two blocks; header carries only the counter test;
     latch carries the plain POPCs and scalar bookkeeping.  */
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
	  /* No CC machinery may sit in the header (it would execute
	     once more than the latch).  */
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

/* The restore proof for LOOP: propagate push depth over the top-level
   body blocks (subloops summarized), requiring consistency at joins
   and zero on the backedge.  Fills A.  Returns false (with A.why
   named) when the proof fails or a non-CC barrier class is present --
   the superset of rvtt_loop_has_sfpu_barrier_p's refusals minus the
   provably-restored CC classes.  */
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
	      /* Join consistency on the subloop entry (recorded but
		 never scanned as a block), then charge its summarized
		 pops and continue at its single exit.  A transparent
		 subloop (no pops, exit unrecorded) propagates the
		 unchanged depth to every exit.  */
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
		      /* A summarized subloop exiting straight into
			 another subloop's header: fail closed rather
			 than scan subloop blocks as if top-level.  */
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

/* Push depth at CALL's position: the recorded block entry depth plus
   the pushes/pops that precede it in its block.  */
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

static bool
transform (function *fn)
{
  bool changed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  /* Innermost first: a load hoists stepwise, out of its own loop into the
     enclosing loop's body, where the enclosing loop's own proofs decide
     whether it moves again.  Every proof below is per-loop and re-runs on
     the CFG as already transformed.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      basic_block bb = loop->header;

      edge entry = rvtt_loop_entry_edge (loop);
      if (!entry)
	continue;

      /* Refuse whenever opaque state exists inside the hoist region
	 ({preheader tail at/after the insertion point} union {loop
	 body}; see rvtt_loop_hoist_region_opaque_p).  Opacity elsewhere
	 in the function cannot interleave with the hoisted live ranges
	 and is no reason to refuse an otherwise proven loop.  */
      if (rvtt_loop_hoist_region_opaque_p (loop, entry))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Invariant SFPU immediate hoist refused: function has opaque LREG state\n");
	  continue;
	}

      /* A dedicated preheader receives the loads at its end; any other
	 entry block keeps its own statements and a fresh block is split
	 from the entry edge at commit time.  A dedicated preheader
	 ending in a non-opaque block terminator cannot receive an
	 insertion after that terminator; refuse structurally.  */
      if (rvtt_preheader_insertion_blocked_p (entry))
	continue;

      if (!rvtt_loop_first_iteration_executes_p (loop, entry))
	continue;

      /* Barrier classes, with the CC classes replaced by the
	 structured-CC-restore proof (block comment above): a loop
	 whose every CC write is confined to balanced plain-PUSHC /
	 plain-POPC regions restores the lane-enable state each
	 iteration, so depth-zero positions carry the preheader mask
	 and in-region positions carry a provable subset of it.  Any
	 failure refuses the loop exactly as the old barrier did.  */
      cc_restore_analysis cc;
      if (!analyze_cc_restore (loop, cc))
	{
	  /* The loop bb index makes multi-loop refusal dumps
	     attributable (two bare identical lines were
	     indistinguishable -- FH audit FHI-T5); dg twins scan the
	     refusal-name substring, unaffected by the suffix.  */
	  rvtt_refuse_by_name (cc.why ? cc.why : "sfpu-barrier", dump_file,
			       "Invariant SFPU immediate hoist refused:"
			       " %s (loop bb %d)\n",
			       cc.why ? cc.why : "sfpu-barrier",
			       loop->header->index);
	  continue;
	}

      /* SFPLOADI writes an architectural LREG even though its SSA result is
	 local.  Do not speculate it out of a loop that may execute zero times;
	 require a structurally proven first iteration and at least one expected
	 backedge for profitability.  */
      if (expected_loop_iterations_unbounded (loop) < 1)
	continue;

      /* Collect candidate loads from the loop's direct body blocks (a load
	 still inside a subloop was already refused there and would only see
	 more pressure here).  Each load's block must provably execute on
	 every iteration that enters the body — never speculate the
	 architectural LREG write.  */
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
		/* A candidate inside a CC region (push depth > 0) is
		   admitted only under the containment fact: every
		   in-region CC modifier in this loop is in the audited
		   narrowing set, so the position's enable set is a
		   subset of the preheader's and the hoisted all-lanes
		   write is a refinement (block comment above).  */
		if (cc_depth_at_stmt (cc, call) > 0 && !cc.narrow_ok)
		  {
		    if (dump_file)
		      {
			rvtt_refuse (RVTT_REF_CC_POSITION_WIDENING_UNPROVEN, dump_file,
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

      /* EL-vs-RESIDENCY ORDERING (lane HN): when the late
	 const-residency walk with its pressure-park tier is enabled in
	 this compilation, a CC-restore loop DEFERS its invariant
	 immediate hoists to that walk entirely.  Two composition
	 defects make the early hoist here strictly dominated on this
	 loop class (softplus-fresh anatomy, laneHJ census):

	 - BUDGET ORDERING: each hoist below pins one LREG across the
	   loop under this pass's conservative single-body SSA walk,
	   spending exactly the free registers the 295t walk's exact
	   function-wide pressure model would allocate by priced
	   selection over ALL of the loop's constants (PRGM tiers
	   first, LREG parks by rank) -- first-come here starves the
	   arbiter there (softplus: one early hoist cost two parks).

	 - LV-CARRIER FORGING: hoisting a predicated in-region
	   materialization to the preheader upgrades its disabled
	   lanes from RA-indeterminate to defined-constant; a merge
	   consumer (sfpassign_lv) whose live-tie the liveness pass
	   would have BROKEN against the in-place definition (same
	   region, same generation) now keeps a genuine cross-region
	   tie and lowers to a per-iteration lane-predicated SFPMOV
	   merge -- a forged word no later pass can remove.

	 PARK-SEED COMPOSITION REFINEMENT (lane HY): the original
	 wholesale deferral over-reached.  Its claim -- "the late walk
	 supersedes every hoist this pass could commit (same
	 candidates, superset of placements)" -- is REFUTED on the
	 depth-zero candidate class by six measured pin-34 loss rows
	 (ceil/roundingops/rdiv/sqrt/softsign/i0, laneHV attribution +
	 laneHX dump chain + laneHY censuses): this pass's hoist of a
	 depth-zero candidate is a mask-exact free code motion (the
	 restore proof makes its position carry the preheader's
	 lane-enable mask, so the moved statement reads the identical
	 enable set), while the late walk can
	 re-place such a candidate only BEHIND a manufactured all-lanes
	 programming point -- the CC-canonical first-iteration peel --
	 whose costs its break-even pricing measures against the
	 in-loop materialization, never against this pass's free hoist:

	 - the peel plus PRGM programming pairs are extra prologue
	   words on every kernel entry (rdiv +2, softsign +2, i0 +4,
	   sqrt +7 words vs the drop-one legs; 253-895 cycles pure);
	 - a PRGM park of a value with a creg-incapable consumer trades
	   the in-loop SFPLOADI for an in-loop SFPMOV copy (sqrt: word
	   count unchanged, placement strictly worse);
	 - and on an even-trip paired row loop the peel flips the trip
	   parity, so the crossrow-pairing capture refuses
	   crossrow-pairing-trips-odd and the paired 2-row record --
	   plus everything the pairing seed builds on it -- never forms
	   (ceil/roundingops: the paired 0,28/15-launch form degraded
	   to per-row 0,14/30-launch, -4736 cycles forgone).

	 Both HN defects live in the IN-REGION class: the lv-carrier
	 forging is an in-region (predicated) materialization by
	 construction, and the budget starvation was that same
	 hoist's pinned LREG (softplus: the one early hoist was the
	 region-interior 0xb8047b21 carrier).  So the deferral hands
	 the walk exactly the in-region candidates -- the class the
	 walk owns with added value (post-CC audited parks under its
	 exact function-wide pressure model) -- and KEEPS the depth-zero
	 hoists here, by name (depth-zero-hoist-dominant).  The
	 boundary is the restore analysis's own CC-region depth, the
	 same fact the collection admission above is built on: a
	 depth-zero candidate's hoist moves the statement between two
	 positions carrying the identical lane-enable mask (the
	 restore proof), a pure free code motion no walk placement can
	 beat, and one whose disabled-lane upgrade -- HN's forging
	 mechanism -- cannot occur because the original position is
	 already unpredicated relative to the preheader.  An IN-REGION
	 (depth > 0) candidate is exactly where both HN defects live
	 and where the walk's audited post-CC admission is the safe
	 placement authority; it defers.  Measured discharge of the
	 depth-zero claim: the six pin-34 loss rows above; measured
	 discharge of the in-region claim: softplus-fresh, whose
	 deferral class is entirely in-region and whose booked winning
	 bytes this split preserves wholesale.  Loops without CC
	 machinery, and compilations without both late flags, keep the
	 early pass's established hoists byte-identically.  */
      if (riscv_tt_opt_park_ordering > 0
	  && cc.has_cc
	  && riscv_tt_opt_const_residency > 0
	  && riscv_tt_opt_pressure_park > 0)
	{
	  /* LUT-COEFFICIENT AUTHORITY (measured, sigmoid-appx-tree
	     anatomy): a loop whose body carries LUT machinery
	     (SFPLUT/SFPLUTFP32) keeps the ESTABLISHED wholesale
	     deferral regardless of depth — the in-loop constant
	     materializations there are LUT slot coefficients whose
	     placement authority belongs to the lut-select passes
	     (shortened slot materializations at the LUT programming
	     point, lane HT's machinery); an early depth-zero hoist
	     moves the coefficient out from under that discovery and
	     the 5-word LUT row decays to a mov-laden 7-word body
	     (silicon: 29861 -> 43447 under an unconditional keep;
	     byte-identical under this gate).  The kept-hoist evidence
	     rows (ceil/rops/rdiv/sqrt/softsign/i0, hardsigmoid) carry
	     no LUT statement.  */
	  bool lut_body = false;
	  basic_block *nest = get_loop_body (loop);
	  for (unsigned ix = 0; ix != loop->num_nodes && !lut_body; ++ix)
	    for (gimple_stmt_iterator gsi = gsi_start_bb (nest[ix]);
		 !gsi_end_p (gsi) && !lut_body; gsi_next (&gsi))
	      if (const rvtt_insn_data *insnd
		    = rvtt_get_insn_data (gsi_stmt (gsi)))
		if (insnd->id == rvtt_insn_data::sfplut
		    || insnd->id == rvtt_insn_data::sfplutfp32_3r
		    || insnd->id == rvtt_insn_data::sfplutfp32_6r)
		  lut_body = true;
	  free (nest);
	  if (lut_body && dump_file)
	    fprintf (dump_file,
		     "park-ordering: loop bb %d defers wholesale:"
		     " lut-coefficient-authority (the body's LUT slot"
		     " coefficients belong to the lut-select placement)\n",
		     loop->header->index);

	  /* IN-REGION DEMAND (measured, sigmoid-appx-tree /
	     lut-variant anatomy): each in-region candidate is a
	     placement demand on the LATER authorities (the lut-select
	     coefficient placement at 288t, the walk's audited post-CC
	     parks at 296t) whose budgets share the 8-LREG file with
	     whatever this pass pins early.  A loop with THREE OR MORE
	     in-region invariant constants is in the
	     pressure-arbitrated regime — HN's budget-ordering defect
	     applies to the depth-zero keeps too (sigmoid-appx-tree:
	     three early keeps pushed the lut-select coefficient
	     placement to lut-coefficient-pressure, LREG 9 > 8, and the
	     5-word LUT row decayed to 43447 cycles) — so the whole
	     loop keeps the ESTABLISHED wholesale deferral, by name
	     (in-region-demand).  Every measured kept-hoist winner
	     (ceil/rops/rdiv/sqrt 1, softsign/i0 2, hardsigmoid 1)
	     sits at demand <= 2; every measured deferral winner
	     (softplus 10, gelu 10, sigmoidlut 10, tanh-lut 4, the
	     tree 4) at >= 3.  A finer per-authority priced
	     arbitration is the named successor.  */
	  unsigned in_region = 0;
	  for (gcall *call : loads)
	    if (cc_depth_at_stmt (cc, call) > 0)
	      ++in_region;
	  bool demand_defer = in_region >= 3;
	  if (demand_defer && !lut_body && dump_file)
	    fprintf (dump_file,
		     "park-ordering: loop bb %d defers wholesale:"
		     " in-region-demand (%u in-region constants: the"
		     " later placement authorities own this loop's"
		     " pressure arbitration)\n",
		     loop->header->index, in_region);
	  bool wholesale = lut_body || demand_defer;
	  unsigned kept = 0;
	  for (gcall *call : loads)
	    {
	      if (!wholesale && cc_depth_at_stmt (cc, call) == 0)
		{
		  if (dump_file)
		    {
		      rvtt_refuse (RVTT_REF_DEPTH_ZERO_HOIST_DOMINANT, dump_file,
				   "Invariant SFPU immediate hoist kept"
				   " under park-ordering:"
				   " depth-zero-hoist-dominant"
				   " (loop bb %d): the hoist is a mask-exact"
				   " free move; the late walk could re-place"
				   " it only behind its manufactured"
				   " trip-parity-flipping peel: ",
				   loop->header->index);
		      print_gimple_stmt (dump_file, call, 0);
		    }
		  loads[kept++] = call;
		}
	      else if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_RESIDENCY_WALK_ORDERING, dump_file,
			       "Invariant SFPU immediate hoist deferred:"
			       " residency-walk-ordering (loop bb %d): the"
			       " enabled const-residency walk owns this"
			       " CC-restore loop's %s constants: ",
			       loop->header->index,
			       lut_body ? "lut-coefficient"
			       : demand_defer ? "demand-arbitrated"
			       : "in-region");
		  print_gimple_stmt (dump_file, call, 0);
		}
	    }
	  loads.truncate (kept);
	  if (loads.is_empty ())
	    continue;
	}

      /* A CC-carrying loop under an explicit unroll request multiplies
	 its in-loop live ranges by the unroll factor after this pass;
	 the single-body SSA pressure walk models none of that overlap,
	 and a miss is not a lost optimization but the post-allocation
	 lreg-pressure-exceeded USER ERROR on a previously-compiling
	 kernel (corpus witness: the pragma-unroll-8 snake-beta body).
	 Such loops are the replay-record delivery domain where in-loop
	 immediates are captured into the recorded window anyway;
	 refuse hoisting by name.  Non-CC unrolled loops keep their
	 established behavior.  */
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

      /* Never overwrite an explicit user unroll request (loop->unroll is
	 nonzero once "#pragma GCC unroll N" has been recorded during CFG
	 construction); in particular "#pragma GCC unroll 1" must keep its
	 scalar loop.  Only the unroll request defers to the pragma — the
	 invariant hoist below is independent and still proceeds.  */
      /* CC-carrying loops are newly reachable here under the restore
	 proof; the unroll request keeps its pre-existing surface (loops
	 with no CC machinery at all) so this widening changes exactly
	 the immediate hoists and nothing else.  */
      if (riscv_tt_opt_replay_hoist > 0
	  && !cc.has_cc
	  && !loop->unroll
	  && short_constant_replay_loop_p (loop, entry))
	{
	  loop->unroll = USHRT_MAX;
	  if (dump_file)
	    fprintf (dump_file,
		     "Requested complete unroll for constant replay loop bb %d\n",
		     bb->index);
	}

      /* Commit: all proofs hold and at least one load will move.  A
	 shared entry edge is split only now, so every refusal above
	 remains byte-identical to the flag-off compilation.  */
      basic_block preheader = rvtt_commit_hoist_preheader (entry);

      for (gcall *call : selected)
	{
	  /* A load hoisted out of an inner loop earlier in this same pass
	     execution carries re-scanned, not-yet-renamed virtual operands
	     (bare .MEM); only a renamed SSA definition has uses to unlink
	     or a name to release.  */
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
