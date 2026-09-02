/* Pass to issue diagnostics for SFPU operations
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

/* Two diagnostic passes over SFPU intrinsic usage, sharing one set of
   checkers.  Both repair what they diagnose (substituting an in-range
   value, the constant-zero register, or deleting the offending
   statement) so that a single bad kernel produces one clear error
   rather than a cascade of RTL-expansion failures or assembler barf.

   rvtt_check_early (right after inlining):
   - function signatures may not pass or return SFPU vector values
     (they live in SFPU registers, outside the RISC-V ABI; functions
     taking them must be inlined, hence the sfpi_inline hint);
   - compile-time-constant ("early") integer operands must be in range
     for their instruction fields, and mod operands in each op's
     legal-mod mask;
   - reads of uninitialized SFPU values are diagnosed once, here, where
     SSA undefinedness is still exact;
   - target-specific usage errors: the Quasar replay erratum
     (exec-while-load unsupported, under -mtt-fix-qsrreplay) and the
     immediate-form SFPCONFIG destination envelope (only LaneConfig,
     destination 15, is audited for direct programming).

   rvtt_check_late (end of the GIMPLE pipeline): re-checks integer
   operands whose constancy is only required by expand time ("late"
   operands, now possibly constant-propagated), and rejects
   reads/writes of SFPU objects from/to memory -- SFPU values cannot
   live in memory, and by this point no further optimization could
   have eliminated the access.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "stringpool.h"
#include "attribs.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "tree-ssa.h"
#include "tree-into-ssa.h"
#include "diagnostic-core.h"
#include "rvtt.h"

/* Is TYPE an SFPU vector type?  */

static bool
is_sfpu_type (tree type)
{
  return TREE_CODE (type) == VECTOR_TYPE
    && lookup_attribute ("__xtt_vector", TYPE_ATTRIBUTES (type));
}

/* Diagnose function type TYPE passing or returning SFPU vector values
   (impossible: they have no ABI representation).  INLINE_P selects the
   hint suggesting sfpi_inline.  Returns true if anything was
   diagnosed.  */

static bool
check_function_type (location_t loc, tree type, bool inline_p)
{
  bool bad = false;
  auto error = [](location_t loc, bool ret, bool inline_p) {
    error_at (loc, inline_p
	      ? "cannot %s sfpu type (missing %<sfpi_inline%>?)"
	      : "cannot %s sfpu type",
	      ret ? "return" : "pass");
  };

  if (is_sfpu_type (TREE_TYPE (type)))
    {
      bad = true;
      error (loc, true, inline_p);
    }

  for (tree args = TYPE_ARG_TYPES (type); args; args = TREE_CHAIN (args))
    if (is_sfpu_type (TREE_VALUE (args)))
      {
	bad = true;
	error (loc, false, inline_p);
	break;
      }
  return bad;
}


/* Check the integral arguments of CALL (an INSND intrinsic): constant
   where required, within field range, and mod values within the op's
   legal mask.  IS_EARLY selects which operand class to check (early
   operands must be constant at the early pass, late ones only by
   expand).  Out-of-range values are diagnosed (when the operand is
   marked checked) and repaired -- clipped into the field or replaced
   by the smallest legal value -- so compilation can continue.
   Returns true if any argument was rewritten.  */

static bool
check_int_args (bool is_early, const rvtt_insn_data *insnd, gcall *call)
{
  bool changed = false;

  for (unsigned ix = 0; auto info = insnd->ops[ix]; ix++)
    {
      if (info.is_runtime ())
	continue;
      if (is_early != info.is_early ())
	continue;

      HOST_WIDE_INT val = 0;
      tree op = gimple_call_arg (call, info.argno ());
      if (TREE_CODE (op) != INTEGER_CST)
	{
	  if (!ix && insnd->has_var ())
	    continue;
	  error_at (gimple_nonartificial_location (call),
		    "argument %d is not a constant", info.argno () + 1);
	zap:
	  // If we don't make this correction, we'll likely crash, fail
	  // at RTL expansion or the assembler will barf
	  if (info.is_mod ())
	    {
	      // Compute the smallest valid mod value
	      unsigned mod = __builtin_ffs (info.mod ());
	      val = mod - (mod != 0);
	    }
	  tree zap = build_int_cst (TREE_TYPE (op), val);
	  gimple_call_set_arg (call, info.argno (), zap);
	  update_stmt (call);
	  changed = true;
	  continue;
	}

      val = TREE_INT_CST_LOW (op);

      if (!info.is_xmod ())
	{
	  HOST_WIDE_INT upper = 0xf;
	  HOST_WIDE_INT lower = 0;
	  unsigned bias = 0;
	  if (!info.is_mod ())
	    {
	      unsigned bits = info.bits ();
	      if (!bits)
		bits = 32;
	      upper = (1u << (bits - 1)) - 1;
	      if (info.kind () != rvtt_insn_data::op_t::UNSIGNED)
		lower = ~upper;
	      if (info.kind () != rvtt_insn_data::op_t::SIGNED)
		upper = (upper << 1) | 1;
	      bias = info.bias ();
	    }

	  if (val > upper + bias || val < lower + bias)
	    {
	      if (info.is_checked ())
		error_at (gimple_location (call),
			  "argument %d %qE is out of range [%ld, %ld]",
			  info.argno () + 1, op,
			  long (lower + bias), long (upper + bias));

	      if (info.is_mod ())
		goto zap;

	      // Clip imm operands.  Keep nonnimm operands for for the moment,
	      // until we fix sfpxloadi
	      val -= bias;
	      HOST_WIDE_INT sign_bits
		= info.kind () == rvtt_insn_data::op_t::SIGNED && (val & lower)
		? ~upper : 0;
	      val = (val & upper) | sign_bits;
	      val += bias;
	      goto zap;
	    }
	}

      if (info.is_mod () || info.is_xmod ())
	{
	  unsigned mask = info.mod ();
	  if (!((1 << (val & 0xf)) & info.mod ()))
	    {
	      error_at (gimple_location (call),
			"argument %d %qE is invalid mod1 value (mask is 0x%x)",
			info.argno () + 1, op, mask);
	      goto zap;
	    }
	}
    }

  return changed;
}

/* Is VAL an uninitialized SSA SFPU vector value?  */

static bool
is_undef_sfpu (tree val)
{
  return SSA_VAR_P (val)
    && is_sfpu_type (TREE_TYPE (val))
    && ssa_undefined_value_p (val, false);
}

/* Is ARG a memory reference to an SFPU vector object?  */

static bool
is_memory (tree arg)
{
  return (TREE_CODE (arg) == MEM_REF
	  || TREE_CODE (arg) == COMPONENT_REF)
    && is_sfpu_type (TREE_TYPE (arg));
}

/* Insert, before GSI, a read of the constant-zero SFPU register into
   RES (a fresh SSA name if none given); the repair value substituted
   for erroneous operands.  Returns RES.  */

static tree
const_zero_reg (gimple_stmt_iterator *gsi, tree res = nullptr)
{
  auto *insnd = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcall *new_stmt = gimple_build_call (insnd->decl, 1);

  if (!res)
    res = make_ssa_name (TREE_TYPE (TREE_TYPE (insnd->decl)));

  gimple_call_set_arg (new_stmt, 0, build_int_cst (unsigned_type_node, CREG_IDX_0));
  gimple_set_location (new_stmt, gimple_location (**gsi));
  gimple_call_set_lhs (new_stmt, res);

  gsi_insert_before (gsi, new_stmt, GSI_SAME_STMT);

  return res;
}

/* Check assignment ASSIGN at GSI: early, diagnose reads of
   uninitialized SFPU values; late, diagnose SFPU objects read from or
   written to memory.  A diagnosed assignment is removed (its LHS, if
   any, is fed by a constant-zero read).  Returns true (with GSI
   already advanced) if the statement was removed.  */

static bool
check_assign (gimple_stmt_iterator *gsi, bool is_early, gassign *assign)
{
  if (!is_early)
    if (auto *lhs = gimple_get_lhs (assign))
      if (is_memory (lhs))
	{
	  error_at (gimple_nonartificial_location (assign),
		    "cannot write SFPU object to memory");
	  goto remove;
	}

  {
    auto *rhs = gimple_assign_rhs1 (assign);
    if (is_early ? is_undef_sfpu (rhs) : is_memory (rhs))
      {
	error_at (gimple_nonartificial_location (assign),
		  is_early ? "reading uninitialized"
		  : "cannot read SFPU object from memory");
	if (auto *lhs = gimple_get_lhs (assign))
	  const_zero_reg (gsi, lhs);
	goto remove;
      }
  }

  return false;

 remove:;
  unlink_stmt_vdef (assign);
  gsi_remove (gsi, true);
  return true;
}

/* The rvtt_check_early pass body; see the file comment.  */

static unsigned
check_early (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi);)
      {
	if (auto *insnd = rvtt_get_insn_data (*gsi))
	  {
	    auto *call = as_a <gcall *> (*gsi);
	    changed |= check_int_args (true, insnd, call);

	    if (riscv_tt_fix_qsr_replay > 0 && insnd->id == rvtt_insn_data::ttreplay)
	      {
		// Quasar erratum, cannot exec-while-loading
		tree op = gimple_call_arg (call, insnd->imm_arg () + 4);
		if (!integer_zerop (op))
		  error_at (gimple_nonartificial_location (call),
			    "Quasar replay instruction cannot exec-while-load, "
			    "your program will behave erratically");
	      }

	    if (insnd->id == rvtt_insn_data::sfpconfig_i)
	      {
		// Audited destination envelope of the immediate-form
		// SFPCONFIG: LaneConfig (15) only.  Dests 0-8 program
		// LoadMacroConfig (macro-planner-owned configuration),
		// 9-10 are NonContractualBehavior, and 11-14 imm-form
		// writes touch the programmable constants without the
		// prgm-const pass modeling them (SFPCONFIG.md functional
		// model; rvtt-insn.def provenance block).
		tree dest = gimple_call_arg (call, 1);
		if (TREE_CODE (dest) == INTEGER_CST
		    && TREE_INT_CST_LOW (dest) != 15)
		  {
		    error_at (gimple_nonartificial_location (call),
			      "sfpconfig-imm-dest-unaudited: immediate-form "
			      "SFPCONFIG destination %d is outside the audited "
			      "envelope (only LaneConfig, destination 15)",
			      int (TREE_INT_CST_LOW (dest)));
		    gimple_call_set_arg
		      (call, 1, build_int_cst (TREE_TYPE (dest), 15));
		    update_stmt (call);
		    changed = true;
		  }
	      }

	    for (unsigned argno = 0, limit = gimple_call_num_args (call);
		 argno != limit; argno++)
	      {
		tree arg = gimple_call_arg (call, argno);
		if (is_undef_sfpu (arg))
		  {
		    error_at (gimple_nonartificial_location (call),
			      "argument %d of %qE is not initialized", argno + 1,
			      gimple_call_fndecl (call));
		    gimple_call_set_arg (call, argno,
					 const_zero_reg (&gsi));
		    update_stmt (call);
		    changed = true;
		  }
	      }
	  }
	else if (auto *a = dyn_cast<gassign *> (*gsi))
	  {
	    if (check_assign (&gsi, true, a))
	      {
		changed = true;
		continue;
	      }
	  }
	else if (auto *call = dyn_cast<gcall *> (*gsi))
	  {
	    if (tree type = gimple_call_fntype (call))
	      if (check_function_type (gimple_nonartificial_location (call),
				       type, true))
		{
		  // Delete call, set lhs to something
		}
	  }
	gsi_next (&gsi);
   }
  return changed ? TODO_update_ssa : 0;
}

/* The rvtt_check_late pass body; see the file comment.  */

static unsigned
check_late (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi);)
      {
	if (auto *insnd = rvtt_get_insn_data (*gsi))
	  {
	    auto *call = as_a <gcall *> (*gsi);
	    changed |= check_int_args (false, insnd, call);
	  }
	else if (auto *a = dyn_cast<gassign *> (*gsi))
	  {
	    if (check_assign (&gsi, false, a))
	      {
		changed = true;
		continue;
	      }
	  }
	gsi_next (&gsi);
      }
  return changed ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_check_early =
{
  GIMPLE_PASS, /* type */
  "rvtt_check_early", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_check_early : public gimple_opt_pass
{
public:
  pass_rvtt_check_early (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_check_early, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    check_function_type (DECL_SOURCE_LOCATION (fn->decl), TREE_TYPE (fn->decl), false);
    return check_early (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_check_early (gcc::context *ctxt)
{
  return new pass_rvtt_check_early (ctxt);
}

namespace {

const pass_data pass_data_rvtt_check_late =
{
  GIMPLE_PASS, /* type */
  "rvtt_check_late", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_check_late : public gimple_opt_pass
{
public:
  pass_rvtt_check_late (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_check_late, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    return check_late (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_check_late (gcc::context *ctxt)
{
  return new pass_rvtt_check_late (ctxt);
}
