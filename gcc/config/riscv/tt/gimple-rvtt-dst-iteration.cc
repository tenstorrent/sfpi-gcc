/* Fuse legal adjacent Dst-register iterations.
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

/* Fusion and interleaving of adjacent Dst-register iterations.

   LLK kernels iterate over rows of the Tensix Dst register file with a
   fixed pattern: a body of SFPU loads/computes/stores addressed
   relative to the row-write counter (RWC), closed by a TTINCRWC that
   advances the counter.  The first two passes here widen that grain
   from one row-pair to two, halving the TTINCRWC overhead and doubling
   the straight-line body available to the RTL schedulers.

   rvtt_dst_iteration (fusion): find two consecutive iterations -- body,
   TTINCRWC(+2), identical body, TTINCRWC(+2) -- whose calls match
   pairwise (same operations, scalar operands and Dst addresses, vector
   operands isomorphic under the value map the pair establishes).
   Every typed Dst access must use the no-auto-increment mode with an
   address that stays in range after the +2 rewrite (using VRP), each
   body's vector values must be used only inside it, and there must be
   at least two loads and a store.  The second body's Dst addresses are
   rewritten to base+2 and the two TTINCRWC(+2)s become one
   TTINCRWC(+4).

   rvtt_dst_interleave runs late, after lowering, before RTL expansion.
   A fused group (marked by its TTINCRWC(+4)) whose halves still match
   is rescheduled load/load/compute/compute/drain/drain so the two rows'
   independent chains overlap.  This also requires the
   loads-computes-drains shape with one store per half placed last,
   4-byte alignment of the first row's addresses (so the +2 row cannot
   alias the first row's store), scalar operands defined before the
   group, and closed vector def-use within the group.  It removes no
   instruction; it fills issue slots at the cost of longer LREG live
   ranges, hence the separate pass.

   Any unrecognized statement inside a candidate rejects it (with a
   TDF_DETAILS reason).  On Quasar the analysis runs and is dumped but
   nothing is rewritten.  Both passes run under
   -mtt-tensix-optimize-dst-iteration-fusion.

   This is unroll-and-jam (Carr and Kennedy, ACM TOPLAS 16(6), 1994)
   where the unrolled copies already exist.  gimple-loop-jam.cc does not
   apply: there is no loop nest, and the only shared resource is the Dst
   RWC, which GCC's dependence analysis cannot see.  */

#define INCLUDE_UNORDERED_MAP
#define INCLUDE_UNORDERED_SET
#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "gimple-iterator.h"
#include "ssa-iterators.h"
#include "tree-ssa-operands.h"
#include "gimple-range.h"
#include "dominance.h"
#include "cfgloop.h"
#include "tree-ssa-loop-niter.h"
#include "tree-into-ssa.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-protos.h"
#include "rvtt-trips.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"

namespace {

/* One candidate iteration: its body calls and closing TTINCRWC.  */

struct dst_iteration
{
  std::vector<gcall *> calls;
  gcall *increment = nullptr;
  bool rejected = false;
};

/* Return true if A and B are both INTEGER_CSTs of equal value; false
   when either operand is not a constant.  */

static bool
integer_cst_eq (tree a, tree b)
{
  return TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST
    && tree_int_cst_equal (a, b);
}

/* Return true if VALUE is a non-null tree of vector type -- an SFPU
   vector datum rather than a scalar or address operand.  */

static bool
vector_value_p (tree value)
{
  return value && VECTOR_TYPE_P (TREE_TYPE (value));
}

/* Return true if INSND is a typed Dst-register access (sfpload or
   sfpstore) -- the calls whose address argument the fusion
   rewrites.  */

static bool
typed_dst_access_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpstore;
}

/* Return true if CALL (insn data INSND) may appear in a fusable
   iteration body: a typed Dst access, or a side-effect-free and
   CC-neutral operation (live variants only via sfpassign_lv).  */

static bool
body_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (typed_dst_access_p (insnd))
    return true;
  if (insnd->id == rvtt_insn_data::synth_opcode)
    return false;
  /* SFPASSIGN_LV is the pure lane-merge used to materialize a value after a
     conversion.  Its explicit live operands are checked for iteration-local
     provenance in same_body_p.  Other live variants remain ineligible.  */
  bool legal_live = !insnd->is_live ()
    || insnd->id == rvtt_insn_data::sfpassign_lv;
  return legal_live && !insnd->has_side_effects (call)
    && !insnd->sets_cc (call);
}

/* Number of bits in the Dst address field of a typed sfpload or
   sfpstore on the current Tensix target, bounding how far an
   address can be raised when the second iteration is rewritten.  */

static unsigned
dst_address_bits (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpload:
    case rvtt_insn_data::sfpstore:
      return TARGET_XTT_TENSIX_WH ? 14 : TARGET_XTT_TENSIX_BH ? 13 : 10;
    default:
      gcc_unreachable ();
    }
}

/* Return true if typed Dst access CALL tolerates having its address
   raised by INCREMENT: the mod operand is zero, the addressing mode
   is the target's no-auto-increment mode, and the address (constant,
   or an SSA name with a proven non-negative range) stays within the
   Dst address field after adding INCREMENT.  */

static bool
dst_access_legal_p (gcall *call, HOST_WIDE_INT increment)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  unsigned addr_arg = insnd->id == rvtt_insn_data::sfpload ? 1 : 2;
  unsigned mod_arg = insnd->id == rvtt_insn_data::sfpload ? 4 : 5;
  unsigned mode_arg = mod_arg + 1;
  tree addr = gimple_call_arg (call, addr_arg);
  tree mod = gimple_call_arg (call, mod_arg);
  tree mode = gimple_call_arg (call, mode_arg);
  unsigned no_increment_mode = TARGET_XTT_TENSIX_WH ? 3 : 7;
  if (!integer_zerop (mod)
      || TREE_CODE (mode) != INTEGER_CST
      || wi::to_wide (mode) != no_increment_mode)
    return false;
  unsigned address_bits = dst_address_bits (insnd);
  unsigned HOST_WIDE_INT limit = (HOST_WIDE_INT_1U << address_bits) - 1;
  if (tree_fits_uhwi_p (addr))
    return tree_to_uhwi (addr) <= limit - increment;

  int_range_max range;
  if (TREE_CODE (addr) != SSA_NAME
      || !get_range_query (cfun)->range_of_expr (range, addr, call)
      || range.undefined_p () || range.varying_p ()
      || wi::neg_p (range.lower_bound ()))
    return false;
  return wi::leu_p (range.upper_bound (), limit - increment);
}

/* Return true if ADJUSTED is provably BASE + INCREMENT: either both
   are constants with that difference, or ADJUSTED is defined by a
   PLUS_EXPR of BASE and constant INCREMENT in either operand
   order.  */

static bool
address_plus_p (tree base, tree adjusted, HOST_WIDE_INT increment)
{
  if (tree_fits_uhwi_p (base) && tree_fits_uhwi_p (adjusted))
    return tree_to_uhwi (adjusted) == tree_to_uhwi (base) + increment;
  if (TREE_CODE (adjusted) != SSA_NAME)
    return false;
  gassign *def = dyn_cast<gassign *> (SSA_NAME_DEF_STMT (adjusted));
  if (!def || gimple_assign_rhs_code (def) != PLUS_EXPR)
    return false;
  tree lhs = gimple_assign_rhs1 (def);
  tree rhs = gimple_assign_rhs2 (def);
  return ((operand_equal_p (lhs, base, 0) && tree_fits_shwi_p (rhs)
	   && tree_to_shwi (rhs) == increment)
	  || (operand_equal_p (rhs, base, 0) && tree_fits_shwi_p (lhs)
	      && tree_to_shwi (lhs) == increment));
}

/* Return true if ADDRESS is provably a multiple of ALIGNMENT (a
   power of two), judged by its known-zero low bits.  */

static bool
address_aligned_p (tree address, unsigned alignment)
{
  gcc_assert (pow2p_hwi (alignment));
  wide_int nonzero = get_nonzero_bits (address);
  return wi::extract_uhwi (nonzero, 0, exact_log2 (alignment)) == 0;
}

/* Return true if CALL is a TTINCRWC that steps only the Dst counter,
   storing the constant step in *AMOUNT.  The CR/B/A operands must be
   zero, and only the unit iteration step of 2 is accepted.  */

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
  return *amount == 2;
}

/* Merge iteration SECOND into FIRST's Dst window: raise every typed
   Dst address in SECOND by INCREMENT (emitting a PLUS_EXPR before
   the call for non-constant addresses), double SECOND's TTINCRWC
   step to cover both iterations, and remove FIRST's increment.  */

static void
fuse_iterations (dst_iteration &first, dst_iteration &second,
		 HOST_WIDE_INT increment)
{
  for (gcall *call : second.calls)
    {
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (!typed_dst_access_p (insnd))
	continue;
      unsigned addr_arg = insnd->id == rvtt_insn_data::sfpload ? 1 : 2;
      tree old_address = gimple_call_arg (call, addr_arg);
      tree delta = build_int_cst (TREE_TYPE (old_address), increment);
      tree new_address;
      if (tree_fits_uhwi_p (old_address))
	new_address = build_int_cst (TREE_TYPE (old_address),
				     tree_to_uhwi (old_address) + increment);
      else
	{
	  new_address = make_ssa_name (TREE_TYPE (old_address));
	  gassign *add = gimple_build_assign (new_address, PLUS_EXPR,
					old_address, delta);
	  gimple_stmt_iterator at = gsi_for_stmt (call);
	  gsi_insert_before (&at, add, GSI_SAME_STMT);
	}
      gimple_call_set_arg (call, addr_arg, new_address);
      update_stmt (call);
    }

  tree old_increment = gimple_call_arg (second.increment, 1);
  gimple_call_set_arg (second.increment, 1,
		       build_int_cst (TREE_TYPE (old_increment),
				      increment * 2));
  update_stmt (second.increment);

  gimple_stmt_iterator gsi = gsi_for_stmt (first.increment);
  if (gimple_vdef (first.increment))
    unlink_stmt_vdef (first.increment);
  gsi_remove (&gsi, true);
}

/* Return true if iterations A and B have isomorphic bodies fusable
   under Dst step INCREMENT: pairwise-equal opcodes, arities, scalar
   operands and typed addresses; every typed access legal to rewrite
   (dst_access_legal_p); and vector operands matched through the map
   of paired definitions, so B computes A's dataflow over its own
   loads.  Requires at least two loads and one store, the shape the
   fusion targets.  */

static bool
same_body_p (const dst_iteration &a, const dst_iteration &b,
	     HOST_WIDE_INT increment)
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
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Dst-body mismatch: call=%u opcode/arity\n", i);
	  return false;
	}

      bool access = typed_dst_access_p (ia);
      if (access)
	{
	  if (!dst_access_legal_p (ca, increment)
	      || !dst_access_legal_p (cb, increment))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "Dst-body mismatch: call=%u access-legality\n", i);
	      return false;
	    }
	  ia->id == rvtt_insn_data::sfpload ? ++loads : ++stores;
	}

      unsigned addr_arg = ia->id == rvtt_insn_data::sfpload ? 1
	: ia->id == rvtt_insn_data::sfpstore ? 2 : ~0u;
      /* Synthesized encoding operands identify the compile-time instruction
	 buffer slot, not the operation's semantics, and legitimately differ
	 between otherwise identical unrolled iterations.  */
      unsigned var_arg = ia->has_var () ? ia->var_arg () : ~0u;
      unsigned id_arg = ia->has_var () ? ia->id_arg () : ~0u;
      for (unsigned argno = 0; argno != gimple_call_num_args (ca); ++argno)
	{
	  if (argno == var_arg || argno == id_arg)
	    continue;
	  tree va = gimple_call_arg (ca, argno);
	  tree vb = gimple_call_arg (cb, argno);
	  if (argno == addr_arg)
	    {
	      if (!operand_equal_p (va, vb, 0))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file,
			     "Dst-body mismatch: call=%u arg=%u address\n", i,
			     argno);
		  return false;
		}
	      continue;
	    }
	  if (vector_value_p (va))
	    {
	      auto it = values.find (va);
	      if (ia->is_live () && it == values.end ())
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file,
			     "Dst-body mismatch: call=%u arg=%u live-in\n", i,
			     argno);
		  return false;
		}
	      if ((it == values.end () && va != vb)
		  || (it != values.end () && it->second != vb))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file,
			     "Dst-body mismatch: call=%u arg=%u vector\n", i,
			     argno);
		  return false;
		}
	    }
	  else if (!operand_equal_p (va, vb, 0))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "Dst-body mismatch: call=%u arg=%u scalar\n",
			 i, argno);
	      return false;
	    }
	}

      tree la = gimple_call_lhs (ca);
      tree lb = gimple_call_lhs (cb);
      if (!!la != !!lb)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Dst-body mismatch: call=%u lhs\n", i);
	  return false;
	}
      if (vector_value_p (la))
	values.emplace (la, lb);
    }
  return loads >= 2 && stores >= 1;
}

/* Return true if every vector value defined by ITERATION's calls is
   consumed only by other statements of the same iteration (debug
   uses aside) -- the group is a self-contained unrolled body.  */

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
	    if (!is_gimple_debug (use) && !members.count (use))
	      return false;
	}
  return true;
}

/* Scan each basic block of FN for adjacent TTINCRWC-delimited
   iteration groups and fuse each legal pair: equal step, isomorphic
   closed bodies, and rewritable typed addresses.  Unrecognized
   statements with memory or other side effects poison the open
   group.  Fusion is suppressed (candidates only counted and dumped)
   on QSR targets.  Returns the number of candidates seen.  */

static unsigned
discover (function *fn)
{
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
		  bool increment_legal
		    = increment_p (previous.increment, &prior)
		    && prior == increment;
		  bool same_body = increment_legal
		    && same_body_p (previous, current, increment);
		  bool first_closed = same_body && defs_closed_p (previous);
		  bool second_closed = first_closed && defs_closed_p (current);
		  bool legal = second_closed;
		  if (!legal && dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file,
			     "Dst-iteration rejected: bb=%d first-ops=%zu "
			     "second-ops=%zu rejected=%d/%d increment=%d "
			     "same-body=%d closed=%d/%d\n",
			     bb->index, previous.calls.size (),
			     current.calls.size (), previous.rejected,
			     current.rejected, increment_legal,
			     same_body, first_closed, second_closed);
		  if (legal)
		    {
		      ++candidates;
		      bool emit = !TARGET_XTT_TENSIX_QSR;
		      if (emit)
			fuse_iterations (previous, current, increment);
		      if (dump_file)
			fprintf (dump_file,
				 "Dst-iteration candidate: bb=%d ops=%zu "
				 "addr-delta=%ld final-rwc=%ld "
				 "target=%s emit=%s\n",
				 bb->index, current.calls.size (),
				 (long) increment, (long) (increment * 2),
				 TARGET_XTT_TENSIX_WH ? "wh" :
				 TARGET_XTT_TENSIX_BH ? "bh" : "qsr",
				 emit ? "yes" : "no");
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
  GIMPLE_PASS, "rvtt_dst_iteration", OPTGROUP_OTHER, TV_NONE,
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
    return TODO_update_ssa;
  }
};

} /* anonymous namespace */

/* Pass factory for rvtt_dst_iteration, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_iteration (gcc::context *ctxt)
{
  return new pass_rvtt_dst_iteration (ctxt);
}

namespace {

/* The interleave runs after RVTT lowering has exposed the final typed
   SFPU operations, but before RTL expansion and register allocation.
   The fusion's TTINCRWC(+4) and address+2 form marks the group.  */

/* Is CALL the fused group marker -- a TTINCRWC advancing only the Dst
   counter by 4?  */

static bool
final_increment_p (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::ttincrwc
      || gimple_call_num_args (call) != 4)
    return false;
  return integer_zerop (gimple_call_arg (call, 0))
    && integer_cst_eq (gimple_call_arg (call, 1),
		       build_int_cst (TREE_TYPE (gimple_call_arg (call, 1)), 4))
    && integer_zerop (gimple_call_arg (call, 2))
    && integer_zerop (gimple_call_arg (call, 3));
}

/* The one-cycle dynamic-result operations are the MAD-subunit
   instructions, from the effect attributes in rvtt-cost.md.
   Operations without that attribute make the group ineligible.  */
static bool
dynamic_result_p (const rvtt_insn_data *insnd)
{
  return rvtt_builtin_subunit (insnd) == XTT_SU_MAD;
}

/* Return true if INSND may form the tail of an interleaved row: a
   ROUND- or STORE-subunit operation, or the sfpassign_lv value move.
   Tail operations stay in per-row order after the interleaved
   dynamic pairs (see make_interleaved_order).  */

static bool
drain_operation_p (const rvtt_insn_data *insnd)
{
  xtt_subunit_t subunit = rvtt_builtin_subunit (insnd);
  if (subunit == XTT_SU_ROUND || subunit == XTT_SU_STORE)
    return true;
  /* sfpassign is a compiler value-move placeholder, not an architectural
     operation; it has no late pattern carrying effect attributes, so its
     admission stays structural.  */
  return insnd->id == rvtt_insn_data::sfpassign_lv;
}

/* Late-pass counterpart of body_call_p: return true if CALL (insn
   data INSND) may appear in an interleave-candidate row -- a typed
   Dst access, or a side-effect-free, CC-neutral MAD-subunit or
   drain operation.  */

static bool
late_body_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (typed_dst_access_p (insnd))
    return true;
  return (dynamic_result_p (insnd) || drain_operation_p (insnd))
    && !insnd->has_side_effects (call) && !insnd->sets_cc (call);
}

/* Return true if rows A and B are isomorphic bodies one Dst step
   apart and safe to interleave: pairwise-equal opcodes, arities and
   scalar operands; typed addresses where B's is provably A's plus 2,
   in range, and A's is 4-aligned (so B's moved loads cannot alias
   A's store); vector operands matched through the paired-lhs map;
   and exactly one store per row.  */

static bool
late_pair_p (const std::vector<gcall *> &a,
	     const std::vector<gcall *> &b)
{
  if (a.empty () || a.size () != b.size ())
    return false;

  std::unordered_map<tree, tree> values;
  unsigned stores = 0;
  bool store_load_disjoint = true;
  for (unsigned i = 0; i != a.size (); ++i)
    {
      gcall *ca = a[i];
      gcall *cb = b[i];
      const rvtt_insn_data *ia = rvtt_get_insn_data (ca);
      const rvtt_insn_data *ib = rvtt_get_insn_data (cb);
      if (!ia || !ib || ia->id != ib->id
	  || gimple_call_num_args (ca) != gimple_call_num_args (cb))
	return false;

      unsigned addr_arg = ia->id == rvtt_insn_data::sfpload ? 1
	: ia->id == rvtt_insn_data::sfpstore ? 2 : ~0u;
      if (typed_dst_access_p (ia))
	{
	  /* Prove BASE+2 from BASE's established range.  The newly
	     inserted SSA name deliberately has no independent VRP cache
	     entry at this point.  */
	  if (!dst_access_legal_p (ca, 2))
	    return false;
	  tree aa = gimple_call_arg (ca, addr_arg);
	  tree ab = gimple_call_arg (cb, addr_arg);
	  if (!address_plus_p (aa, ab, 2))
	    return false;
	  /* Interleaving moves every row-B load before the row-A store.
	     Requiring row-A typed addresses to be 4-aligned makes their
	     residues 0 mod 4, while the exact +2 row-B rewrite has
	     residue 2 mod 4.  Thus no moved load can alias the earlier
	     row's store.  */
	  if (ia->id == rvtt_insn_data::sfpload
	      || ia->id == rvtt_insn_data::sfpstore)
	    {
	      bool aligned = address_aligned_p (aa, 4);
	      if (!aligned && dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "Dst-interleave mismatch: call=%u "
			 "address-alignment\n", i);
	      store_load_disjoint &= aligned;
	    }
	  stores += ia->id == rvtt_insn_data::sfpstore;
	}

      unsigned var_arg = ia->has_var () ? ia->var_arg () : ~0u;
      unsigned id_arg = ia->has_var () ? ia->id_arg () : ~0u;
      for (unsigned argno = 0; argno != gimple_call_num_args (ca); ++argno)
	{
	  if (argno == addr_arg || argno == var_arg || argno == id_arg)
	    continue;
	  tree va = gimple_call_arg (ca, argno);
	  tree vb = gimple_call_arg (cb, argno);
	  if (vector_value_p (va))
	    {
	      auto it = values.find (va);
	      if (it == values.end () || it->second != vb)
		return false;
	    }
	  else if (!operand_equal_p (va, vb, 0))
	    return false;
	}

      tree la = gimple_call_lhs (ca);
      tree lb = gimple_call_lhs (cb);
      if (!!la != !!lb)
	return false;
      if (vector_value_p (la))
	values.emplace (la, lb);
    }
  return stores == 1 && store_load_disjoint;
}

/* Return true if every scalar SSA argument of the calls in OPS is
   defined before INCREMENT: either earlier in INCREMENT's own block
   (PHIs allowed), or in a dominating block.  Interleaving moves the
   row-B calls up to the INCREMENT boundary, so any scalar input
   defined between the boundary and its use would be torn from its
   definition.  Vector arguments are exempt (their producers move as
   part of the same group).  */

static bool
scalar_defs_before_increment_p (const std::vector<gcall *> &ops,
				 gcall *increment)
{
  std::unordered_set<gimple *> before;
  basic_block bb = gimple_bb (increment);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == increment)
	break;
      before.insert (stmt);
    }

  for (gcall *call : ops)
    for (unsigned argno = 0; argno != gimple_call_num_args (call); ++argno)
      {
	tree arg = gimple_call_arg (call, argno);
	if (TREE_CODE (arg) != SSA_NAME || vector_value_p (arg)
	    || SSA_NAME_IS_DEFAULT_DEF (arg))
	  continue;
	gimple *def = SSA_NAME_DEF_STMT (arg);
	basic_block def_bb = gimple_bb (def);
	if (def_bb == bb)
	  {
	    if (gimple_code (def) != GIMPLE_PHI && !before.count (def))
	      return false;
	  }
	else
	  {
	    if (!dom_info_available_p (CDI_DOMINATORS))
	      calculate_dominance_info (CDI_DOMINATORS);
	    if (!def_bb || !dominated_by_p (CDI_DOMINATORS, bb, def_bb))
	      return false;
	  }
      }
  return true;
}

/* Return true if the candidate group is value-closed: every vector
   value defined by a call in OPS is consumed only by other members of
   OPS (or by INCREMENT).  A use escaping the group would observe the
   reordered schedule.  Debug statements are ignored.  */

static bool
closed_group_p (const std::vector<gcall *> &ops, gcall *increment)
{
  std::unordered_set<gimple *> members;
  for (gcall *call : ops)
    members.insert (call);
  members.insert (increment);
  for (gcall *call : ops)
    if (tree lhs = gimple_call_lhs (call))
      if (vector_value_p (lhs))
	{
	  imm_use_iterator iter;
	  gimple *use;
	  FOR_EACH_IMM_USE_STMT (use, iter, lhs)
	    if (!is_gimple_debug (use) && !members.count (use))
	      return false;
	}
  return true;
}

/* Build the interleaved schedule for the paired rows A and B into
   ORDER.  Each row must have the shape loads / dynamic-result body /
   drain tail ending in the row store; the result is A's loads, then
   B's loads, then the two dynamic bodies interleaved pairwise, then
   both drain tails.  Returns false (ORDER unusable) when either row
   does not have that shape.  *LOADS and *DYNAMIC_PAIRS return the
   combined load count and the per-row dynamic-body length for the
   dump line.  */

static bool
make_interleaved_order (const std::vector<gcall *> &a,
			const std::vector<gcall *> &b,
			std::vector<gcall *> &order,
			unsigned *loads, unsigned *dynamic_pairs)
{
  unsigned load_count = 0;
  while (load_count != a.size ()
	 && rvtt_get_insn_data (a[load_count])->id == rvtt_insn_data::sfpload)
    ++load_count;
  if (load_count < 2 || load_count == a.size ())
    return false;

  unsigned drain = load_count;
  while (drain != a.size () && dynamic_result_p (rvtt_get_insn_data (a[drain])))
    ++drain;
  if (drain == load_count || drain == a.size ())
    return false;
  for (unsigned i = drain; i != a.size (); ++i)
    if (!drain_operation_p (rvtt_get_insn_data (a[i])))
      return false;
  if (rvtt_get_insn_data (a.back ())->id != rvtt_insn_data::sfpstore)
    return false;

  order.reserve (a.size () + b.size ());
  order.insert (order.end (), a.begin (), a.begin () + load_count);
  order.insert (order.end (), b.begin (), b.begin () + load_count);
  for (unsigned i = load_count; i != drain; ++i)
    {
	order.push_back (a[i]);
	order.push_back (b[i]);
    }
  order.insert (order.end (), a.begin () + drain, a.end ());
  order.insert (order.end (), b.begin () + drain, b.end ());
  *loads = load_count * 2;
  *dynamic_pairs = drain - load_count;
  return true;
}

/* Commit the interleaving: strip the virtual operands from every call
   in OPS (their Dst accesses were proven disjoint, so the vdef/vuse
   chain no longer constrains them), then move the calls in ORDER, in
   sequence, to just before INCREMENT.  */

static void
apply_interleaved_order (const std::vector<gcall *> &ops,
			 const std::vector<gcall *> &order,
			 gcall *increment)
{
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

  gimple_stmt_iterator boundary = gsi_for_stmt (increment);
  for (gcall *call : order)
    {
      gimple_stmt_iterator from = gsi_for_stmt (call);
      gsi_move_before (&from, &boundary);
    }
}

/* Decide and (when everything proves out) perform the interleave of
   one candidate group OPS in BB, ending at INCREMENT.  REJECTED is the
   scan's poison flag (an unanalyzable statement inside the group).
   The group must split into two equal rows that pair instruction-for-
   instruction, be value-closed, and have its scalar inputs defined
   before the boundary; QSR targets prove but never emit.  Returns
   true iff the block was changed.  */

static bool
interleave_group (basic_block bb, std::vector<gcall *> &ops,
		  gcall *increment, bool rejected)
{
  if (rejected || ops.size () < 8 || (ops.size () & 1))
    return false;
  unsigned half = ops.size () / 2;
  std::vector<gcall *> a (ops.begin (), ops.begin () + half);
  std::vector<gcall *> b (ops.begin () + half, ops.end ());
  bool paired = late_pair_p (a, b);
  bool closed = paired && closed_group_p (ops, increment);
  bool scalar_defs = closed && scalar_defs_before_increment_p (ops, increment);
  if (!scalar_defs)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "Dst-interleave rejected: bb=%d paired=%d closed=%d "
		 "scalar-defs=%d\n", bb->index, paired, closed, scalar_defs);
    return false;
    }

  std::vector<gcall *> order;
  unsigned loads = 0;
  unsigned dynamic_pairs = 0;
  bool schedulable
    = make_interleaved_order (a, b, order, &loads, &dynamic_pairs);
  bool emit = schedulable && !TARGET_XTT_TENSIX_QSR;
  if (emit)
    apply_interleaved_order (ops, order, increment);
  if (dump_file)
    fprintf (dump_file,
	     "Dst-interleave candidate: bb=%d ops=%zu loads=%u "
	     "dynamic-pairs=%u target=%s emit=%s scalar-defs=retained\n",
	     bb->index, ops.size (), loads, dynamic_pairs,
	     TARGET_XTT_TENSIX_WH ? "wh" :
	     TARGET_XTT_TENSIX_BH ? "bh" : "qsr",
	     emit ? "yes" : "no");
  return emit;
}

/* Scan FN block by block for interleave candidates: runs of typed-Dst
   SFPU body calls delimited by final-increment calls.  Statements that
   are neither body calls nor synthesized opcode bookkeeping poison the
   current group when they could alias or order against it.  Returns
   true iff any group was interleaved.  */

static bool
interleave_function (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      std::vector<gcall *> ops;
      bool started = false;
      bool rejected = false;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  gcall *call = dyn_cast<gcall *> (stmt);
	  if (call && final_increment_p (call))
	    {
	      if (started)
		changed |= interleave_group (bb, ops, call, rejected);
	      ops.clear ();
	      started = false;
	      rejected = false;
	      continue;
	    }

	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  /* Synthesized instruction-buffer bookkeeping for dynamic typed
	     addresses is neither part of a row chain nor a side effect.  */
	  if (insnd && insnd->id == rvtt_insn_data::synth_opcode)
	    continue;
	  if (insnd && typed_dst_access_p (insnd))
	    started = true;
	  if (!started)
	    continue;
	  if (!call || !insnd || !late_body_call_p (call, insnd))
	    {
	      if (gimple_has_volatile_ops (stmt) || is_gimple_call (stmt)
		  || gimple_vuse (stmt) || gimple_vdef (stmt))
		rejected = true;
	      continue;
	    }
	  ops.push_back (call);
	}
    }
  return changed;
}

const pass_data pass_data_rvtt_dst_interleave = {
  GIMPLE_PASS, "rvtt_dst_interleave", OPTGROUP_OTHER, TV_NONE,
  PROP_cfg | PROP_ssa, 0, 0, 0, 0
};

class pass_rvtt_dst_interleave : public gimple_opt_pass
{
public:
  pass_rvtt_dst_interleave (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_dst_interleave, ctxt) {}
  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_dst_iteration_fusion;
  }
  unsigned execute (function *fn) final override
  {
    return interleave_function (fn)
      ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Pass factory for rvtt_dst_interleave, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_interleave (gcc::context *ctxt)
{
  return new pass_rvtt_dst_interleave (ctxt);
}

/* Row-pair interleave of rolled Dst row loops.

   The two passes above work on rows already written out straight-line.
   Most SFPU kernels instead process one row per trip of a counted loop,
   and when the row is a serial dependence chain (a polynomial, a
   Newton step) each producer whose consumer issues next exposes its
   result latency: SFPMAD needs two cycles, and a dependent instruction
   on the next cycle stalls (SFPMAD.md, "Instruction scheduling").
   Consecutive rows are independent, so the second row's chain can fill
   the first row's stalls.

   rvtt_dst_row_interleave unrolls such a loop by two and jams the
   copies: row B is row A with every typed Dst address raised by the row
   step, the two TTINCRWC(+S) become one TTINCRWC(+2S), the induction
   step doubles, and the rows are zipped so each statement of row A is
   followed by its row-B copy.  Only the order changes, and only where
   the rows are independent:

   - shape: an innermost single-block counted loop on Blackhole with an
     even constant trip count, one integral induction variable stepped by
     a constant and tested after the step, and a body of typed SFPU
     builtins closed by a single Dst-only TTINCRWC;
   - independence: no loop-carried or live-out row value, no scalar
     operand computed in the row, and Dst accessed only by typed
     no-increment loads and stores at constant addresses that stay in
     range after the rebase.  An access selects an aligned group of four
     rows and one column parity (address bit 1; SFPLOAD.md), so a row-B
     and a row-A access, one of them a store, must differ in parity or
     lie in different row groups of the same Dst view (Dst.md);
   - lane state: every CC writer belongs to an atom that ends with the
     all-lanes SFPENCC, atoms are never split or interleaved, and all
     lanes are enabled on the loop's entry edge, so every statement
     outside an atom runs with all lanes enabled in either order;
   - PRNG order: every SFPSTOCHRND advances the per-lane PRNG, so no
     row-B advance moves above a row-A advance.

   A literal constant load (an SFPLOADI chain) outside any atom is
   issued once for both rows.  When the doubled live set exceeds the
   LREG file, loop-invariant literal loads are re-issued inside the loop
   ahead of their first use, shortest in-loop span first; if the pair
   still does not fit, the loop is refused.

   The rewrite is made only if the issue-cycle model (result latencies
   from rvtt_builtin_result_latency, word counts from
   rvtt_replay_unroll_row_words) shows the zipped pair is faster than
   two rows back to back, and the function-wide pressure model
   (rvtt_pressure_compute) agrees the block still fits the LREG file;
   otherwise it is undone.

   Runs under -mtt-tensix-optimize-dst-row-interleave, after the combiner
   has formed the final SFPMAD chains and before the fused-group
   interleave above.  */

namespace {

/* What a row statement is, for the jam's legality.  */

enum row_kind
{
  ROW_PURE,	/* lane-local compute, no Dst/CC/config effect */
  ROW_DST,	/* typed SFPLOAD/SFPSTORE */
  ROW_CC	/* writes the lane-enable state */
};

struct row_stmt
{
  gcall *call;
  row_kind kind;
  bool prng = false;		/* advances the per-lane PRNG */
  int atom = -1;		/* index of the CC atom holding it */
  HOST_WIDE_INT address = 0;	/* ROW_DST: the literal Dst address */
  bool store = false;		/* ROW_DST: SFPSTORE */
  int view = 0;			/* ROW_DST: Dst view, 16 or 32 bits, 0 unknown */
  int words = 0;		/* delivered words */
  int latency = 0;		/* exposed result latency, in issue slots */
};

/* One scheduled statement of the pair: row A's original statement INDEX,
   row B's copy of it, or the in-loop materialization of evicted invariant
   INDEX.  */

enum pair_kind { PAIR_A, PAIR_B, PAIR_REMAT };

struct pair_item
{
  pair_kind kind;
  unsigned index;
};

/* A loop-invariant vector value the row reads, with its literal
   materialization chain when it has one (empty otherwise).  */

struct invariant_value
{
  tree name;
  std::vector<gcall *> chain;
  int words = 0;
  bool evicted = false;
};

class row_interleave
{
public:
  unsigned n_fired = 0;
  unsigned n_refused = 0;

  void process (class loop *loop);

private:
  class loop *loop = nullptr;
  basic_block bb = nullptr;
  edge entry = nullptr;
  gassign *step_stmt = nullptr;
  gcall *separator = nullptr;
  HOST_WIDE_INT row_step = 0;
  unsigned HOST_WIDE_INT trips = 0;
  std::vector<row_stmt> row;
  std::unordered_map<tree, unsigned> row_def;	/* SSA name -> row index */
  std::vector<invariant_value> invariants;
  std::unordered_map<tree, unsigned> invariant_index;
  std::vector<std::pair<unsigned, unsigned>> atoms;
  std::vector<bool> shared;	/* row B reuses row A's value */

  bool refuse (const char *name, const char *detail = nullptr);
  bool admit_shape ();
  bool admit_body ();
  bool admit_atoms ();
  bool entry_all_lanes_p () const;
  bool rows_independent_p () const;
  void find_shared_materializations ();
  unsigned unit_end (unsigned first) const;
  std::vector<pair_item> zip_order () const;
  unsigned source (pair_kind kind, unsigned def) const;
  int peak_pressure (const std::vector<pair_item> &order) const;
  int cycles (const std::vector<pair_item> &order) const;
  int row_cycles () const;
  void place_remats (std::vector<pair_item> &order) const;
  bool evict_for_pressure (std::vector<pair_item> &order);
  bool apply (const std::vector<pair_item> &order);
};

bool
row_interleave::refuse (const char *name, const char *detail)
{
  ++n_refused;
  rvtt_refuse_by_name (name, dump_file,
		       "Dst-row interleave refused: loop %d (%s)",
		       loop->num, name);
  if (dump_file)
    {
      if (detail)
	fprintf (dump_file, ": %s", detail);
      fprintf (dump_file, "\n");
    }
  return false;
}

/* The loop is an innermost single block with one entry edge, a self
   latch and one exit tested by the block's final condition; its only
   non-virtual PHI is an integral induction variable stepped by a
   constant, whose stepped value feeds only the PHI and the exit test;
   and the trip count is a proven even constant.  */

bool
row_interleave::admit_shape ()
{
  if (!TARGET_XTT_TENSIX_BH)
    return refuse ("dst-row-interleave-bh-only");
  if (loop->inner || loop->num_nodes != 1)
    return refuse ("dst-row-interleave-loop-shape", "not a single block");
  bb = loop->header;
  if (EDGE_COUNT (bb->preds) != 2 || EDGE_COUNT (bb->succs) != 2)
    return refuse ("dst-row-interleave-loop-shape", "edges");
  edge e;
  edge_iterator ei;
  edge latch = nullptr;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == bb)
      latch = e;
    else
      entry = e;
  if (!latch || !entry || (entry->flags & EDGE_ABNORMAL))
    return refuse ("dst-row-interleave-loop-shape", "latch");

  if (!rvtt_loop_trips_gimple (loop, &trips))
    return refuse ("dst-row-interleave-trips-unproven");
  if (trips < 2 || trips % 2)
    return refuse ("dst-row-interleave-trips-odd");

  gcond *cond = safe_dyn_cast <gcond *> (gsi_stmt (gsi_last_bb (bb)));
  if (!cond)
    return refuse ("dst-row-interleave-loop-shape", "no exit test");

  gphi *iv = nullptr;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (virtual_operand_p (res))
	continue;
      if (iv || !INTEGRAL_TYPE_P (TREE_TYPE (res)))
	return refuse ("dst-row-interleave-loop-carried");
      iv = phi;
    }
  if (!iv)
    return refuse ("dst-row-interleave-loop-shape", "no induction variable");

  tree res = gimple_phi_result (iv);
  tree next = PHI_ARG_DEF_FROM_EDGE (iv, latch);
  step_stmt = TREE_CODE (next) == SSA_NAME
	      ? dyn_cast <gassign *> (SSA_NAME_DEF_STMT (next)) : nullptr;
  if (!step_stmt || gimple_bb (step_stmt) != bb
      || (gimple_assign_rhs_code (step_stmt) != PLUS_EXPR
	  && gimple_assign_rhs_code (step_stmt) != MINUS_EXPR)
      || gimple_assign_rhs1 (step_stmt) != res
      || TREE_CODE (gimple_assign_rhs2 (step_stmt)) != INTEGER_CST)
    return refuse ("dst-row-interleave-loop-shape", "induction step");

  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  if (!((lhs == next && TREE_CODE (rhs) == INTEGER_CST)
	|| (rhs == next && TREE_CODE (lhs) == INTEGER_CST)))
    return refuse ("dst-row-interleave-loop-shape", "exit test");

  use_operand_p use;
  imm_use_iterator it;
  FOR_EACH_IMM_USE_FAST (use, it, res)
    if (USE_STMT (use) != step_stmt && !is_gimple_debug (USE_STMT (use)))
      return refuse ("dst-row-interleave-scalar-dependence",
		     "induction variable");
  FOR_EACH_IMM_USE_FAST (use, it, next)
    if (USE_STMT (use) != iv && USE_STMT (use) != cond
	&& !is_gimple_debug (USE_STMT (use)))
      return refuse ("dst-row-interleave-scalar-dependence",
		     "induction variable");
  return true;
}

/* Is CALL the row separator: TTINCRWC advancing only the Dst counter by
   a positive constant?  Sets *STEP.  */

static bool
row_separator_p (gcall *call, HOST_WIDE_INT *step)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::ttincrwc
      || gimple_call_num_args (call) != 4)
    return false;
  for (unsigned i : { 0u, 2u, 3u })
    if (!integer_zerop (gimple_call_arg (call, i)))
      return false;
  tree dst = gimple_call_arg (call, 1);
  if (!tree_fits_shwi_p (dst) || tree_to_shwi (dst) <= 0)
    return false;
  *step = tree_to_shwi (dst);
  return true;
}

/* Every SFPSTOCHRND form advances the per-lane PRNG.  */

static bool
prng_advance_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpstochrnd:
    case rvtt_insn_data::sfpstochrnd_lv:
    case rvtt_insn_data::sfpstochrnd_descale_i:
    case rvtt_insn_data::sfpstochrnd_descale_i_lv:
    case rvtt_insn_data::sfpstochrnd_descale_v:
    case rvtt_insn_data::sfpstochrnd_descale_v_lv:
      return true;
    default:
      return false;
    }
}

/* Read the literal address and the Dst view of the typed access CALL (an
   SFPSTORE when STORE).  The view follows the Mod0 data-type field
   (SFPLOAD.md and SFPSTORE.md, "Functional model"): MOD0_FMT_SRCB
   resolves from configuration and stays unknown.  Fails for the
   no-increment-mode check, a runtime address or Mod0, and the formats
   with effects beyond the addressed rows: INT32_ALL steps the stack
   counter, and the LO16_ONLY/HI16_ONLY loads merge the old register.  */

static bool
dst_access_view (gcall *call, bool store, HOST_WIDE_INT *address,
		 int *view)
{
  unsigned addr_arg = store ? 2 : 1;
  unsigned mod_arg = store ? 5 : 4;
  tree addr = gimple_call_arg (call, addr_arg);
  tree mod = gimple_call_arg (call, mod_arg);
  tree mode = gimple_call_arg (call, mod_arg + 1);
  if (!tree_fits_shwi_p (addr) || tree_to_shwi (addr) < 0
      || !tree_fits_uhwi_p (mod) || !tree_fits_shwi_p (mode)
      || tree_to_shwi (mode) != rvtt_no_increment_address_mode ())
    return false;
  *address = tree_to_shwi (addr);
  switch (tree_to_uhwi (mod))
    {
    case 0:
      *view = 0;
      return true;
    case 1: case 2: case 5: case 6: case 8: case 13:
      *view = 16;
      return true;
    case 3: case 4: case 12:
      *view = 32;
      return true;
    case 7: case 9:
      *view = store ? 32 : 16;
      return true;
    case 11:
      *view = 16;
      return true;
    case 14: case 15:
      *view = 16;
      return store;
    default:
      return false;
    }
}

/* Is LAST the end of a chain of SFPLOADI words whose operands are all
   literals, each later word merging into its predecessor's value?  Fills
   CHAIN in program order.  */

static bool
literal_chain_p (gcall *last, std::vector<gcall *> *chain)
{
  chain->clear ();
  for (gcall *call = last; call; )
    {
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (!insnd || (insnd->id != rvtt_insn_data::sfploadi
		     && insnd->id != rvtt_insn_data::sfploadi_lv))
	return false;
      gcall *prev = nullptr;
      for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
	{
	  tree arg = gimple_call_arg (call, i);
	  if (TREE_CODE (arg) == INTEGER_CST || rvtt_canonical_buffer_arg_p (arg))
	    continue;
	  if (TREE_CODE (arg) != SSA_NAME || !vector_value_p (arg) || prev
	      || insnd->id != rvtt_insn_data::sfploadi_lv
	      || !has_single_use (arg))
	    return false;
	  prev = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (arg));
	  if (!prev || gimple_bb (prev) != gimple_bb (call))
	    return false;
	}
      chain->insert (chain->begin (), call);
      call = prev;
    }
  return true;
}

/* Scan of the body: classify every statement, collect the invariant
   vector inputs, and prove the row's values stay inside the row.  */

bool
row_interleave::admit_body ()
{
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_nop_p (stmt) || gimple_code (stmt) == GIMPLE_COND
	  || stmt == step_stmt)
	continue;
      gcall *call = dyn_cast <gcall *> (stmt);
      const rvtt_insn_data *insnd = call ? rvtt_get_insn_data (call) : nullptr;
      if (!insnd)
	return refuse ("dst-row-interleave-foreign-stmt",
		       gimple_code_name[gimple_code (stmt)]);
      if (separator)
	return refuse ("dst-row-interleave-row-step-shape",
		       "word after the row step");
      if (row_separator_p (call, &row_step))
	{
	  separator = call;
	  continue;
	}

      row_stmt rs;
      rs.call = call;
      int words = rvtt_replay_unroll_row_words (insnd);
      rs.words = words < 0 ? 1 : words;
      rs.latency = MAX (rvtt_builtin_result_latency (insnd), 0);
      if (typed_dst_access_p (insnd))
	{
	  rs.kind = ROW_DST;
	  rs.store = insnd->id == rvtt_insn_data::sfpstore;
	  if (!dst_access_view (call, rs.store, &rs.address, &rs.view))
	    return refuse ("dst-row-interleave-dst-access-unproven",
			   insnd->name);
	}
      else if (rvtt_cc_writer_id_p (insnd->id) || insnd->sets_cc (call))
	rs.kind = ROW_CC;
      else if (rvtt_dst_inert_compute_id_p (insnd->id)
	       && insnd->id != rvtt_insn_data::synth_opcode)
	{
	  rs.kind = ROW_PURE;
	  rs.prng = prng_advance_p (insnd);
	  /* A read of an allocatable LREG observes raw architectural
	     state rather than a row value.  */
	  if (insnd->id == rvtt_insn_data::sfpreadlreg)
	    {
	      tree lhs = gimple_call_lhs (call);
	      if (!lhs || rvtt_pressure_tracked_p (lhs))
		return refuse ("dst-row-interleave-foreign-stmt",
			       insnd->name);
	    }
	}
      else
	return refuse ("dst-row-interleave-foreign-stmt", insnd->name);

      for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
	{
	  tree arg = gimple_call_arg (call, i);
	  if (TREE_CODE (arg) != SSA_NAME)
	    continue;
	  gimple *def = SSA_NAME_DEF_STMT (arg);
	  if (gimple_bb (def) == bb)
	    {
	      if (!row_def.count (arg))
		return refuse (gimple_code (def) == GIMPLE_PHI
			       ? "dst-row-interleave-loop-carried"
			       : "dst-row-interleave-scalar-dependence");
	      continue;
	    }
	  if (!vector_value_p (arg) || invariant_index.count (arg))
	    continue;
	  invariant_index.emplace (arg, invariants.size ());
	  invariant_value inv;
	  inv.name = arg;
	  gcall *last = dyn_cast <gcall *> (def);
	  if (last && literal_chain_p (last, &inv.chain))
	    for (gcall *c : inv.chain)
	      inv.words += MAX (rvtt_replay_unroll_row_words
				(rvtt_get_insn_data (c)), 1);
	  else
	    inv.chain.clear ();
	  invariants.push_back (inv);
	}
      if (tree lhs = gimple_call_lhs (call))
	{
	  if (TREE_CODE (lhs) != SSA_NAME)
	    return refuse ("dst-row-interleave-foreign-stmt", "memory result");
	  use_operand_p use;
	  imm_use_iterator it;
	  FOR_EACH_IMM_USE_FAST (use, it, lhs)
	    {
	      gimple *user = USE_STMT (use);
	      if (!is_gimple_debug (user)
		  && (gimple_bb (user) != bb
		      || gimple_code (user) == GIMPLE_PHI))
		return refuse ("dst-row-interleave-live-out");
	    }
	  row_def.emplace (lhs, row.size ());
	}
      row.push_back (rs);
    }

  if (!separator)
    return refuse ("dst-row-interleave-row-step-shape", "no row step");
  if (row.size () < 2)
    return refuse ("dst-row-interleave-row-step-shape", "row too short");

  /* Row B's addresses must still fit the encoding.  */
  for (const row_stmt &rs : row)
    if (rs.kind == ROW_DST)
      {
	unsigned bits = dst_address_bits (rvtt_get_insn_data (rs.call));
	if (rs.address + row_step > (HOST_WIDE_INT_1 << bits) - 1)
	  return refuse ("dst-row-interleave-dst-access-unproven",
			 "address range");
      }
  return true;
}

/* CC atoms: from a CC writer through the next all-lanes SFPENCC.  A
   writer left open at the row end would carry lane state into the next
   row.  */

bool
row_interleave::admit_atoms ()
{
  int open = -1;
  for (unsigned i = 0; i != row.size (); ++i)
    {
      row_stmt &rs = row[i];
      if (rs.kind == ROW_CC && open < 0)
	open = i;
      if (open < 0)
	continue;
      rs.atom = atoms.size ();
      if (rs.kind == ROW_CC
	  && rvtt_encc_all_lanes_call_p (rs.call, rvtt_get_insn_data (rs.call)))
	{
	  atoms.emplace_back (open, i);
	  open = -1;
	}
    }
  if (open >= 0)
    return refuse ("dst-row-interleave-cc-unclosed");
  return true;
}

/* Both lane facts must hold on every incoming path; neither is an entry
   ABI promise, and a flags reset alone cannot clear LaneConfig ROW_MASK.  */

bool
row_interleave::entry_all_lanes_p () const
{
  rvtt_gimple_lane_facts facts (cfun);
  return facts.block_out (entry->src) == RVTT_LANE_ALL;
}

/* Every row-B Dst access is disjoint from every row-A access when one of
   the two is a store.  */

bool
row_interleave::rows_independent_p () const
{
  for (const row_stmt &a : row)
    for (const row_stmt &b : row)
      if (a.kind == ROW_DST && b.kind == ROW_DST && (a.store || b.store))
	{
	  HOST_WIDE_INT delta = b.address + row_step - a.address;
	  /* Opposite column parity separates them in either view.  */
	  if (((delta % 4) + 4) % 4 == 2)
	    continue;
	  /* Distinct four-row groups of one known view.  */
	  if (a.view && a.view == b.view && (delta <= -4 || delta >= 4))
	    continue;
	  return false;
	}
  return true;
}

/* Row B can reuse row A's value of every literal materialization outside
   an atom: both run with all lanes enabled and produce the same value,
   and the zip issues row A's chain first.  */

void
row_interleave::find_shared_materializations ()
{
  shared.assign (row.size (), false);
  for (unsigned i = 0; i != row.size (); ++i)
    {
      tree lhs = gimple_call_lhs (row[i].call);
      if (row[i].atom >= 0 || !lhs || !vector_value_p (lhs))
	continue;
      std::vector<gcall *> chain;
      if (!literal_chain_p (row[i].call, &chain))
	continue;
      bool outside = true;
      for (gcall *c : chain)
	{
	  auto rd = row_def.find (gimple_call_lhs (c));
	  outside &= rd != row_def.end () && row[rd->second].atom < 0;
	}
      if (outside)
	for (gcall *c : chain)
	  shared[row_def.at (gimple_call_lhs (c))] = true;
    }
}

/* The last statement of the unit starting at FIRST: its atom's end, or
   FIRST itself.  */

unsigned
row_interleave::unit_end (unsigned first) const
{
  return row[first].atom >= 0 ? atoms[row[first].atom].second : first;
}

/* The zip: each row-A unit (a statement, or a whole atom) followed by its
   row-B copy, unless the copy advances the PRNG while row A still has an
   advance to issue.  Shared materializations are issued once.  */

std::vector<pair_item>
row_interleave::zip_order () const
{
  int last_prng = -1;
  for (unsigned i = 0; i != row.size (); ++i)
    if (row[i].prng)
      last_prng = i;

  std::vector<pair_item> order;
  auto emit_unit = [&] (pair_kind kind, unsigned first)
    {
      unsigned last = unit_end (first);
      for (unsigned i = first; i <= last; ++i)
	if (kind == PAIR_A || !shared[i])
	  order.push_back ({ kind, i });
      return last + 1;
    };
  unsigned b = 0;
  for (unsigned a = 0; a != row.size (); )
    {
      a = emit_unit (PAIR_A, a);
      while (b < a)
	{
	  bool prng = false;
	  for (unsigned i = b; i <= unit_end (b); ++i)
	    prng |= row[i].prng;
	  if (prng && (int) a <= last_prng)
	    break;
	  b = emit_unit (PAIR_B, b);
	}
    }
  while (b != row.size ())
    b = emit_unit (PAIR_B, b);
  return order;
}

/* Which copy of row value DEF a statement of KIND reads: row B reads its
   own copy, except of a shared materialization.  */

unsigned
row_interleave::source (pair_kind kind, unsigned def) const
{
  return kind == PAIR_B && !shared[def] ? 1 : 0;
}

/* LREGs NAME occupies while live: an SFPSWAP pair against a constant
   register drops the constant half.  */

static unsigned
lreg_width (tree name)
{
  switch (TYPE_MODE (TREE_TYPE (name)))
    {
    case E_XTT32SImode:
      return 1;
    case E_XTT64SImode:
      if (gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (name)))
	{
	  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
	  if (insnd && insnd->id == rvtt_insn_data::sfpswap
	      && (!rvtt_pressure_tracked_p (gimple_call_arg (def, 0))
		  || !rvtt_pressure_tracked_p (gimple_call_arg (def, 1))))
	    return 1;
	}
      return 2;
    case E_XTT128SImode:
      return 4;
    default:
      return SFPU_REG_NUM;
    }
}

/* Peak simultaneously-live LREG demand of the body in ORDER, taken at
   each statement over the values live into it and the values live out of
   it (an operand read for the last time can hand its register to the
   result): each row value from its definition to its last read (row B's
   reads of a shared value extend row A's), every kept tracked invariant
   throughout, an evicted invariant from its in-loop materialization to
   the pair's last read, plus the CC-transient charge of each CC word.  */

int
row_interleave::peak_pressure (const std::vector<pair_item> &order) const
{
  int n = order.size ();
  /* One live range per value: [def, last], def -1 for the body entry.  */
  struct range { int def, last; unsigned width; };
  std::vector<range> ranges;
  std::vector<int> slot[2];
  slot[0].assign (row.size (), -1);
  slot[1].assign (row.size (), -1);
  std::vector<int> remat_slot (invariants.size (), -1);
  for (unsigned v = 0; v != invariants.size (); ++v)
    if (rvtt_pressure_tracked_p (invariants[v].name)
	&& !invariants[v].evicted)
      ranges.push_back ({ -1, n, lreg_width (invariants[v].name) });
  for (int p = 0; p != n; ++p)
    {
      const pair_item &it = order[p];
      if (it.kind == PAIR_REMAT)
	{
	  remat_slot[it.index] = ranges.size ();
	  ranges.push_back ({ p, p, lreg_width (invariants[it.index].name) });
	  continue;
	}
      gcall *call = row[it.index].call;
      for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
	{
	  tree arg = gimple_call_arg (call, i);
	  auto rd = row_def.find (arg);
	  if (rd != row_def.end ())
	    {
	      int r = slot[source (it.kind, rd->second)][rd->second];
	      if (r >= 0)
		ranges[r].last = p;
	      continue;
	    }
	  auto iv = invariant_index.find (arg);
	  if (iv != invariant_index.end () && remat_slot[iv->second] >= 0)
	    ranges[remat_slot[iv->second]].last = p;
	}
      tree lhs = gimple_call_lhs (call);
      if (lhs && rvtt_pressure_tracked_p (lhs))
	{
	  slot[it.kind == PAIR_B][it.index] = ranges.size ();
	  ranges.push_back ({ p, p, lreg_width (lhs) });
	}
    }

  int peak = 0;
  for (int p = 0; p != n; ++p)
    {
      int in = 0, out = 0;
      for (const range &r : ranges)
	{
	  if (r.def < p && p <= r.last)
	    in += r.width;
	  if (r.def <= p && (p < r.last || r.def == r.last))
	    out += r.width;
	}
      int live = MAX (in, out);
      if (order[p].kind != PAIR_REMAT)
	live += rvtt_pressure_cc_transient (row[order[p].index].call);
      peak = MAX (peak, live);
    }
  return peak;
}

/* Issue-cycle model of ORDER on the in-order single-issue SFPU: a word
   issues once its vector operands are ready, occupies its delivered
   words, and makes its result ready RESULT_LATENCY slots after that.
   Rows share no value across trips, so one pass is the steady state.  */

int
row_interleave::cycles (const std::vector<pair_item> &order) const
{
  std::vector<int> ready[2];
  ready[0].assign (row.size (), 0);
  ready[1].assign (row.size (), 0);
  int t = 0;
  for (const pair_item &it : order)
    {
      if (it.kind == PAIR_REMAT)
	{
	  t += invariants[it.index].words;
	  continue;
	}
      const row_stmt &rs = row[it.index];
      int start = t;
      for (unsigned i = 0; i != gimple_call_num_args (rs.call); ++i)
	{
	  auto rd = row_def.find (gimple_call_arg (rs.call, i));
	  if (rd != row_def.end ())
	    start = MAX (start, ready[source (it.kind, rd->second)][rd->second]);
	}
      t = start + rs.words;
      ready[it.kind == PAIR_B][it.index] = rs.words ? t + rs.latency : start;
    }
  return t;
}

/* Two rows issued back to back, as in the original loop.  */

int
row_interleave::row_cycles () const
{
  std::vector<pair_item> order;
  for (unsigned i = 0; i != row.size (); ++i)
    order.push_back ({ PAIR_A, i });
  return 2 * cycles (order);
}

/* Place each evicted invariant's materialization just ahead of the unit
   holding its first read in ORDER, so it never lands inside an atom.  */

void
row_interleave::place_remats (std::vector<pair_item> &order) const
{
  order.erase (std::remove_if (order.begin (), order.end (),
			       [] (const pair_item &it)
			       { return it.kind == PAIR_REMAT; }),
	       order.end ());
  for (unsigned v = 0; v != invariants.size (); ++v)
    {
      if (!invariants[v].evicted)
	continue;
      for (unsigned p = 0; p != order.size (); ++p)
	{
	  if (order[p].kind == PAIR_REMAT)
	    continue;
	  gcall *call = row[order[p].index].call;
	  bool reads = false;
	  for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
	    reads |= gimple_call_arg (call, i) == invariants[v].name;
	  if (!reads)
	    continue;
	  int atom = row[order[p].index].atom;
	  unsigned at = p;
	  while (atom >= 0 && at && order[at - 1].kind == order[p].kind
		 && row[order[at - 1].index].atom == atom)
	    --at;
	  order.insert (order.begin () + at, { PAIR_REMAT, v });
	  break;
	}
    }
}

/* Evict literal invariants, shortest in-loop span first, until the pair
   fits the LREG file.  */

bool
row_interleave::evict_for_pressure (std::vector<pair_item> &order)
{
  while ((unsigned) peak_pressure (order) > rvtt_pressure_capacity ())
    {
      int best = -1;
      int best_span = INT_MAX;
      for (unsigned v = 0; v != invariants.size (); ++v)
	{
	  if (invariants[v].evicted || invariants[v].chain.empty ()
	      || !rvtt_pressure_tracked_p (invariants[v].name))
	    continue;
	  int first = -1, last = -1;
	  for (unsigned p = 0; p != order.size (); ++p)
	    {
	      if (order[p].kind == PAIR_REMAT)
		continue;
	      gcall *call = row[order[p].index].call;
	      for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
		if (gimple_call_arg (call, i) == invariants[v].name)
		  {
		    if (first < 0)
		      first = p;
		    last = p;
		  }
	    }
	  if (first >= 0 && last - first < best_span)
	    {
	      best = v;
	      best_span = last - first;
	    }
	}
      if (best < 0)
	return false;
      invariants[best].evicted = true;
      place_remats (order);
    }
  return true;
}

/* Rewrite the loop to ORDER.  Returns false, with the loop restored
   exactly, when the function-wide pressure model rejects the result.  */

bool
row_interleave::apply (const std::vector<pair_item> &order)
{
  rvtt_pressure_model before;
  rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &before);
  bool over_before = bitmap_bit_p (before.over_bbs, bb->index);

  std::vector<gimple *> original;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    if (gimple_code (gsi_stmt (gsi)) != GIMPLE_LABEL)
      original.push_back (gsi_stmt (gsi));

  std::vector<gcall *> created;
  std::vector<std::pair<gcall *, std::pair<unsigned, tree>>> rewired;
  std::unordered_map<tree, tree> partner;	/* row-A name -> row-B name */
  std::vector<tree> remat_name (invariants.size (), NULL_TREE);
  gimple_stmt_iterator at = gsi_for_stmt (separator);

  /* A copy of FROM with a fresh result; its virtual operands are
     renamed with the rest of the function after the commit.  */
  auto fresh = [] (gcall *from)
    {
      gcall *copy = as_a <gcall *> (gimple_copy (from));
      if (gimple_vuse (from))
	gimple_set_vuse (copy, gimple_vop (cfun));
      if (gimple_vdef (from))
	gimple_set_vdef (copy, gimple_vop (cfun));
      if (tree lhs = gimple_call_lhs (from))
	gimple_call_set_lhs (copy, make_ssa_name (TREE_TYPE (lhs), copy));
      return copy;
    };
  /* The value KIND's statement reads for operand ARG.  */
  auto operand = [&] (pair_kind kind, tree arg) -> tree
    {
      auto iv = invariant_index.find (arg);
      if (iv != invariant_index.end ())
	return invariants[iv->second].evicted ? remat_name[iv->second] : arg;
      auto rd = row_def.find (arg);
      if (rd != row_def.end () && source (kind, rd->second))
	return partner.at (arg);
      return arg;
    };

  for (const pair_item &it : order)
    {
      if (it.kind == PAIR_REMAT)
	{
	  tree prev = NULL_TREE;
	  for (gcall *c : invariants[it.index].chain)
	    {
	      gcall *copy = fresh (c);
	      for (unsigned i = 0; i != gimple_call_num_args (copy); ++i)
		if (TREE_CODE (gimple_call_arg (copy, i)) == SSA_NAME)
		  gimple_call_set_arg (copy, i, prev);
	      gsi_insert_before (&at, copy, GSI_SAME_STMT);
	      update_stmt (copy);
	      created.push_back (copy);
	      prev = gimple_call_lhs (copy);
	    }
	  remat_name[it.index] = prev;
	  continue;
	}

      gcall *call = row[it.index].call;
      if (it.kind == PAIR_A)
	{
	  gimple_stmt_iterator from = gsi_for_stmt (call);
	  gsi_move_before (&from, &at);
	  for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
	    {
	      tree arg = gimple_call_arg (call, i);
	      tree to = operand (PAIR_A, arg);
	      if (to != arg)
		{
		  rewired.push_back ({ call, { i, arg } });
		  gimple_call_set_arg (call, i, to);
		}
	    }
	  update_stmt (call);
	  continue;
	}

      gcall *copy = fresh (call);
      for (unsigned i = 0; i != gimple_call_num_args (copy); ++i)
	gimple_call_set_arg (copy, i,
			     operand (PAIR_B, gimple_call_arg (call, i)));
      if (row[it.index].kind == ROW_DST)
	{
	  unsigned arg = row[it.index].store ? 2 : 1;
	  tree address = gimple_call_arg (copy, arg);
	  gimple_call_set_arg (copy, arg,
			       build_int_cst (TREE_TYPE (address),
					      row[it.index].address
					      + row_step));
	}
      gsi_insert_before (&at, copy, GSI_SAME_STMT);
      update_stmt (copy);
      created.push_back (copy);
      if (tree lhs = gimple_call_lhs (call))
	partner.emplace (lhs, gimple_call_lhs (copy));
    }

  tree old_step = gimple_call_arg (separator, 1);
  gimple_call_set_arg (separator, 1,
		       build_int_cst (TREE_TYPE (old_step), 2 * row_step));
  update_stmt (separator);
  tree old_iv_step = gimple_assign_rhs2 (step_stmt);
  gimple_assign_set_rhs2 (step_stmt,
			  wide_int_to_tree (TREE_TYPE (old_iv_step),
					    wi::to_wide (old_iv_step) * 2));
  update_stmt (step_stmt);

  rvtt_pressure_model after;
  rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &after);
  if (over_before || !bitmap_bit_p (after.over_bbs, bb->index))
    return true;

  for (unsigned k = created.size (); k--;)
    {
      gcall *c = created[k];
      gimple_set_vuse (c, NULL_TREE);
      gimple_set_vdef (c, NULL_TREE);
      gimple_stmt_iterator gsi = gsi_for_stmt (c);
      gsi_remove (&gsi, true);
      if (tree lhs = gimple_call_lhs (c))
	release_ssa_name (lhs);
    }
  for (auto &rw : rewired)
    {
      gimple_call_set_arg (rw.first, rw.second.first, rw.second.second);
      update_stmt (rw.first);
    }
  gimple_stmt_iterator tail = gsi_last_bb (bb);
  for (gimple *s : original)
    if (s != gsi_stmt (tail))
      {
	gimple_stmt_iterator from = gsi_for_stmt (s);
	gsi_move_before (&from, &tail);
      }
  gimple_call_set_arg (separator, 1, old_step);
  update_stmt (separator);
  gimple_assign_set_rhs2 (step_stmt, old_iv_step);
  update_stmt (step_stmt);
  return false;
}

void
row_interleave::process (class loop *l)
{
  loop = l;
  bb = nullptr;
  entry = nullptr;
  step_stmt = nullptr;
  separator = nullptr;
  row_step = 0;
  row.clear ();
  row_def.clear ();
  invariants.clear ();
  invariant_index.clear ();
  atoms.clear ();

  if (!admit_shape () || !admit_body () || !admit_atoms ())
    return;
  if (!entry_all_lanes_p ())
    {
      refuse ("dst-row-interleave-entry-cc-unproven");
      return;
    }
  if (!rows_independent_p ())
    {
      refuse ("dst-row-interleave-dst-alias");
      return;
    }

  find_shared_materializations ();
  std::vector<pair_item> order = zip_order ();
  int zip_peak = peak_pressure (order);
  if (!evict_for_pressure (order))
    {
      if (dump_file)
	fprintf (dump_file, "Dst-row interleave: loop %d pair peak %d "
		 "exceeds the %u-LREG file\n", loop->num, zip_peak,
		 rvtt_pressure_capacity ());
      refuse ("dst-row-interleave-pressure-exceeded");
      return;
    }
  unsigned evicted = 0;
  for (const invariant_value &inv : invariants)
    evicted += inv.evicted;
  int shared_words = 0;
  for (unsigned i = 0; i != row.size (); ++i)
    shared_words += shared[i] ? row[i].words : 0;

  int before = row_cycles ();
  int after = cycles (order);
  if (dump_file)
    fprintf (dump_file, "Dst-row interleave: loop %d trips "
	     HOST_WIDE_INT_PRINT_UNSIGNED " row-step %d stmts %zu "
	     "atoms %zu pair-cycles %d -> %d shared-words %d evicted %u "
	     "peak %d\n", loop->num, trips, (int) row_step, row.size (),
	     atoms.size (), before, after, shared_words, evicted,
	     peak_pressure (order));
  if (after >= before)
    {
      refuse ("dst-row-interleave-no-benefit");
      return;
    }
  if (!apply (order))
    {
      refuse ("dst-row-interleave-pressure-exceeded",
	      "function-wide model");
      return;
    }

  /* The body's debug binds now precede the definitions they name.  */
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *dbg = gsi_stmt (gsi);
      if (gimple_debug_bind_p (dbg) && gimple_debug_bind_has_value_p (dbg))
	{
	  gimple_debug_bind_reset_value (dbg);
	  update_stmt (dbg);
	}
    }
  /* An evicted invariant read only in the loop leaves its entry
     materialization dead.  */
  for (const invariant_value &inv : invariants)
    if (inv.evicted)
      for (unsigned k = inv.chain.size (); k--;)
	{
	  gcall *c = inv.chain[k];
	  if (!has_zero_uses (gimple_call_lhs (c)))
	    break;
	  gimple_stmt_iterator gsi = gsi_for_stmt (c);
	  if (MAY_HAVE_DEBUG_BIND_STMTS)
	    insert_debug_temps_for_defs (&gsi);
	  if (gimple_vdef (c))
	    unlink_stmt_vdef (c);
	  gsi_remove (&gsi, true);
	  release_defs (c);
	}
  free_numbers_of_iterations_estimates (loop);
  ++n_fired;
  if (dump_file)
    fprintf (dump_file, "Dst-row interleave: loop %d rows paired, "
	     "row step %d -> %d\n", loop->num, (int) row_step,
	     (int) (2 * row_step));
}

const pass_data pass_data_rvtt_dst_row_interleave = {
  GIMPLE_PASS, "rvtt_dst_row_interleave", OPTGROUP_OTHER, TV_NONE,
  PROP_cfg | PROP_ssa, 0, 0, 0, 0
};

class pass_rvtt_dst_row_interleave : public gimple_opt_pass
{
public:
  pass_rvtt_dst_row_interleave (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_dst_row_interleave, ctxt) {}
  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && optimize > 0
      && riscv_tt_opt_dst_row_interleave;
  }
  unsigned execute (function *fn) final override
  {
    row_interleave ctx;
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    for (auto loop : loops_list (fn, LI_ONLY_INNERMOST))
      ctx.process (loop);
    loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file, "Dst-row interleave: fires=%u refusals=%u\n",
	       ctx.n_fired, ctx.n_refused);
    if (!ctx.n_fired)
      return 0;
    mark_virtual_operands_for_renaming (fn);
    return TODO_update_ssa_only_virtuals;
  }
};

} /* anonymous namespace */

/* Pass factory for rvtt_dst_row_interleave, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_row_interleave (gcc::context *ctxt)
{
  return new pass_rvtt_dst_row_interleave (ctxt);
}

/* Row look-ahead of Dst row sequences.

   A row that loads its input from Dst and then runs a short dependence
   chain exposes result latency at its head (the first operation waits
   for the value it just loaded and processed) and at its tail (the
   store waits for the last SFPMAD).  The next row's input load, and the
   lane-local operations that depend only on it, are independent of the
   current row, so issuing them in the current row's stall slots hides
   that latency without changing what any row computes.

   rvtt_dst_row_lookahead moves the leading part of each row -- its
   typed Dst loads and the lane-local operations fed only by them -- up
   into the previous row, ahead of the TTINCRWC that separates the two
   rows, raising each moved load's address by the row step so it still
   reads the row it read before.  It works in two places:

   - straight-line rows (a row loop the unroller already wrote out): the
     prefix of row k+1 moves into row k.  No access is added or removed,
     so every access stays at the address it had.

   - an innermost single-block counted loop: the loop is software
     pipelined by one row.  The first row's prefix is issued before the
     loop, each trip issues the next row's prefix, the loop runs one trip
     fewer, and the last row's remainder is peeled after the loop.  The
     last trip therefore never loads past the last row.

   Legality is the same in both forms:

   - the prefix is taken from the row's statements before its first
     CC-writing or otherwise effectful word, and it is placed after the
     previous row's last such word, so no lane-state change lies between
     a moved statement's old and new positions;
   - every moved load's address must stay in range after the raise, and
     must be disjoint from every store it moves above (opposite column
     parity, address bit 1; SFPLOAD.md, "Functional model"), proven from
     literal addresses or from the known-zero low bits of a runtime
     address;
   - PRNG-advancing words are never moved, nor LUT words, whose table
     and operands sit in fixed LREGs.

   The prefix length and the slot are chosen by the issue-cycle model of
   the row pair (rvtt_builtin_result_latency, delivered words), and the
   rewrite commits only when the model gains and the LREG pressure model
   still fits: the function-wide model for straight-line rows, and for a
   pipelined loop a model of the trip in which the carried prefix values
   are live twice, never above what the original trip needs.

   Runs under -mtt-tensix-optimize-dst-row-lookahead, after the row-pair
   interleave.  */

namespace {

enum la_kind
{
  LA_LOAD,	/* typed SFPLOAD */
  LA_STORE,	/* typed SFPSTORE */
  LA_PURE,	/* lane-local compute with no Dst, CC or PRNG effect */
  LA_PRNG,	/* lane-local compute advancing the per-lane PRNG */
  LA_SCALAR,	/* scalar statement (address arithmetic) */
  LA_BARRIER	/* anything that may change lane state or other state */
};

/* A typed Dst address: a literal OFFSET, or a runtime BASE plus OFFSET.
   For the runtime form WORD is the synthesized instruction word that
   carries the address.  */



/* One row: its statements and the TTINCRWC closing it (null for the last,
   open row of a run).  */


/* One statement of a modeled sequence.  The pipelined loop is modeled as
   consecutive trips, so a value is keyed by its SSA name and the trip it
   belongs to: the statement defines DEF_TAG's copy and reads USE_TAG's,
   except that reads of PREFIX_DEFS take PREFIX_TAG's copy.  */


class row_lookahead
{
public:
  unsigned n_fired = 0;
  unsigned n_refused = 0;

  void process_block (basic_block bb);
  void process_loop (class loop *loop);

private:
  std::unordered_map<tree, std::pair<tree, tree>> raised_word;
  std::vector<std::pair<tree, std::vector<gimple *>>> created;

  bool refuse (const char *name, int where, const char *detail = nullptr);
  bool segment (basic_block bb, std::vector<la_row> *rows);
  void eligible (const la_row &r, const std::unordered_set<tree> &before,
		 std::vector<unsigned> *out);
  bool raise_load (la_stmt &st, HOST_WIDE_INT step);
};

bool
row_lookahead::refuse (const char *name, int where, const char *detail)
{
  ++n_refused;
  rvtt_refuse_by_name (name, dump_file,
		       "Dst-row lookahead refused: %d (%s)", where, name);
  if (dump_file)
    {
      if (detail)
	fprintf (dump_file, ": %s", detail);
      fprintf (dump_file, "\n");
    }
  return false;
}

/* Split BB into rows.  A statement no row may contain closes the current
   run; every TTINCRWC Dst-only step closes a row.  */

bool
row_lookahead::segment (basic_block bb, std::vector<la_row> *rows)
{
  rows->clear ();
  int run = 0;
  la_row cur;
  cur.run = run;
  auto close = [&] (gcall *sep, HOST_WIDE_INT step)
    {
      cur.separator = sep;
      cur.step = step;
      rows->push_back (cur);
      cur = la_row ();
      cur.run = run;
    };
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_nop_p (stmt))
	continue;
      HOST_WIDE_INT step;
      gcall *call = dyn_cast <gcall *> (stmt);
      if (call && la_separator_p (call, &step))
	{
	  close (call, step);
	  continue;
	}
      la_stmt st;
      if (!la_classify (stmt, &st))
	{
	  if (!cur.stmts.empty ())
	    close (nullptr, 0);
	  cur.run = ++run;
	  continue;
	}
      cur.stmts.push_back (st);
    }
  if (!cur.stmts.empty ())
    close (nullptr, 0);
  return !rows->empty ();
}

/* The prefix candidates of row R, in program order: its loads and the
   lane-local words fed only by them and by values in BEFORE (defined
   ahead of the insertion slot), up to the row's first barrier.  A word
   reading a value R defines outside the prefix stops nothing but is not
   a candidate.  */

void
row_lookahead::eligible (const la_row &r,
			 const std::unordered_set<tree> &before,
			 std::vector<unsigned> *out)
{
  out->clear ();
  std::unordered_set<tree> chosen;
  for (unsigned i = 0; i != r.stmts.size (); ++i)
    {
      const la_stmt &st = r.stmts[i];
      if (st.kind == LA_BARRIER)
	break;
      if (st.kind != LA_LOAD && st.kind != LA_PURE)
	continue;
      gcall *call = as_a <gcall *> (st.stmt);
      if (la_fixed_registers_p (rvtt_get_insn_data (call)))
	continue;
      bool ok = true, fed = st.kind == LA_LOAD;
      for (unsigned a = 0; ok && a != gimple_call_num_args (call); ++a)
	{
	  tree arg = gimple_call_arg (call, a);
	  if (TREE_CODE (arg) != SSA_NAME)
	    continue;
	  if (chosen.count (arg))
	    fed = true;
	  else if (!before.count (arg))
	    ok = false;
	}
      tree lhs = gimple_call_lhs (call);
      if (!ok || !fed || !lhs || TREE_CODE (lhs) != SSA_NAME)
	continue;
      chosen.insert (lhs);
      out->push_back (i);
    }
}

/* Raise the load ST's address by STEP: a literal in place, a runtime
   address through a new raw address and instruction word computed once
   next to the original word.  */

bool
row_lookahead::raise_load (la_stmt &st, HOST_WIDE_INT step)
{
  gcall *call = as_a <gcall *> (st.stmt);
  if (!st.addr.base)
    {
      tree addr = gimple_call_arg (call, 1);
      gimple_call_set_arg (call, 1,
			   build_int_cst (TREE_TYPE (addr),
					  st.addr.offset + step));
      update_stmt (call);
      return true;
    }
  auto hit = raised_word.find (st.addr.word);
  if (hit == raised_word.end ())
    {
      /* word = (raw & mask) + opcode.  */
      gassign *sum = dyn_cast <gassign *> (SSA_NAME_DEF_STMT (st.addr.word));
      if (!sum || gimple_assign_rhs_code (sum) != PLUS_EXPR)
	return false;
      tree masked = gimple_assign_rhs1 (sum);
      tree opcode = gimple_assign_rhs2 (sum);
      gassign *and_def = TREE_CODE (masked) == SSA_NAME
	? dyn_cast <gassign *> (SSA_NAME_DEF_STMT (masked)) : nullptr;
      if (!and_def || gimple_assign_rhs_code (and_def) != BIT_AND_EXPR
	  || gimple_assign_rhs1 (and_def) != st.addr.base
	  || TREE_CODE (gimple_assign_rhs2 (and_def)) != INTEGER_CST
	  || wi::exact_log2 (wi::to_wide (gimple_assign_rhs2 (and_def)) + 1) < 0)
	return false;
      tree type = TREE_TYPE (st.addr.base);
      gimple_stmt_iterator at = gsi_for_stmt (sum);
      tree raw = make_ssa_name (type);
      gassign *g1 = gimple_build_assign (raw, PLUS_EXPR, st.addr.base,
					 build_int_cst (type, step));
      tree m = make_ssa_name (TREE_TYPE (masked));
      gassign *g2 = gimple_build_assign (m, BIT_AND_EXPR, raw,
					 gimple_assign_rhs2 (and_def));
      tree w = make_ssa_name (TREE_TYPE (st.addr.word));
      gassign *g3 = gimple_build_assign (w, PLUS_EXPR, m, opcode);
      gsi_insert_after (&at, g1, GSI_NEW_STMT);
      gsi_insert_after (&at, g2, GSI_NEW_STMT);
      gsi_insert_after (&at, g3, GSI_NEW_STMT);
      created.push_back ({ st.addr.word, { g1, g2, g3 } });
      hit = raised_word.emplace (st.addr.word, std::make_pair (raw, w)).first;
    }
  gimple_call_set_arg (call, 1, hit->second.first);
  gimple_call_set_arg (call, 2, hit->second.second);
  update_stmt (call);
  return true;
}

/* Straight-line rows: move each row's best prefix into the row before
   it.  */

void
row_lookahead::process_block (basic_block bb)
{
  for (unsigned k = 1;; ++k)
    {
      std::vector<la_row> rows;
      segment (bb, &rows);
      if (k >= rows.size ())
	return;
      la_row &q = rows[k - 1];
      la_row &r = rows[k];
      if (q.run != r.run || !q.separator
	  || std::none_of (r.stmts.begin (), r.stmts.end (),
			   [] (const la_stmt &st) { return st.kind == LA_LOAD; }))
	continue;

      /* Slots: after Q's last barrier, up to its separator.  */
      unsigned first_slot = 0;
      for (unsigned i = 0; i != q.stmts.size (); ++i)
	if (q.stmts[i].kind == LA_BARRIER)
	  first_slot = i + 1;

      std::vector<la_item> orig;
      for (const la_stmt &st : q.stmts)
	orig.push_back ({ &st, 0, 0, nullptr, 0 });
      for (const la_stmt &st : r.stmts)
	orig.push_back ({ &st, 0, 0, nullptr, 0 });
      int orig_cycles = la_cycles (orig);

      int best = orig_cycles;
      unsigned best_slot = 0, best_m = 0;
      bool aliased = false;
      for (unsigned slot = first_slot; slot <= q.stmts.size (); ++slot)
	{
	  /* Values defined ahead of the slot.  */
	  std::unordered_set<tree> before;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      gimple *s = gsi_stmt (gsi);
	      if (slot < q.stmts.size () && s == q.stmts[slot].stmt)
		break;
	      if (slot == q.stmts.size () && s == q.separator)
		break;
	      if (tree lhs = gimple_get_lhs (s))
		if (TREE_CODE (lhs) == SSA_NAME)
		  before.insert (lhs);
	    }
	  std::vector<unsigned> cand;
	  eligible (r, before, &cand);
	  for (unsigned m = 1; m <= cand.size (); ++m)
	    {
	      bool legal = true;
	      for (unsigned c = 0; legal && c != m; ++c)
		{
		  const la_stmt &ld = r.stmts[cand[c]];
		  if (ld.kind != LA_LOAD)
		    continue;
		  if (!la_raise_ok_p (ld.addr, as_a <gcall *> (ld.stmt), q.step))
		    legal = false;
		  la_addr up = ld.addr;
		  up.offset += q.step;
		  for (unsigned i = slot; legal && i != q.stmts.size (); ++i)
		    if (q.stmts[i].kind == LA_STORE
			&& !la_disjoint_p (up, q.stmts[i].addr))
		      legal = false, aliased = true;
		  for (unsigned i = 0; legal && i != cand[c]; ++i)
		    if (r.stmts[i].kind == LA_STORE
			&& !la_disjoint_p (ld.addr, r.stmts[i].addr))
		      legal = false, aliased = true;
		}
	      if (!legal)
		break;
	      std::vector<bool> moved (r.stmts.size (), false);
	      for (unsigned c = 0; c != m; ++c)
		moved[cand[c]] = true;
	      std::vector<la_item> seq;
	      for (unsigned i = 0; i != q.stmts.size (); ++i)
		{
		  if (i == slot)
		    for (unsigned c = 0; c != m; ++c)
		      seq.push_back ({ &r.stmts[cand[c]], 0, 0, nullptr, 0 });
		  seq.push_back ({ &q.stmts[i], 0, 0, nullptr, 0 });
		}
	      if (slot == q.stmts.size ())
		for (unsigned c = 0; c != m; ++c)
		  seq.push_back ({ &r.stmts[cand[c]], 0, 0, nullptr, 0 });
	      for (unsigned i = 0; i != r.stmts.size (); ++i)
		if (!moved[i])
		  seq.push_back ({ &r.stmts[i], 0, 0, nullptr, 0 });
	      int c = la_cycles (seq);
	      if (c < best)
		{
		  best = c;
		  best_slot = slot;
		  best_m = m;
		}
	    }
	}
      int where = bb->index;
      if (!best_m)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Dst-row lookahead: bb %d row %u: pair "
		     "cycles %d, no gain\n", bb->index, k, orig_cycles);
	  refuse (aliased ? "dst-row-lookahead-dst-alias"
		  : "dst-row-lookahead-no-benefit", where);
	  continue;
	}

      /* Apply, keeping what an undo needs.  */
      rvtt_pressure_model pre;
      rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &pre);
      bool over_before = bitmap_bit_p (pre.over_bbs, bb->index);
      std::vector<gimple *> original;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	original.push_back (gsi_stmt (gsi));
      std::vector<std::pair<gcall *, std::pair<tree, tree>>> addrs;
      std::vector<unsigned> cand;
      created.clear ();
      {
	std::unordered_set<tree> before;
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  {
	    gimple *s = gsi_stmt (gsi);
	    if ((best_slot < q.stmts.size () && s == q.stmts[best_slot].stmt)
		|| (best_slot == q.stmts.size () && s == q.separator))
	      break;
	    if (tree lhs = gimple_get_lhs (s))
	      if (TREE_CODE (lhs) == SSA_NAME)
		before.insert (lhs);
	  }
	eligible (r, before, &cand);
      }
      gimple *anchor = best_slot < q.stmts.size ()
	? q.stmts[best_slot].stmt : q.separator;
      bool ok = true;
      for (unsigned c = 0; c != best_m; ++c)
	{
	  la_stmt &st = r.stmts[cand[c]];
	  gcall *call = as_a <gcall *> (st.stmt);
	  if (st.kind == LA_LOAD)
	    {
	      addrs.push_back ({ call, { gimple_call_arg (call, 1),
					 gimple_call_arg (call, 2) } });
	      if (!raise_load (st, q.step))
		{
		  ok = false;
		  break;
		}
	    }
	  gimple_stmt_iterator from = gsi_for_stmt (call);
	  gimple_stmt_iterator to = gsi_for_stmt (anchor);
	  gsi_move_before (&from, &to);
	}
      rvtt_pressure_model post;
      if (ok)
	rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &post);
      if (!ok || (!over_before && bitmap_bit_p (post.over_bbs, bb->index)))
	{
	  for (auto &a : addrs)
	    {
	      gimple_call_set_arg (a.first, 1, a.second.first);
	      gimple_call_set_arg (a.first, 2, a.second.second);
	      update_stmt (a.first);
	    }
	  for (auto &c : created)
	    {
	      raised_word.erase (c.first);
	      for (unsigned i = c.second.size (); i--;)
		{
		  gimple_stmt_iterator gsi = gsi_for_stmt (c.second[i]);
		  gsi_remove (&gsi, true);
		  release_defs (c.second[i]);
		}
	    }
	  gimple_stmt_iterator tail = gsi_last_bb (bb);
	  gimple *last = gsi_stmt (tail);
	  for (gimple *s : original)
	    if (s != last)
	      {
		gimple_stmt_iterator from = gsi_for_stmt (s);
		gsi_move_before (&from, &tail);
	      }
	  refuse (ok ? "dst-row-lookahead-pressure-exceeded"
		  : "dst-row-lookahead-dst-access-unproven", where);
	  continue;
	}
      ++n_fired;
      if (dump_file)
	fprintf (dump_file, "Dst-row lookahead: bb %d row %u: %u statement(s) "
		 "of the next row issued %u slot(s) before the row step, "
		 "pair cycles %d -> %d\n", bb->index, k, best_m,
		 (unsigned) (q.stmts.size () - best_slot), orig_cycles, best);
    }
}

/* A counted single-block loop: pipeline it by one row.  */

void
row_lookahead::process_loop (class loop *loop)
{
  int where = loop->num;
  if (loop->inner || loop->num_nodes != 1)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "not a single block");
      return;
    }
  basic_block bb = loop->header;
  if (EDGE_COUNT (bb->preds) != 2 || EDGE_COUNT (bb->succs) != 2)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "edges");
      return;
    }
  edge entry = nullptr, latch = nullptr, exit = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == bb)
      latch = e;
    else
      entry = e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest != bb)
      exit = e;
  if (!latch || !entry || !exit || (entry->flags & EDGE_ABNORMAL)
      || (exit->flags & EDGE_ABNORMAL))
    {
      refuse ("dst-row-lookahead-loop-shape", where, "latch");
      return;
    }
  unsigned HOST_WIDE_INT trips;
  if (!rvtt_loop_trips_gimple (loop, &trips) || trips < 2)
    {
      refuse ("dst-row-lookahead-trips", where);
      return;
    }
  gcond *cond = safe_dyn_cast <gcond *> (gsi_stmt (gsi_last_bb (bb)));
  if (!cond)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "no exit test");
      return;
    }

  /* One integral induction variable, stepped by a constant, read only
     by its step and the exit test, with a literal initial value.  */
  gphi *iv = nullptr;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (virtual_operand_p (res))
	continue;
      if (iv || !INTEGRAL_TYPE_P (TREE_TYPE (res)))
	{
	  refuse ("dst-row-lookahead-loop-carried", where);
	  return;
	}
      iv = phi;
    }
  if (!iv)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "no induction variable");
      return;
    }
  tree res = gimple_phi_result (iv);
  tree next = PHI_ARG_DEF_FROM_EDGE (iv, latch);
  tree init = PHI_ARG_DEF_FROM_EDGE (iv, entry);
  gassign *step_stmt = TREE_CODE (next) == SSA_NAME
    ? dyn_cast <gassign *> (SSA_NAME_DEF_STMT (next)) : nullptr;
  if (!step_stmt || gimple_bb (step_stmt) != bb
      || (gimple_assign_rhs_code (step_stmt) != PLUS_EXPR
	  && gimple_assign_rhs_code (step_stmt) != MINUS_EXPR)
      || gimple_assign_rhs1 (step_stmt) != res
      || TREE_CODE (gimple_assign_rhs2 (step_stmt)) != INTEGER_CST
      || TREE_CODE (init) != INTEGER_CST)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "induction step");
      return;
    }
  {
    use_operand_p use;
    imm_use_iterator it;
    FOR_EACH_IMM_USE_FAST (use, it, res)
      if (USE_STMT (use) != step_stmt && !is_gimple_debug (USE_STMT (use)))
	{
	  refuse ("dst-row-lookahead-loop-shape", where, "induction variable");
	  return;
	}
    FOR_EACH_IMM_USE_FAST (use, it, next)
      if (USE_STMT (use) != iv && USE_STMT (use) != cond
	  && !is_gimple_debug (USE_STMT (use)))
	{
	  refuse ("dst-row-lookahead-loop-shape", where, "induction variable");
	  return;
	}
  }

  /* The body is one row closed by its separator, then the step and the
     test.  */
  la_row r;
  bool closed = false;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_nop_p (stmt) || stmt == cond || stmt == step_stmt)
	continue;
      HOST_WIDE_INT step;
      gcall *call = dyn_cast <gcall *> (stmt);
      if (call && la_separator_p (call, &step))
	{
	  if (closed)
	    {
	      refuse ("dst-row-lookahead-loop-shape", where, "two row steps");
	      return;
	    }
	  r.separator = call;
	  r.step = step;
	  closed = true;
	  continue;
	}
      la_stmt st;
      if (closed || !la_classify (stmt, &st) || st.kind == LA_SCALAR)
	{
	  refuse ("dst-row-lookahead-foreign-stmt", where,
		  closed ? "word after the row step" : nullptr);
	  return;
	}
      r.stmts.push_back (st);
      /* Row values stay in the loop: the peeled row re-creates them.  */
      if (tree lhs = gimple_get_lhs (stmt))
	{
	  use_operand_p use;
	  imm_use_iterator it;
	  FOR_EACH_IMM_USE_FAST (use, it, lhs)
	    if (!is_gimple_debug (USE_STMT (use))
		&& gimple_bb (USE_STMT (use)) != bb)
	      {
		refuse ("dst-row-lookahead-live-out", where);
		return;
	      }
	}
    }
  if (!closed)
    {
      refuse ("dst-row-lookahead-loop-shape", where, "no row step");
      return;
    }

  /* Everything defined outside the loop is available at any slot; values
     the row defines are not.  */
  std::unordered_set<tree> outside;
  for (const la_stmt &st : r.stmts)
    if (gcall *call = dyn_cast <gcall *> (st.stmt))
      for (unsigned a = 0; a != gimple_call_num_args (call); ++a)
	{
	  tree arg = gimple_call_arg (call, a);
	  if (TREE_CODE (arg) == SSA_NAME
	      && gimple_bb (SSA_NAME_DEF_STMT (arg)) != bb)
	    outside.insert (arg);
	}
  std::vector<unsigned> cand;
  eligible (r, outside, &cand);
  if (cand.empty ())
    {
      refuse ("dst-row-lookahead-no-prefix", where);
      return;
    }

  unsigned first_slot = 0;
  for (unsigned i = 0; i != r.stmts.size (); ++i)
    if (r.stmts[i].kind == LA_BARRIER)
      first_slot = i + 1;

  /* Two trips of the original loop, against the prologue prefix plus
     two pipelined trips.  */
  std::vector<la_item> orig;
  for (int tag = 0; tag != 2; ++tag)
    for (const la_stmt &st : r.stmts)
      orig.push_back ({ &st, tag, tag, nullptr, 0 });
  int orig_cycles = la_cycles (orig);

  struct option { int cycles; unsigned m, slot; };
  std::vector<option> options;
  bool aliased = false;
  for (unsigned m = 1; m <= cand.size (); ++m)
    {
      std::unordered_set<tree> prefix_defs;
      std::vector<bool> moved (r.stmts.size (), false);
      bool legal = true;
      for (unsigned c = 0; c != m; ++c)
	{
	  moved[cand[c]] = true;
	  prefix_defs.insert (gimple_get_lhs (r.stmts[cand[c]].stmt));
	  const la_stmt &ld = r.stmts[cand[c]];
	  if (ld.kind != LA_LOAD)
	    continue;
	  if (!la_raise_ok_p (ld.addr, as_a <gcall *> (ld.stmt), r.step))
	    legal = false;
	  for (unsigned i = 0; legal && i != cand[c]; ++i)
	    if (r.stmts[i].kind == LA_STORE
		&& !la_disjoint_p (ld.addr, r.stmts[i].addr))
	      legal = false, aliased = true;
	}
      if (!legal)
	break;
      /* A slot's crossed stores are those at or after it, seen with the
	 raised address.  */
      for (unsigned slot = MAX (first_slot, cand[m - 1] + 1);
	   slot <= r.stmts.size (); ++slot)
	{
	  bool ok = true;
	  for (unsigned c = 0; ok && c != m; ++c)
	    {
	      const la_stmt &ld = r.stmts[cand[c]];
	      if (ld.kind != LA_LOAD)
		continue;
	      la_addr up = ld.addr;
	      up.offset += r.step;
	      for (unsigned i = slot; ok && i != r.stmts.size (); ++i)
		if (r.stmts[i].kind == LA_STORE
		    && !la_disjoint_p (up, r.stmts[i].addr))
		  ok = false, aliased = true;
	    }
	  if (!ok)
	    continue;
	  std::vector<la_item> seq;
	  for (unsigned c = 0; c != m; ++c)
	    seq.push_back ({ &r.stmts[cand[c]], 0, 0, nullptr, 0 });
	  for (int tag = 0; tag != 2; ++tag)
	    {
	      for (unsigned i = 0; i != r.stmts.size (); ++i)
		{
		  if (i == slot)
		    for (unsigned c = 0; c != m; ++c)
		      seq.push_back ({ &r.stmts[cand[c]], tag + 1, tag + 1,
				       nullptr, 0 });
		  if (!moved[i])
		    seq.push_back ({ &r.stmts[i], tag, tag, &prefix_defs, tag });
		}
	      if (slot == r.stmts.size ())
		for (unsigned c = 0; c != m; ++c)
		  seq.push_back ({ &r.stmts[cand[c]], tag + 1, tag + 1,
				   nullptr, 0 });
	    }
	  int cyc = la_cycles (seq);
	  /* The pipelined pair also issues the prefix of a third row, which
	     the original pays in its next trip.  */
	  int extra = 0;
	  for (unsigned c = 0; c != m; ++c)
	    extra += r.stmts[cand[c]].words;
	  if (cyc - extra < orig_cycles)
	    options.push_back ({ cyc - extra, m, slot });
	}
    }
  if (options.empty ())
    {
      refuse (aliased ? "dst-row-lookahead-dst-alias"
	      : "dst-row-lookahead-no-benefit", where);
      return;
    }
  std::stable_sort (options.begin (), options.end (),
		    [] (const option &a, const option &b)
		    { return a.cycles < b.cycles
			     || (a.cycles == b.cycles && a.m < b.m); });

  /* Pressure of the pipelined body: the moved prefix values live from
     their slot to the end of the trip and again from the trip's start to
     their last read; values from outside the loop live throughout,
     including reads of allocatable LREGs the row is pinned to.  */
  auto peak_of = [&] (unsigned best_m, unsigned best_slot) -> int
  {
    std::vector<bool> moved (r.stmts.size (), false);
    std::unordered_set<tree> prefix_defs;
    for (unsigned c = 0; c != best_m; ++c)
      {
	moved[cand[c]] = true;
	prefix_defs.insert (gimple_get_lhs (r.stmts[cand[c]].stmt));
      }
    std::vector<const la_stmt *> order;
    for (unsigned i = 0; i != r.stmts.size (); ++i)
      {
	if (i == best_slot)
	  for (unsigned c = 0; c != best_m; ++c)
	    order.push_back (&r.stmts[cand[c]]);
	if (!moved[i])
	  order.push_back (&r.stmts[i]);
      }
    if (best_slot == r.stmts.size ())
      for (unsigned c = 0; c != best_m; ++c)
	order.push_back (&r.stmts[cand[c]]);
    int n = order.size ();
    struct range { int def, last; unsigned width; bool wrap; int head; };
    std::map<tree, range> ranges;
    for (tree v : outside)
      {
	bool pinned = false;
	if (gcall *d = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (v)))
	  {
	    const rvtt_insn_data *di = rvtt_get_insn_data (d);
	    unsigned reg;
	    pinned = di && di->id == rvtt_insn_data::sfpreadlreg
		     && rvtt_call_const_uarg (d, 0, &reg) && reg < 8;
	  }
	if (pinned || rvtt_pressure_tracked_p (v))
	  ranges[v] = { -1, n, pinned ? 1u : lreg_width (v), false, -1 };
      }
    for (int p = 0; p != n; ++p)
      if (gcall *call = dyn_cast <gcall *> (order[p]->stmt))
	{
	  tree lhs = gimple_call_lhs (call);
	  if (lhs && rvtt_pressure_tracked_p (lhs))
	    ranges[lhs] = { p, p, lreg_width (lhs), false, -1 };
	}
    for (int p = 0; p != n; ++p)
      {
	gcall *call = dyn_cast <gcall *> (order[p]->stmt);
	if (!call)
	  continue;
	bool is_prefix = prefix_defs.count (gimple_get_lhs (call));
	for (unsigned a = 0; a != gimple_call_num_args (call); ++a)
	  {
	    tree arg = gimple_call_arg (call, a);
	    auto it = ranges.find (arg);
	    if (it == ranges.end () || it->second.def < 0)
	      continue;
	    if (prefix_defs.count (arg) && !is_prefix)
	      {
		it->second.wrap = true;
		it->second.head = MAX (it->second.head, p);
	      }
	    else
	      it->second.last = MAX (it->second.last, p);
	  }
      }
    int peak = 0;
    for (int p = 0; p != n; ++p)
      {
	int in = 0, out = 0;
	for (auto &kv : ranges)
	  {
	    const range &rg = kv.second;
	    int last = rg.wrap ? n : rg.last;
	    /* A carried value is two live ranges: last trip's copy up to its
	       last read, and this trip's from the moved definition on.  */
	    if (rg.def < p && p <= last)
	      in += rg.width;
	    if (rg.wrap && p <= rg.head)
	      in += rg.width;
	    if (rg.def <= p && (p < last || rg.def == last))
	      out += rg.width;
	    if (rg.wrap && p < rg.head)
	      out += rg.width;
	  }
	int live = MAX (in, out)
		   + (int) rvtt_pressure_cc_transient (order[p]->stmt);
	peak = MAX (peak, live);
      }
    return peak;
  };
  /* The model may overstate what the allocator needs; never ask for more
     than the original row already needs.  */
  int limit = MAX ((int) rvtt_pressure_capacity (),
		   peak_of (0, r.stmts.size ()));
  int best = orig_cycles;
  unsigned best_slot = 0, best_m = 0;
  for (const option &o : options)
    {
      int peak = peak_of (o.m, o.slot);
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Dst-row lookahead: loop %d prefix %u at slot %u "
		 "cycles %d peak %d limit %d\n", loop->num, o.m, o.slot,
		 o.cycles, peak, limit);
      if (peak <= limit)
	{
	  best = o.cycles;
	  best_m = o.m;
	  best_slot = o.slot;
	  break;
	}
    }
  if (!best_m)
    {
      refuse ("dst-row-lookahead-pressure-exceeded", where);
      return;
    }
  /* Commit.  The prologue issues the first row's prefix on the entry
     edge.  */
  std::vector<gcall *> prefix;
  for (unsigned c = 0; c != best_m; ++c)
    prefix.push_back (as_a <gcall *> (r.stmts[cand[c]].stmt));
  std::unordered_set<gimple *> prefix_set (prefix.begin (), prefix.end ());

  auto fresh = [] (gcall *from)
    {
      gcall *copy = as_a <gcall *> (gimple_copy (from));
      if (gimple_vuse (from))
	gimple_set_vuse (copy, gimple_vop (cfun));
      if (gimple_vdef (from))
	gimple_set_vdef (copy, gimple_vop (cfun));
      if (tree lhs = gimple_call_lhs (from))
	gimple_call_set_lhs (copy, make_ssa_name (TREE_TYPE (lhs), copy));
      return copy;
    };

  basic_block pre = split_edge (entry);
  entry = single_succ_edge (pre);
  std::unordered_map<tree, tree> first;	/* prefix def -> prologue copy */
  {
    gimple_stmt_iterator at = gsi_last_bb (pre);
    for (gcall *p : prefix)
      {
	gcall *copy = fresh (p);
	for (unsigned a = 0; a != gimple_call_num_args (copy); ++a)
	  {
	    auto f = first.find (gimple_call_arg (copy, a));
	    if (f != first.end ())
	      gimple_call_set_arg (copy, a, f->second);
	  }
	if (gsi_end_p (at))
	  {
	    at = gsi_start_bb (pre);
	    gsi_insert_before (&at, copy, GSI_NEW_STMT);
	  }
	else
	  gsi_insert_after (&at, copy, GSI_NEW_STMT);
	update_stmt (copy);
	first.emplace (gimple_call_lhs (p), gimple_call_lhs (copy));
      }
  }

  /* Each prefix value the rest of the row reads arrives through a PHI:
     the prologue copy on entry, the value issued in the previous trip on
     the latch.  */
  std::unordered_map<tree, tree> carried;	/* prefix def -> PHI result */
  for (gcall *p : prefix)
    {
      tree v = gimple_call_lhs (p);
      bool read = false;
      use_operand_p use;
      imm_use_iterator it;
      FOR_EACH_IMM_USE_FAST (use, it, v)
	if (!is_gimple_debug (USE_STMT (use))
	    && !prefix_set.count (USE_STMT (use)))
	  read = true;
      if (!read)
	continue;
      tree t = make_ssa_name (TREE_TYPE (v));
      gphi *phi = create_phi_node (t, bb);
      add_phi_arg (phi, first.at (v), entry, UNKNOWN_LOCATION);
      add_phi_arg (phi, v, latch, UNKNOWN_LOCATION);
      carried.emplace (v, t);
    }
  for (const la_stmt &st : r.stmts)
    {
      if (prefix_set.count (st.stmt))
	continue;
      gcall *call = dyn_cast <gcall *> (st.stmt);
      if (!call)
	continue;
      bool changed = false;
      for (unsigned a = 0; a != gimple_call_num_args (call); ++a)
	{
	  auto c = carried.find (gimple_call_arg (call, a));
	  if (c != carried.end ())
	    {
	      gimple_call_set_arg (call, a, c->second);
	      changed = true;
	    }
	}
      if (changed)
	update_stmt (call);
    }

  /* Move the prefix into its slot, reading the next row.  */
  gimple *anchor = best_slot < r.stmts.size ()
    ? r.stmts[best_slot].stmt : r.separator;
  for (unsigned c = 0; c != best_m; ++c)
    {
      la_stmt &st = r.stmts[cand[c]];
      if (st.kind == LA_LOAD)
	gcc_assert (raise_load (st, r.step));
      gimple_stmt_iterator from = gsi_for_stmt (st.stmt);
      gimple_stmt_iterator to = gsi_for_stmt (anchor);
      gsi_move_before (&from, &to);
    }

  /* One trip fewer.  */
  tree istep = gimple_assign_rhs2 (step_stmt);
  wide_int w = wi::to_wide (init);
  w = gimple_assign_rhs_code (step_stmt) == PLUS_EXPR
    ? w + wi::to_wide (istep) : w - wi::to_wide (istep);
  SET_PHI_ARG_DEF (iv, entry->dest_idx,
		   wide_int_to_tree (TREE_TYPE (init), w));

  /* The last row's remainder runs after the loop.  */
  basic_block epi = split_edge (exit);
  {
    std::unordered_map<tree, tree> map;
    for (auto &c : carried)
      map.emplace (c.second, c.first);
    gimple_stmt_iterator at = gsi_start_bb (epi);
    bool started = false;
    std::vector<gcall *> rest;
    for (const la_stmt &st : r.stmts)
      if (!prefix_set.count (st.stmt))
	rest.push_back (as_a <gcall *> (st.stmt));
    rest.push_back (r.separator);
    for (gcall *s : rest)
      {
	gcall *copy = fresh (s);
	for (unsigned a = 0; a != gimple_call_num_args (copy); ++a)
	  {
	    auto f = map.find (gimple_call_arg (copy, a));
	    if (f != map.end ())
	      gimple_call_set_arg (copy, a, f->second);
	  }
	if (!started)
	  {
	    gsi_insert_before (&at, copy, GSI_NEW_STMT);
	    started = true;
	  }
	else
	  gsi_insert_after (&at, copy, GSI_NEW_STMT);
	update_stmt (copy);
	if (tree lhs = gimple_call_lhs (s))
	  map.emplace (lhs, gimple_call_lhs (copy));
      }
  }

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *dbg = gsi_stmt (gsi);
      if (gimple_debug_bind_p (dbg) && gimple_debug_bind_has_value_p (dbg))
	{
	  gimple_debug_bind_reset_value (dbg);
	  update_stmt (dbg);
	}
    }
  free_numbers_of_iterations_estimates (loop);
  ++n_fired;
  if (dump_file)
    fprintf (dump_file, "Dst-row lookahead: loop %d trips "
	     HOST_WIDE_INT_PRINT_UNSIGNED " -> " HOST_WIDE_INT_PRINT_UNSIGNED
	     " and a peeled last row, %u statement(s) issued one row ahead "
	     "%u slot(s) before the row step, trip-pair cycles %d -> %d\n",
	     loop->num, trips, trips - 1, best_m,
	     (unsigned) (r.stmts.size () - best_slot), orig_cycles, best);
}

const pass_data pass_data_rvtt_dst_row_lookahead = {
  GIMPLE_PASS, "rvtt_dst_row_lookahead", OPTGROUP_OTHER, TV_NONE,
  PROP_cfg | PROP_ssa, 0, 0, 0, 0
};

class pass_rvtt_dst_row_lookahead : public gimple_opt_pass
{
public:
  pass_rvtt_dst_row_lookahead (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_dst_row_lookahead, ctxt) {}
  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX_BH && optimize > 0
      && riscv_tt_opt_dst_row_lookahead;
  }
  unsigned execute (function *fn) final override
  {
    row_lookahead ctx;
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    std::vector<class loop *> loops;
    for (auto loop : loops_list (fn, LI_ONLY_INNERMOST))
      loops.push_back (loop);
    for (class loop *loop : loops)
      ctx.process_loop (loop);
    std::vector<basic_block> blocks;
    basic_block bb;
    FOR_EACH_BB_FN (bb, fn)
      blocks.push_back (bb);
    for (basic_block b : blocks)
      ctx.process_block (b);
    loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file, "Dst-row lookahead: fires=%u refusals=%u\n",
	       ctx.n_fired, ctx.n_refused);
    if (!ctx.n_fired)
      return 0;
    free_dominance_info (CDI_DOMINATORS);
    mark_virtual_operands_for_renaming (fn);
    return TODO_update_ssa_only_virtuals | TODO_cleanup_cfg;
  }
};

} /* anonymous namespace */
