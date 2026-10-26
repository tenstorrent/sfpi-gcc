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

/* Replace a one's complement written as an all-ones subtract with
   SFPNOT (-mtt-tensix-optimize-int-not, default off).

   The source form r = vInt(-1) - v reaches this pass as

       ones = <all-ones load>
       r    = sfpiadd_v (v, ones, ARG_2SCOMP_LREG_DST|CC_NONE)   ; ones - v

   which costs two instructions and an LREG for the all-ones value.
   The same per-lane function is the single instruction r = SFPNOT (v):
   0xFFFFFFFF - v never borrows, so the wrapping subtract is the
   bitwise complement: subtracting each bit of v from one produces
   the inverted bit without borrowing from the next position.

   Both instructions write the destination only on enabled lanes and
   neither writes CC (the matched SFPIADD mod has CC_NONE), so the
   statement is replaced in place, inside or outside a CC region.  A
   CC-setting subtract (CC_LT0 or CC_GTE0) also writes the lane flags,
   which SFPNOT cannot do, and is refused
   (int-not-cc-mode-unsupported).  The all-ones load is deleted when
   the folded subtract was its last use.

   The match is limited to the wrapping integer subtract and NOT
   forms on Blackhole and Wormhole; other targets are refused
   (int-not-target-unproven).

   The pass runs before the invariant pass, so a loop no longer carries
   the -1 immediate at all instead of hoisting it.  */

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

/* Record the named refusal REASON against STMT and dump it.  Returns
   false.  */

static bool
refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "int-not refused (%s): ", reason);
  if (dump_file)
    print_gimple_stmt (dump_file, stmt, 0);
  return false;
}

/* Return true when VAL is an all-ones vector: an immediate load whose
   32-bit value is 0xFFFFFFFF.  */

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
      /* Only the synthetic 32-bit load, whose value argument is the
	 32-bit value.  A plain sfploadi's value is interpreted by its mod
	 (sign- or zero-extending 16-bit forms), so it does not match.  */
      {
	/* (ib, value, ...).  Require a literal constant; variable
	   immediates must not match, and rvtt_call_int_arg's -1 "not a
	   constant" sentinel would be indistinguishable from all-ones.  */
	tree varg = gimple_call_arg (call, 1);
	if (TREE_CODE (varg) != INTEGER_CST)
	  return false;
	return (TREE_INT_CST_LOW (varg) & 0xFFFFFFFFul) == 0xFFFFFFFFul;
      }
    default:
      return false;
    }
}

/* Try to fold the sfpiadd_v call IADD.  Returns true when folded.  */

static bool
fold_iadd (gcall *iadd)
{
  tree mode = gimple_call_arg (iadd, 2);
  if (TREE_CODE (mode) != INTEGER_CST)
    return false;
  long mod = TREE_INT_CST_LOW (mode);
  if (mod < 0 || !(mod & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST))
    return false;

  /* sfpiadd_v (A, B, ARG_2SCOMP_LREG_DST) computes B - A.  The minuend
     B must be all-ones for the subtract to be the bitwise complement
     of A.  */
  tree x = gimple_call_arg (iadd, 0);
  tree ones = gimple_call_arg (iadd, 1);
  if (!all_ones_vector_p (ones))
    return false;
  if (TREE_CODE (x) != SSA_NAME)
    return refuse ("int-not-operand-form", iadd);

  /* A CC-writing mod also writes the lane flags, which SFPNOT cannot
     do.  */
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

  /* Find the all-ones load before replacing its use: replacing the
     statement can strip the lhs of a single-use argument's defining
     call.  */
  gimple *onesdef = SSA_NAME_DEF_STMT (ones);

  gsi_replace (&at, notcall, false);

  if (dump_file)
    {
      fprintf (dump_file, "int-not: folded all-ones subtract into ");
      print_gimple_stmt (dump_file, notcall, 0);
    }

  /* Delete the all-ones load if this was its last use, so the
     invariant pass does not hoist it.  */
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

/* Walk every statement of FUN and offer each sfpiadd_v to the
   all-ones-subtract fold.  Returns whether the IL changed.  */

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
    /* The equivalence has only been checked for Blackhole and
       Wormhole.  */
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
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the int-not pass.  */

gimple_opt_pass *
make_pass_rvtt_int_not (gcc::context *ctxt)
{
  return new pass_rvtt_int_not (ctxt);
}
