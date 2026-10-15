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
   physical LREGs.  Unlike the fusion, the interleave removes no word:
   it fills issue slots and pays in LREG live ranges out of the
   8-register file, which is why the two are separate passes with
   separate legality.

   Both passes only reorder/rewrite proven-identical bodies; any
   unrecognized statement inside a candidate window rejects the whole
   candidate (with a TDF_DETAILS reason line).  On Quasar
   (TARGET_XTT_TENSIX_QSR) the analysis runs and dumps its verdicts,
   but the rewrite itself is suppressed (emit=no).

   Runs under -mtt-tensix-optimize-dst-iteration-fusion.  Both passes
   share that one flag; the interleave has no knob of its own.

   The transformation is a recognized form of unroll-and-jam (S. Carr and
   K. Kennedy, "Improving the ratio of memory operations to
   floating-point operations in loops", ACM TOPLAS 16(6), November 1994,
   pp. 1768-1810): widen the iteration grain, then jam the copies into
   one body so their independent chains overlap and the per-iteration
   overhead is amortized over more work.  Here the copies already exist
   -- an LLK row sequence is written unrolled -- so the jam is
   recognized, never created, and legality is not a dependence question:
   it is pairwise isomorphism of the two bodies under the value map the
   pair itself establishes, plus a VRP-backed proof that every typed Dst
   address stays in range after the +2 rewrite.  gcc/gimple-loop-jam.cc
   does not apply: it needs a loop nest to unroll and a data-dependence
   relation to consult, whereas these bodies are straight-line inside one
   block and the only resource they share is the Dst RWC, which is not a
   memory reference GCC's dependence machinery can see.  */


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

/* Are A and B equal integer constants?  */

/* Return true if A and B are both INTEGER_CSTs of equal value; false
   when either operand is not a constant.  */

static bool
integer_cst_eq (tree a, tree b)
{
  return TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST
    && tree_int_cst_equal (a, b);
}

/* Is VALUE an SFPU vector value?  */

/* Return true if VALUE is a non-null tree of vector type -- an SFPU
   vector datum rather than a scalar or address operand.  */

static bool
vector_value_p (tree value)
{
  return value && VECTOR_TYPE_P (TREE_TYPE (value));
}

/* Is INSND a typed Dst access (SFPLOAD/SFPSTORE)?  */

/* Return true if INSND is a typed Dst-register access (sfpload or
   sfpstore) -- the calls whose address argument the fusion
   rewrites.  */

static bool
typed_dst_access_p (const rvtt_insn_data *insnd)
{
  return insnd->id == rvtt_insn_data::sfpload
    || insnd->id == rvtt_insn_data::sfpstore;
}

/* May CALL (an INSND intrinsic) be part of a fusible iteration body?
   Typed accesses always; otherwise only effect-free, CC-preserving
   operations.  */

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

/* Width of the Dst address field of INSND on this architecture.  */

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

/* Is CALL a Dst access whose address provably stays in range after
   adding INCREMENT, in the no-auto-increment addressing mode?  Uses
   VRP ranges for non-constant addresses.  */

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

/* Is ADJUSTED provably BASE + INCREMENT (as constants, or as the
   direct PLUS_EXPR rewrite the fusion pass inserted)?  */

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

/* Is ADDRESS provably ALIGNMENT-aligned (by known nonzero bits)?  */

/* Return true if ADDRESS is provably a multiple of ALIGNMENT (a
   power of two), judged by its known-zero low bits.  */

static bool
address_aligned_p (tree address, unsigned alignment)
{
  gcc_assert (pow2p_hwi (alignment));
  wide_int nonzero = get_nonzero_bits (address);
  return wi::extract_uhwi (nonzero, 0, exact_log2 (alignment)) == 0;
}

/* Is CALL a plain row increment -- TTINCRWC advancing only the Dst
   counter, by 2?  Sets *AMOUNT.  */

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

/* Perform the fusion: rebase SECOND's Dst addresses by INCREMENT,
   widen its TTINCRWC to INCREMENT*2 and delete FIRST's.  */

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

/* Do iterations A and B have pairwise-matching bodies, legal for
   fusion at INCREMENT?  See the file comment for the criteria; VALUES
   carries the A-value -> B-value isomorphism as it is established.  */

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

/* Are all of ITERATION's vector results consumed only within the
   iteration itself?  */

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

/* The rvtt_dst_iteration pass body: find and fuse candidate adjacent
   iterations per block.  Returns the candidate count (dumped).  */

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

/* This second pass runs after RVTT lowering has exposed the final typed SFPU
   operations, but before RTL expansion and register allocation. The fusion
   pass's TTINCRWC(+4) and address+2 form is therefore both a durable group
   marker and an opportunity to lengthen the independent live ranges before IRA
   assigns physical LREGs.  */

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

/* May CALL (an INSND intrinsic) be part of an interleavable group?  */

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

/* Do the two halves A and B of a fused group still match pairwise and
   satisfy the interleaving legality conditions (addresses exactly +2,
   first-row 4-alignment, one store)?  */

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

/* Do all scalar operands of OPS have definitions that dominate the
   group (so hoisted calls cannot see undefined operands)?  */

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

/* Are all of OPS's vector results consumed only within the group?  */

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

/* Compute the interleaved schedule of halves A and B into ORDER:
   all loads (A's then B's), then compute pairs alternating A/B, then
   the drains (A's then B's).  Requires the loads/computes/drains shape
   with the store last; returns false if the halves do not have it.
   *LOADS and *DYNAMIC_PAIRS report the shape for dumping.  */

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

/* Reorder OPS into ORDER immediately before INCREMENT, dropping the
   group-internal virtual operand links (the accesses were proven
   disjoint).  */

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

/* Check and (if legal) interleave one fused group OPS ending at
   INCREMENT in BB.  Returns true if the schedule was applied.  */

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

/* The rvtt_dst_interleave pass body: find fused groups (by their
   TTINCRWC(+4) markers) and interleave them.  */

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
	  /* Dynamic typed addresses carry synthesized instruction-buffer
	     bookkeeping between semantic SFPU calls. It is neither part of a
	     row chain nor a side effect, and its var/id pairing was already
	     proven by the fusion pass.  */
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

} /* anonymous namespace */

/* Pass factory for rvtt_dst_interleave, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_dst_interleave (gcc::context *ctxt)
{
  return new pass_rvtt_dst_interleave (ctxt);
}

/* Row-pair interleave of rolled Dst row loops.

   The two passes above work on rows that are already written out
   straight-line.  Most SFPU kernels instead iterate one row per trip of
   a counted loop, and when the row is a serial dependence chain (a
   polynomial, a Newton step) every producer whose consumer issues next
   exposes its result latency: SFPMAD needs two cycles, and the hardware
   stalls a dependent SFPU instruction presented on the next cycle
   (SFPMAD.md, "Instruction scheduling").  Two consecutive rows are
   independent, so the second row's chain is exactly the filler the first
   row's stalls need.

   rvtt_dst_row_interleave unrolls such a loop by two and jams the copies
   in one step: row B is a copy of row A with every typed Dst address
   raised by the row step, the two TTINCRWC(+S) separators become one
   TTINCRWC(+2S), the induction step doubles, and the rows are zipped so
   each statement of row A is followed by its row-B twin.  Every statement
   keeps its operation and its operands; only the order across the two
   rows changes, and only where the rows are proven independent:

   - shape: an innermost single-block counted loop on Blackhole whose trip
     count is proven even, with one integral induction variable stepped by
     a constant and tested after the step (so the doubled loop runs exactly
     the original's odd-numbered exit tests), and whose body is typed SFPU
     builtins closed by a single Dst-only TTINCRWC;
   - independence: no loop-carried or live-out row value, no scalar
     operand computed in the row, and Dst touched only by typed loads and
     stores in the no-increment mode at constant addresses that stay in
     range after the rebase.  An access selects an aligned group of four
     rows and one column parity (address bit 1; SFPLOAD.md, "Functional
     model"), so a row-B access and a row-A access of which one is a store
     must differ in parity, or sit in different row groups of the same
     Dst view (Dst.md: the 16-bit and 32-bit views map rows differently);
   - lane state: every CC writer belongs to an atom that ends with the
     word-exact all-lanes SFPENCC, an atom is never split or entered by
     the other row, and the ambient state on the loop's entry edge is
     proven all-lanes, so every statement outside an atom runs with all
     lanes enabled in both orders;
   - PRNG order: every SFPSTOCHRND advances the per-lane PRNG even in its
     deterministic modes, so no row-B advance moves above a row-A advance.

   Two value-identical simplifications follow from the zip.  A literal
   constant materialization (an SFPLOADI chain) outside any atom is issued
   once for both rows.  When the doubled live set exceeds the LREG file,
   loop-invariant literal materializations are re-issued inside the loop
   once per pair, ahead of the pair's first use, shortest in-loop span
   first, instead of being held across the loop; nothing else is ever
   evicted, so a pair that still does not fit refuses.

   The rewrite commits only on a modeled gain: the pair, issued in the new
   order with the audited result latencies (rvtt_builtin_result_latency)
   and delivered word counts (rvtt_replay_unroll_row_words), must take
   fewer cycles than two rows issued back to back.  A row that already
   issues without exposed latency, or whose evicted constants cost more
   words than the stalls they hide, refuses by name.  The function-wide
   pressure model (rvtt_pressure_compute) then has to agree that the block
   still fits the LREG file, or the rewrite is undone exactly.

   Runs under -mtt-tensix-optimize-dst-row-interleave, after the combiner
   has formed the final SFPMAD chains and before the fused-group interleave
   above.  */

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
   row B's twin of it, or the in-loop materialization of evicted invariant
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

/* Census of the body: classify every statement, collect the invariant
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

/* CC atoms: from a CC writer through the next word-exact all-lanes
   SFPENCC.  A writer left open at the row end would carry lane state
   into the next row.  */

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

/* The ambient lane state on the loop's entry edge is all-lanes: walking
   back through single-predecessor blocks, the nearest CC writer is the
   all-lanes SFPENCC, or the walk reaches the function entry (all-lanes by
   the structured-CC lowering contract).  A call, an asm or an unaudited
   word may hand back any state.  */

bool
row_interleave::entry_all_lanes_p () const
{
  for (basic_block cur = entry->src; cur != ENTRY_BLOCK_PTR_FOR_FN (cfun);
       cur = single_pred (cur))
    {
      for (gimple_stmt_iterator gsi = gsi_last_bb (cur); !gsi_end_p (gsi);
	   gsi_prev (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (gimple_code (stmt) == GIMPLE_ASM)
	    return false;
	  gcall *call = dyn_cast <gcall *> (stmt);
	  if (!call)
	    continue;
	  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	  if (!insnd)
	    return false;
	  if (rvtt_encc_all_lanes_call_p (call, insnd)
	      || insnd->id == rvtt_insn_data::sfpencc_all_lanes)
	    return true;
	  if (rvtt_cc_writer_id_p (insnd->id) || insnd->sets_cc (call))
	    return false;
	  /* Counter and address-modifier words, and SFPCONFIG programming of
	     the constant registers (dests 12-14; LaneConfig is dest 15),
	     leave lane state alone; anything else unaudited might not.  */
	  if (insnd->id == rvtt_insn_data::sfpwriteconfig_v)
	    {
	      unsigned mod, dest;
	      if (rvtt_call_const_uarg (call, 1, &mod) && mod == 0
		  && rvtt_call_const_uarg (call, 2, &dest)
		  && dest >= 12 && dest <= 14)
		continue;
	      return false;
	    }
	  if (!rvtt_cc_quiet_compute_id_p (insnd->id)
	      && insnd->id != rvtt_insn_data::ttincrwc
	      && insnd->id != rvtt_insn_data::ttsetc16)
	    return false;
	}
      if (!single_pred_p (cur))
	return false;
    }
  return true;
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
   row-B twin, unless the twin advances the PRNG while row A still has an
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
   own twin, except of a shared materialization.  */

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

/* Two rows issued back to back, as the loop issues them today.  */

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
  std::unordered_map<tree, tree> twin;	/* row-A name -> row-B name */
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
	return twin.at (arg);
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
	twin.emplace (lhs, gimple_call_lhs (copy));
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
