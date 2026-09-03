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
   advances the counter.  Two passes here widen that iteration grain
   from one row-pair to two, which halves the TTINCRWC overhead and
   doubles the straight-line body available to the RTL schedulers.

   rvtt_dst_iteration (fusion): scan each block for two consecutive
   iterations -- body, TTINCRWC(+2), identical body, TTINCRWC(+2) --
   whose calls match pairwise (same operations, same scalar operands,
   vector operands isomorphic under the value map established by the
   pair, same Dst addresses).  Legality requires: every typed Dst
   access uses the no-auto-increment addressing mode with a provable
   in-range address (VRP-backed) even after +2 rewriting; each body's
   vector values are consumed only inside its own iteration; and at
   least two loads and a store are present (the shapes worth fusing).
   The fusion rewrites the second body's Dst addresses to base+2 and
   merges the two TTINCRWC(+2)s into one TTINCRWC(+4), deleting the
   first.

   rvtt_dst_interleave: runs late, after lowering has exposed the final
   typed SFPU operations but before RTL expansion.  A fused group
   (recognized by its TTINCRWC(+4) marker) whose two halves still match
   pairwise is rescheduled load/load/compute/compute/drain/drain so the
   two rows' independent chains overlap; legality further requires the
   loads-computes-drains shape, one store per half placed last, 4-byte
   alignment of the first row's typed addresses (making the +2 row's
   accesses provably disjoint from the first row's store under the
   interleaved order), all scalar operand definitions to dominate the
   group, and closed vector def-use within the group.  Rescheduling
   drops the group-internal virtual operands (the accesses were proven
   disjoint) and lengthens independent live ranges before IRA assigns
   physical LREGs.

   Both passes only reorder/rewrite proven-identical bodies; any
   unrecognized statement inside a candidate window rejects the whole
   candidate (with a TDF_DETAILS reason line).  On Quasar
   (TARGET_XTT_TENSIX_QSR) the analysis runs and dumps its verdicts,
   but the rewrite itself is suppressed (emit=no).

   Runs under -mtt-tensix-optimize-dst-iteration-fusion.  */


#define INCLUDE_UNORDERED_MAP
#define INCLUDE_UNORDERED_SET
#define INCLUDE_VECTOR
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
#include "rvtt.h"
#include "rvtt-effects.h"

namespace {

/* One candidate iteration: its body calls and closing TTINCRWC.  */

struct dst_iteration
{
  std::vector<gcall *> calls;
  gcall *increment = nullptr;
  bool rejected = false;
};

/* Are A and B equal integer constants?  */

static bool
integer_cst_eq (tree a, tree b)
{
  return TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST
    && tree_int_cst_equal (a, b);
}

/* Is VALUE an SFPU vector value?  */

static bool
vector_value_p (tree value)
{
  return value && VECTOR_TYPE_P (TREE_TYPE (value));
}

/* Is INSND a typed Dst access (SFPLOAD/SFPSTORE)?  */

static bool
typed_dst_access_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpstore;
}

/* May CALL (an INSND intrinsic) be part of a fusible iteration body?
   Typed accesses always; otherwise only effect-free, CC-preserving
   operations.  */

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

/* Width of the Dst address field of INSND on this architecture.  */

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

/* Is CALL a Dst access whose address provably stays in range after
   adding INCREMENT, in the no-auto-increment addressing mode?  Uses
   VRP ranges for non-constant addresses.  */

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

/* Is ADJUSTED provably BASE + INCREMENT (as constants, or as the
   direct PLUS_EXPR rewrite the fusion pass inserted)?  */

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

/* Is ADDRESS provably ALIGNMENT-aligned (by known nonzero bits)?  */

static bool
address_aligned_p (tree address, unsigned alignment)
{
  gcc_assert (pow2p_hwi (alignment));
  wide_int nonzero = get_nonzero_bits (address);
  return wi::extract_uhwi (nonzero, 0, exact_log2 (alignment)) == 0;
}

/* Is CALL a plain row increment -- TTINCRWC advancing only the Dst
   counter, by 2?  Sets *AMOUNT.  */

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

/* Perform the fusion: rebase SECOND's Dst addresses by INCREMENT,
   widen its TTINCRWC to INCREMENT*2 and delete FIRST's.  */

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
		       build_int_cst (TREE_TYPE (old_increment), increment * 2));
  update_stmt (second.increment);

  gimple_stmt_iterator gsi = gsi_for_stmt (first.increment);
  if (gimple_vdef (first.increment))
    unlink_stmt_vdef (first.increment);
  gsi_remove (&gsi, true);
}

/* Do iterations A and B have pairwise-matching bodies, legal for
   fusion at INCREMENT?  See the file comment for the criteria; VALUES
   carries the A-value -> B-value isomorphism as it is established.  */

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
		fprintf (dump_file, "Dst-body mismatch: call=%u access-legality\n", i);
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
		    fprintf (dump_file, "Dst-body mismatch: call=%u arg=%u address\n", i, argno);
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
		    fprintf (dump_file, "Dst-body mismatch: call=%u arg=%u live-in\n", i, argno);
		  return false;
		}
	      if ((it == values.end () && va != vb)
		  || (it != values.end () && it->second != vb))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Dst-body mismatch: call=%u arg=%u vector\n", i, argno);
		  return false;
		}
	    }
	  else if (!operand_equal_p (va, vb, 0))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "Dst-body mismatch: call=%u arg=%u scalar\n", i, argno);
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

/* Are all of ITERATION's vector results consumed only within the
   iteration itself?  */

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

/* The rvtt_dst_iteration pass body: find and fuse candidate adjacent
   iterations per block.  Returns the candidate count (dumped).  */

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
		  bool increment_legal = increment_p (previous.increment, &prior)
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
			     bb->index, previous.calls.size (), current.calls.size (),
			     previous.rejected, current.rejected, increment_legal,
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
				 "addr-delta=%ld final-rwc=%ld target=%s emit=%s\n",
				 bb->index, current.calls.size (), (long) increment,
				 (long) (increment * 2),
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

} // anonymous namespace

/* Pass factory for rvtt_dst_iteration, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_iteration (gcc::context *ctxt)
{
  return new pass_rvtt_dst_iteration (ctxt);
}

namespace {

/* This second pass runs after RVTT lowering has exposed the final typed SFPU
   operations, but before RTL expansion and register allocation.  Phase 2's
   RWC=4 and address+2 form is therefore both a durable group marker and an
   opportunity to lengthen the independent live ranges before IRA assigns
   physical LREGs.  */

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

/* The one-cycle dynamic-result operations are exactly the MAD-subunit
   instructions.  The subunit comes from the generated effect attribute
   family in rvtt-cost.md through the effect query API; there is no
   duplicated opcode list here, and unaudited operations return the
   refusing default and make the group ineligible.  */
static bool
dynamic_result_p (const rvtt_insn_data *insnd)
{
  return rvtt_builtin_subunit (insnd) == XTT_SU_MAD;
}

/* Is INSND a "drain" operation (rounding/store subunits, or the
   compiler-internal SFPASSIGN placeholder) -- the tail of a row chain
   that consumes the dynamic results?  */

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

/* May CALL (an INSND intrinsic) be part of an interleavable group?  */

static bool
late_body_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (typed_dst_access_p (insnd))
    return true;
  return (dynamic_result_p (insnd) || drain_operation_p (insnd))
    && !insnd->has_side_effects (call) && !insnd->sets_cc (call);
}

/* Do the two halves A and B of a fused group still match pairwise and
   satisfy the interleaving legality conditions (addresses exactly +2,
   first-row 4-alignment, one store)?  */

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
	  /* Prove BASE+2 from BASE's established range.  The newly inserted SSA
	     name deliberately has no independent VRP cache entry at this point.  */
	  if (!dst_access_legal_p (ca, 2))
	    return false;
	  tree aa = gimple_call_arg (ca, addr_arg);
	  tree ab = gimple_call_arg (cb, addr_arg);
	  if (!address_plus_p (aa, ab, 2))
	    return false;
	  /* Interleaving moves every row-B load before the row-A store.  Requiring
	     row-A typed addresses to be 4-aligned makes their residues 0 mod 4,
	     while the exact +2 row-B rewrite has residue 2 mod 4.  Thus no moved
	     load can alias the earlier row's store.  */
	  if (ia->id == rvtt_insn_data::sfpload
	      || ia->id == rvtt_insn_data::sfpstore)
	    {
	      bool aligned = address_aligned_p (aa, 4);
	      if (!aligned && dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file,
			 "Dst-interleave mismatch: call=%u address-alignment\n", i);
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

/* Do all scalar operands of OPS have definitions that dominate the
   group (so hoisted calls cannot see undefined operands)?  */

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

/* Are all of OPS's vector results consumed only within the group?  */

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

/* Compute the interleaved schedule of halves A and B into ORDER:
   all loads (A's then B's), then compute pairs alternating A/B, then
   the drains (A's then B's).  Requires the loads/computes/drains shape
   with the store last; returns false if the halves do not have it.
   *LOADS and *DYNAMIC_PAIRS report the shape for dumping.  */

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

/* Reorder OPS into ORDER immediately before INCREMENT, dropping the
   group-internal virtual operand links (the accesses were proven
   disjoint).  */

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

/* Check and (if legal) interleave one fused group OPS ending at
   INCREMENT in BB.  Returns true if the schedule was applied.  */

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

/* The rvtt_dst_interleave pass body: find fused groups (by their
   TTINCRWC(+4) markers) and interleave them.  */

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
	  /* Dynamic typed addresses carry synthesized instruction-buffer
	     bookkeeping between semantic SFPU calls.  It is neither part of a row
	     chain nor a side effect, and its var/id pairing was already proven by
	     phase 2.  */
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
      ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

/* Pass factory for rvtt_dst_interleave, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_interleave (gcc::context *ctxt)
{
  return new pass_rvtt_dst_interleave (ctxt);
}
