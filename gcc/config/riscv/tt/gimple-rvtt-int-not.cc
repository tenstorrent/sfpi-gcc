/* Select the single SFPNOT for the all-ones-minus-x value function.
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

/* -mtt-tensix-optimize-int-not (default off).

   A one's complement stated at the value level,

       r = vInt(-1) - v;        // ~v == (-1) - v in two's complement

   reaches this pass as

       ones = <all-ones materialization>
       r    = sfpiadd_v (v, ones, ARG_2SCOMP_LREG_DST|CC_NONE)   ; ones - v

   which delivers two words (the hoisted or in-loop SFPLOADI -1 plus the
   SFPIADD) and carries the all-ones LREG live range.  The same per-lane
   function is ONE vector word with no register dependence:

       r = SFPNOT (v)

   Bit-exactness (all 2^32 v per lane, from the pinned simulator models
   -- the reference simulator's TENSIX_EXECUTE_SFPIADD mod1&2 arm
   src(C) -= LReg[dest]
   two's-complement wrap subtract vs TENSIX_EXECUTE_SFPNOT ~src_c):
   0xFFFFFFFF - v never borrows below any bit (the minuend is all-ones),
   so the wrapping subtract IS the bitwise complement.  The exhaustive
   host sweep (mismatches = 0 over 2^32, streams hash-identical) ships
   in tt/proofs/int-not-allones-subtract/; per the tt/proofs README
   contract this fold may fire ONLY while that RESULT is EQUAL.

   Predication and side-channel equivalence: both instructions write the
   destination only on CC-enabled lanes through the simulator's shared
   for_each_lane masked walk, and neither writes CC (the matched SFPIADD
   mod carries CC_NONE; SFPNOT has no CC logic at all).  The fold
   therefore replaces the sfpiadd_v statement in place -- inside or
   outside a structured CC region -- and leaves every merge (assign_lv)
   and the region skeleton untouched.  A CC-setting spelling of the
   subtract (mod1 CC_LT0 or CC_GTE0) is NOT this value function plus a
   dead flag: it publishes a lane-flag write SFPNOT cannot express, so
   it refuses by name (int-not-cc-mode-unsupported).

   The all-ones materialization is deleted only when the folded subtract
   was its last use (the int-abs zero-materialization discipline).

   The proof ran against the shared TT_VERSION<=1 simulator arm that
   both pinned oracles (BH 32489dda..., WH 8f0079a9...) compile;
   the pass fires on those two targets and fails closed elsewhere
   (int-not-target-unproven).

   The pass runs beside the ccmask/int-abs folds before the invariant
   pass: removing the subtract kills the all-ones use, so the loop no
   longer carries the -1 immediate at all (instead of hoisting it).  */

#define INCLUDE_ALGORITHM
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
#include "gimple-pretty-print.h"
#include "tree-cfg.h"
#include "rvtt.h"
#include "rvtt-refuse.h"

namespace {

static unsigned n_folded;

static long
int_arg (gcall *call, unsigned n)
{
  tree arg = gimple_call_arg (call, n);
  return TREE_CODE (arg) == INTEGER_CST ? TREE_INT_CST_LOW (arg) : -1;
}

static bool
refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "int-not refused (%s): ", reason);
  if (dump_file)
    print_gimple_stmt (dump_file, stmt, 0);
  return false;
}

/* Return true when VAL is an architectural all-ones vector: an immediate
   materialization whose 32-bit value is 0xFFFFFFFF.  The value argument
   of the synthetic 32-bit load is authoritative at this pipeline stage
   (the int-abs zero_vector_p precedent).  */

static bool
all_ones_vector_p (tree val)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (def);
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxloadi:
      /* Only the synthetic 32-bit load, whose value argument IS the
	 architectural 32-bit value.  A raw sfploadi's value argument is
	 mod-interpreted (sign/zero-extending 16-bit forms), so an
	 all-ones LITERAL there is not mod-invariant the way int-abs's
	 zero is -- raw forms never match.  */
      {
	/* (ib, value, ...) -- LITERAL-constant argument forms only: the
	   int_arg "not a constant" sentinel (-1) would collide with the
	   all-ones value itself, and variable-immediate loads (the immvar
	   RISC-composed forms) must never match.  */
	tree varg = gimple_call_arg (call, 1);
	if (TREE_CODE (varg) != INTEGER_CST)
	  return false;
	return (TREE_INT_CST_LOW (varg) & 0xFFFFFFFFul) == 0xFFFFFFFFul;
      }
    default:
      return false;
    }
}

/* Try to fold the sfpiadd_v at GSI.  Returns true when folded.  */

static bool
fold_iadd (gcall *iadd)
{
  long mod = int_arg (iadd, 2);
  if (mod < 0 || !(mod & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST))
    return false;

  /* sfpiadd_v (A, B, ARG_2SCOMP_LREG_DST) computes B - A.  The minuend
     B must be architectural all-ones for the subtract to be the bitwise
     complement of A.  */
  tree x = gimple_call_arg (iadd, 0);
  tree ones = gimple_call_arg (iadd, 1);
  if (!all_ones_vector_p (ones))
    return false;
  if (TREE_CODE (x) != SSA_NAME)
    return refuse ("int-not-operand-form", iadd);

  /* Candidate identified: an all-ones-minus-x subtract.  A CC-writing
     mod publishes a lane-flag side channel SFPNOT cannot express.  */
  if ((unsigned) mod
      != (SFPIADD_MOD1_ARG_2SCOMP_LREG_DST | SFPIADD_MOD1_CC_NONE))
    return refuse ("int-not-cc-mode-unsupported", iadd);

  const rvtt_insn_data *not_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpnot);
  gcc_assert (not_insnd->decl);

  gimple_stmt_iterator at = gsi_for_stmt (iadd);
  location_t loc = gimple_location (iadd);
  gcall *notcall = gimple_build_call (not_insnd->decl, 1, x);
  gimple_call_set_lhs (notcall, gimple_call_lhs (iadd));
  gimple_set_location (notcall, loc);

  /* Identify the all-ones materialization BEFORE replacing its use:
     rvtt_prep_stmt_for_deletion (via gsi_replace's def bookkeeping)
     interacts with single-use argument chains (the int-abs lesson).  */
  gimple *onesdef = SSA_NAME_DEF_STMT (ones);

  gsi_replace (&at, notcall, false);

  if (dump_file)
    {
      fprintf (dump_file, "int-not: folded all-ones subtract into ");
      print_gimple_stmt (dump_file, notcall, 0);
    }

  /* The all-ones materialization is dead once its last subtract is
     folded; delete it so the invariant pass never sees a use-free
     immediate to hoist.  Keep it when other uses survive.  */
  if (onesdef && gimple_bb (onesdef) && rvtt_get_insn_data (onesdef))
    {
      tree lhs = gimple_call_lhs (onesdef);
      if (lhs && TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs))
	{
	  rvtt_prep_stmt_for_deletion (onesdef);
	  unlink_stmt_vdef (onesdef);
	  gimple_stmt_iterator gsi = gsi_for_stmt (onesdef);
	  gsi_remove (&gsi, true);
	  release_defs (onesdef);
	}
    }

  n_folded++;
  return true;
}

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (gsi_stmt (gsi));
	if (insnd && insnd->id == rvtt_insn_data::sfpiadd_v)
	  changed |= fold_iadd (as_a <gcall *> (gsi_stmt (gsi)));
      }
  return changed;
}

const pass_data pass_data_rvtt_int_not =
{
  GIMPLE_PASS,
  "rvtt_int_not",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_int_not : public gimple_opt_pass
{
public:
  pass_rvtt_int_not (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_int_not, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_int_not > 0;
  }

  unsigned execute (function *fn) final override
  {
    /* The equivalence is proven against the shared TT_VERSION<=1
       simulator arm both pinned oracles compile (BH and WH); no QSR
       oracle is pinned.  Fail closed everywhere the proof was not
       run.  */
    if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
      {
	rvtt_refuse (RVTT_REF_INT_NOT_TARGET_UNPROVEN, dump_file,
		     "int-not refused (int-not-target-unproven)\n");
	return 0;
      }
    n_folded = 0;
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file, "int-not: folds=%u\n", n_folded);
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_int_not (gcc::context *ctxt)
{
  return new pass_rvtt_int_not (ctxt);
}
