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

#define INCLUDE_VECTOR
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

// Is TYPE an sfpu vector type?

static bool
is_sfpu_type (tree type)
{
  return TREE_CODE (type) == VECTOR_TYPE
    && lookup_attribute ("__xtt_vector", TYPE_ATTRIBUTES (type));
}

// We can't pass or return sfpu vector types.  Barf if we are.

static bool
validate_function_type (location_t loc, tree type, bool inline_p)
{
  bool bad = false;

  if (is_sfpu_type (TREE_TYPE (type)))
    {
      bad = true;
      error_at (loc, "cannot return sfpu type%s",
  		inline_p ? " (missing <%sfpi_inline%>?)" : "");
    }

  for (tree args = TYPE_ARG_TYPES (type); args; args = TREE_CHAIN (args))
    if (is_sfpu_type (TREE_VALUE (args)))
      {
	bad = true;
	error_at (loc, "cannot pass sfpu type%s",
		  inline_p ? " (missing <%sfpi_inline%>?)" : "");
	break;
      }
  return bad;
}

// Check integral arguments passed to CALL, an INSND are within range.

static bool
validate_integral_args (const rvtt_insn_data *insnd, gcall *call)
{
  bool changed = false;

  for (unsigned ix = 0; auto info = insnd->ops[ix]; ix++)
    {
      if (info.is_runtime ())
	continue;
      if (info.is_xmod ())
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

      HOST_WIDE_INT upper = 0;
      HOST_WIDE_INT lower = 0;
      val = TREE_INT_CST_LOW (op);
      unsigned bias = 0;

      if (info.is_mod ())
	upper = 15;
      else
	{
	  unsigned bits = info.bits ();
	  if (!bits)
	    bits = 32;
	  upper = (1u << (info.bits () - 1)) - 1;
	  if (info.kind () != rvtt_insn_data::op_t::UNSIGNED)
	    lower = ~upper;
	  if (info.kind () != rvtt_insn_data::op_t::SIGNED)
	    upper = (upper << 1) | 1;
	  bias = info.bias ();
	  if (info.is_upper ())
	    {
	      // sfpxloadi hack. Bleah!
	      upper <<= 16;
	      lower <<= 16;
	    }
	}

      if (val > upper + bias || val < lower + bias)
	{
	  if (info.is_checked ())
	    error_at (gimple_location (call),
		      "argument %d %qE is out of range [%ld, %ld]",
		      info.argno () + 1, op, (long long)lower, (long long)upper);
	  if (info.is_mod ())
	    goto zap;
	  if (info.is_checked () || !insnd->has_var () || info.argno () != insnd->imm_arg ())
	    {
	      // Clip imm operands.  Keep nonnimm operands for for the moment,
	      // until we fix sfpxloadi
	      if (info.kind () == rvtt_insn_data::op_t::SIGNED)
		upper = (upper << 1) | 1;
	      val = ((val - bias) & upper) + bias;
	      goto zap;
	    }
	}

      if (info.is_mod () && !((1 << val) & info.mod ()))
	{
	  unsigned mask = info.mod ();
	  error_at (gimple_location (call),
		    "argument %d %qE is invalid mod1 value (mask is 0x%x)",
		    info.argno () + 1, op, mask);
	  goto zap;
	}
    }

  return changed;
}

static bool
is_undef_sfpu (tree val)
{
  return SSA_VAR_P (val)
    && ssa_name_maybe_undef_p (val)
    && is_sfpu_type (TREE_TYPE (val));
}

static bool
is_memory (tree arg)
{
  return (TREE_CODE (arg) == MEM_REF
	  || TREE_CODE (arg) == COMPONENT_REF)
    && is_sfpu_type (TREE_TYPE (arg));
}

static tree
const_zero_reg (gimple *stmt, tree arg)
{
  auto *insnd = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcall *new_stmt = gimple_build_call (insnd->decl, 1);
  tree reg = make_ssa_name (TREE_TYPE (arg), new_stmt);

  gimple_call_set_arg (new_stmt, 0, build_int_cst (unsigned_type_node, 8));
  gimple_set_location (new_stmt, gimple_location (stmt));
  gimple_call_set_lhs (new_stmt, reg);
  auto gsi = gsi_for_stmt (stmt);
  gsi_insert_before (&gsi, new_stmt, GSI_SAME_STMT);

  return reg;
}

static bool
validate_assign (gassign *assign)
{
  bool changed = false;

  auto *ops = gimple_ops (assign);
  unsigned limit = gimple_num_ops (assign);
  for (unsigned opno = 0; opno != limit; opno++)
    if (auto op = ops[opno])
      {
	if (!opno)
	  {
	    if (is_memory (op))
	      {
		error_at (gimple_nonartificial_location (assign),
			  "cannot write SFPU object to memory");
		ops[opno] = nullptr;
		update_stmt (assign);
		changed = true;
	      }
	  }
	else
	  {
	    bool is_undef = is_undef_sfpu (op);
	    bool is_mem = is_memory (op);
	    if (is_undef || is_mem)
	      {
		error_at (gimple_nonartificial_location (assign),
			  is_undef ? "rhs operand %d is uninitialized"
			  : "rhs operand %d cannot read SFPU object from memory",
			  opno);
		ops[opno] = const_zero_reg (assign, ops[opno]);
		update_stmt (assign);
		changed = true;
	      }
	  }
      }

  return changed;
}

static unsigned
validate (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	
	gcall *call;
	const rvtt_insn_data *insnd;
	if (rvtt_p (&insnd, &call, gsi))
	  {
	    changed |= validate_integral_args (insnd, call);
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
					 const_zero_reg (call, arg));
		    update_stmt (call);
		    changed = true;
		  }
	      }
	  }
	else if (auto *call = dyn_cast<gcall *> (*gsi))
	  changed |= validate_function_type (gimple_nonartificial_location (call),
					     gimple_call_fntype (call), true);
	else if (auto *a = dyn_cast<gassign *> (*gsi))
	  changed |= validate_assign (a);
      }
  return changed ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_validate =
{
  GIMPLE_PASS, /* type */
  "rvtt_validate", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_validate : public gimple_opt_pass
{
public:
  pass_rvtt_validate (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_validate, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    validate_function_type (DECL_SOURCE_LOCATION (fn->decl), TREE_TYPE (fn->decl), false);
    return validate (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_validate (gcc::context *ctxt)
{
  return new pass_rvtt_validate (ctxt);
}
