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
#include "gimple-pretty-print.h"
#include "tree-ssa.h"
#include "tree-into-ssa.h"
#include "diagnostic-core.h"
#include "rvtt.h"

static tree
emit_sfploadi (gimple_stmt_iterator &gsi, location_t loc, unsigned mod,
	       tree lv, tree addr, tree val, tree res)
{
  const auto *insnd = rvtt_get_insn_data (rvtt_insn_data::sfploadi) + bool (lv);

  if (!res)
    res = make_ssa_name (TREE_TYPE (TREE_TYPE (insnd->decl)));

  auto *stmt = gimple_build_call (insnd->decl, insnd->num_args ());
  gimple_set_location (stmt, loc);
  gimple_call_set_arg (stmt, 0, addr);
  if (lv)
    gimple_call_set_arg (stmt, 1, lv);
  gimple_call_set_arg (stmt, insnd->imm_arg (), val);
  gimple_call_set_arg (stmt, insnd->var_arg (), integer_zero_node);
  gimple_call_set_arg (stmt, insnd->id_arg (), integer_zero_node);
  gimple_call_set_arg (stmt, insnd->mod_arg (), build_int_cst (unsigned_type_node, mod));
  gimple_call_set_lhs (stmt, res);
  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);

  return res;
}

// For now do the best we can just here.  Later we'll want a combining pass to
// handle the cases we get here, AND those that become constant.  BITS < 0 is
// unsigned, BITS >=0 is position of sign bit.

static tree
emit_loadimm (gimple_stmt_iterator &gsi, location_t loc, int bits,
	      tree addr, tree val, tree res = nullptr)
{
  unsigned mod = SFPLOADI_MOD0_USHORT;
  bool needs_both = false;

  if (bits >= 0)
    {
      if (bits < 16)
	mod = SFPLOADI_MOD0_SHORT;
      else
	needs_both = true;
    }
  else if (bits < -16)
    needs_both = true;

 tree lower = val;
  if (!needs_both)
    ;
  else if (SSA_VAR_P (lower))
    {
      lower = make_ssa_name (TREE_TYPE (lower));
      auto *lower_stmt = gimple_build_assign
	(lower, BIT_AND_EXPR, val, build_int_cst (unsigned_type_node, 0xffff));
      gimple_set_location (lower_stmt, loc);
      gsi_insert_before (&gsi, lower_stmt, GSI_SAME_STMT);
    }
  else
    lower = build_int_cst (TREE_TYPE (lower), uint32_t (TREE_INT_CST_LOW (lower)) & 0xffff);

  tree tmp = emit_sfploadi (gsi, loc, mod, nullptr, addr, lower,
			    needs_both ? nullptr : res);

  if (needs_both)
    {
      tree shift = nullptr;
      if (SSA_VAR_P (val))
	{
	  shift = make_ssa_name (TREE_TYPE (val));
	  auto *shift_stmt = gimple_build_assign
	    (shift, RSHIFT_EXPR, val, build_int_cst (unsigned_type_node, 16));
	  gimple_set_location (shift_stmt, loc);
	  gsi_insert_before (&gsi, shift_stmt, GSI_SAME_STMT);
	}
      else
	shift = build_int_cst (TREE_TYPE (val), uint32_t (TREE_INT_CST_LOW (val)) >> 16);

      res = emit_sfploadi (gsi, loc, SFPLOADI_MOD0_UPPER, tmp, addr, shift, res);
    }
  else
    res = tmp;

  return res;
}

static bool
emit_replacement (gimple_stmt_iterator &gsi, const rvtt_insn_data  *insnd, gcall *call,
		  rvtt_insn_data::insn_id id, bool imm_first, tree imm, tree mod)
{
  auto *new_insnd = rvtt_get_insn_data (id);
  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_set_location (stmt, gimple_location (call));
  gimple_call_set_arg (stmt, new_insnd->src_arg () + !imm_first, imm);
  gimple_call_set_arg (stmt, new_insnd->src_arg () + imm_first,
		       gimple_call_arg (call, insnd->src_arg ()));
  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
  return true;
}

static bool
immvar_split (gimple_stmt_iterator &gsi, const rvtt_insn_data *insnd, gcall *call)
{
  if (!insnd->has_var ())
    return false;
  tree imm = gimple_call_arg (call, insnd->imm_arg ());
  tree addr = insnd->has_var () ? gimple_call_arg (call, 0) : nullptr;
  tree mod = insnd->has_mod () ? gimple_call_arg (call, insnd->mod_arg ()) : nullptr;
  switch (insnd->id)
    {
    default:
      break;

    case rvtt_insn_data::sfpxloadi:
      {
	int bits = TREE_INT_CST_LOW (gimple_call_arg (call, 4));
	emit_loadimm (gsi, gimple_location (call),
		      bits, addr, imm, gimple_call_lhs (call));
	return true;
      }
    case rvtt_insn_data::sfpxicmps:
      if (SSA_VAR_P (imm))
	{
	  // This should totally be handled at the souce level.
	  unsigned imod = TREE_INT_CST_LOW (mod);
	  tree tmp = emit_loadimm (gsi,
				   gimple_location (call),
				   !(imod & SFPXIADD_MOD1_16BIT)
				   ? -32 : imod & SFPXIADD_MOD1_SIGNED ? 15 : -16,
				   addr, imm, nullptr);
	  mod = build_int_cst (unsigned_type_node, TREE_INT_CST_LOW (mod) & SFPXCMP_MOD1_CC_MASK);

	  return emit_replacement (gsi, insnd, call,
				   rvtt_insn_data::sfpxicmpv, true, tmp, mod);

	  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxicmpv);
	  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
	  gimple_set_location (stmt, gimple_location (call));
	  gimple_call_set_arg (stmt, new_insnd->src_arg (), tmp);
	  gimple_call_set_arg (stmt, new_insnd->src_arg () + 1,
			       gimple_call_arg (call, insnd->src_arg ()));
	  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
	  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
	  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
	  return true;
	}
      break;

    case rvtt_insn_data::sfpxfcmps:
      if (SSA_VAR_P (imm))
	{
	  unsigned imod = TREE_INT_CST_LOW (mod);
	  int fmt = imod & SFPXSCMP_MOD1_FMT_MASK;
	  tree tmp = nullptr;

	  if (fmt == SFPXSCMP_MOD1_FMT_A || fmt == SFPXSCMP_MOD1_FMT_B)
	    tmp = emit_sfploadi (gsi, gimple_location (call),
				 fmt == SFPXSCMP_MOD1_FMT_A ? SFPLOADI_MOD0_FLOATA : SFPLOADI_MOD0_FLOATB,
				 nullptr, addr, imm, nullptr);
	  else
	    tmp = emit_loadimm (gsi, gimple_location (call), -32, addr, imm, nullptr);
	  mod = build_int_cst (unsigned_type_node, imod & SFPXCMP_MOD1_CC_MASK);

	  return emit_replacement (gsi, insnd, call,
				   rvtt_insn_data::sfpxfcmpv, false, tmp, mod);

	  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxfcmpv);
	  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
	  gimple_set_location (stmt, gimple_location (call));
	  gimple_call_set_arg (stmt, new_insnd->src_arg (),
			       gimple_call_arg (call, insnd->src_arg ()));
	  gimple_call_set_arg (stmt, new_insnd->src_arg () + 1, tmp);
	  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
	  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
	  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
	  return true;
	}
      break;

    case rvtt_insn_data::sfpxiadd_i:
      if (SSA_VAR_P (imm))
	{
	  unsigned imod = TREE_INT_CST_LOW (mod);
	  tree tmp = emit_loadimm (gsi,
				   gimple_location (call),
				   !(imod & SFPXIADD_MOD1_16BIT)
				   ? -32 : imod & SFPXIADD_MOD1_SIGNED ? 15 : -16,
				   addr, imm, nullptr);
	  mod = build_int_cst (unsigned_type_node, imod & SFPXIADD_MOD1_IS_SUB);

	  return emit_replacement (gsi, insnd, call,
				   rvtt_insn_data::sfpxiadd_v, true, tmp, mod);

	  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxiadd_v);
	  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
	  gimple_set_location (stmt, gimple_location (call));
	  gimple_call_set_arg (stmt, new_insnd->src_arg (), tmp);
	  gimple_call_set_arg (stmt, new_insnd->src_arg () + 1,
			       gimple_call_arg (call, insnd->src_arg ()));
	  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
	  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
	  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
	  return true;
	}
      break;

    case rvtt_insn_data::sfpsetman_i:
      // setman only has a 12-bit immediate field
      if (SSA_VAR_P (imm))
	{
	  tree tmp = emit_loadimm (gsi, gimple_location (call), -24, addr, imm, nullptr);

	  return emit_replacement (gsi, insnd, call,
				   rvtt_insn_data::sfpsetman_v, false, tmp, mod);

	  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpsetman_v);
	  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
	  gimple_set_location (stmt, gimple_location (call));
	  gimple_call_set_arg (stmt, new_insnd->src_arg (),
			       gimple_call_arg (call, insnd->src_arg ()));
	  gimple_call_set_arg (stmt, new_insnd->src_arg () + 1, tmp);
	  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
	  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
	  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
	  return true;
	}
      break;
    }
  return false;
}

// FIXME: This would be better served by always splitting and having a more
// general combine imm pass later.

static bool
maybe_split_setman (gimple_stmt_iterator &gsi, const rvtt_insn_data *insnd, gcall *call)
{
  tree imm = gimple_call_arg (call, insnd->imm_arg ());
  HOST_WIDE_INT val = TREE_INT_CST_LOW (imm);
  if (val < 1 << 12)
    return false;

  tree mod = gimple_call_arg (call, insnd->mod_arg ());
  int bits = -24;
  if (val < 1 << 16)
    bits = -16;
  tree tmp = emit_loadimm (gsi, gimple_location (call), bits,
			   gimple_call_arg (call, 0),
			   imm, nullptr);
  auto *new_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpsetman_v);

  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_set_location (stmt, gimple_location (call));
  gimple_call_set_arg (stmt, 0, gimple_call_arg (call, insnd->src_arg ()));
  gimple_call_set_arg (stmt, 1, tmp);
  gimple_call_set_arg (stmt, 2, mod);
  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
  return true;
}

static bool
immvar_gather (gimple_stmt_iterator &gsi, const rvtt_insn_data *insnd,
	       gcall *call, std::vector<gcall *> &loads)
{
  if (!insnd->has_var ())
    return false;

  tree imm = gimple_call_arg (call, insnd->imm_arg ());
  if (SSA_VAR_P (imm))
    return false;

  bool changed = false;
  if (!integer_zerop (gimple_call_arg (call, 0)))
    {
      if (dump_file)
	{
	  fprintf (dump_file, "Immediate is now known: ");
	  print_gimple_stmt (dump_file, call, 0);
	}
      gimple_call_set_arg (call, 0, null_pointer_node);
      gimple_call_set_arg (call, insnd->var_arg (), integer_zero_node);
      gimple_call_set_arg (call, insnd->id_arg (), integer_zero_node);
      update_stmt (call);
      changed = true;
    }

  switch (insnd->id)
    {
    default:
      break;

    case rvtt_insn_data::sfpsetman_i:
      if (maybe_split_setman (gsi, insnd, call))
	{
	  gsi_remove (&gsi, true);
	  gsi_prev (&gsi);
	  changed = true;
	}
      break;

    case rvtt_insn_data::sfploadi:
      // We've not done LV optimizing yet, so we don't have to capture sfploadi_lv
      loads.push_back (call);
      break;
    }

  return changed;
}

static void
replace_loadi (gcall *call, gcall *earlier, int creg, int mod, tree val)
{
  const auto *new_insnd = rvtt_get_insn_data
    (creg >= 0 ? rvtt_insn_data::sfpreadlreg : rvtt_insn_data::sfploadi);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_set_location (new_stmt, gimple_location (call));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (call));
  if (creg >= 0)
    gimple_call_set_arg (new_stmt, 0, build_int_cst (unsigned_type_node, creg));
  else
    {
      gimple_call_set_arg (new_stmt, 0, null_pointer_node);
      gimple_call_set_arg (new_stmt, new_insnd->imm_arg (), val);
      gimple_call_set_arg (new_stmt, new_insnd->var_arg (), integer_zero_node);
      gimple_call_set_arg (new_stmt, new_insnd->id_arg (), integer_zero_node);
      gimple_call_set_arg (new_stmt, new_insnd->mod_arg (), build_int_cst (unsigned_type_node, mod));
    }

  if (dump_file)
    {
      fprintf (dump_file, "\nReplacing\n");
      if (earlier)
	print_gimple_stmt (dump_file, earlier, 2);
      print_gimple_stmt (dump_file, call, 2);
      fprintf (dump_file, "with\n");
      print_gimple_stmt (dump_file, new_stmt, 2);
    }

  auto gsi = gsi_for_stmt (call);
  gsi_insert_before (&gsi, new_stmt, GSI_SAME_STMT);
  gsi_remove (&gsi, true);
}

// CALL is an sfploadi call, can we simplify it (and the possibly-following
// sfploadi_lv?
// For the two-loadi case, the first one is SFPLOADI_MOD0_USHORT and the second
// is SFPLOADI_MOD0_UPPER.
// TODO: have a separate sfp DCE pass
static bool
immvar_simplify (gcall *call, std::vector<gcall *> uppers)
{
  const auto *insnd = rvtt_get_insn_data (rvtt_insn_data::sfploadi);

  unsigned first_mod = TREE_INT_CST_LOW (gimple_call_arg (call, insnd->mod_arg ()));
  tree val = gimple_call_arg (call, insnd->imm_arg ());
  uint32_t ival = TREE_INT_CST_LOW (val);

  tree res = gimple_call_lhs (call);
  if (!res)
    return false;

  bool other_use = false;
  bool sfp_use = false;
  use_operand_p use_p;
  imm_use_iterator iter;

  uppers.clear ();
  FOR_EACH_IMM_USE_FAST (use_p, iter, res)
    {
      gimple *use_stmt = USE_STMT (use_p);
      if (is_gimple_debug (use_stmt))
	continue;
      const rvtt_insn_data *use_insnd;
      gcall *use_call;
      if (!rvtt_p (&use_insnd, &use_call, use_stmt))
	other_use = true;
      else if (first_mod == SFPLOADI_MOD0_USHORT
	       && use_insnd->id == rvtt_insn_data::sfploadi_lv
	       && integer_zerop (gimple_call_arg (use_call, 0))
	       && (TREE_INT_CST_LOW (gimple_call_arg (use_call, use_insnd->mod_arg ()))
		   == SFPLOADI_MOD0_UPPER))
	uppers.push_back (use_call);
      else
	{
	  sfp_use = true;
#if 0 // FIXME: enable when we do register subst
	  if (use_insnd->id == rvtt_insn_data::sfpwriteconfig_v)
	    {
	      int reg = TREE_INT_CST_LOW (gimple_call_arg (use_call, 1));
	      if (reg == CREG_IDX_0 || reg == CREG_IDX_1 || reg == CREG_IDX_NEG_1)
		{
		  warning_at (gimple_location (use_call), 0,
			      "Initializing constant lreg %d from regular sfploadi, use sfploadi (val, X)",
			      reg);
		  gcc_unreachable ();
		}
	    }
#endif
	}
    }

  bool changed = false;
  for (auto *upper : uppers)
    {
      tree upper_val = gimple_call_arg (upper, insnd[1].imm_arg ());
      uint32_t upper_ival = TREE_INT_CST_LOW (upper_val);
      int creg = -1;
      int new_mod = -1;

      if (!ival)
	{
#if 0 // FIXME: Disable register subst for the moment
	  if (upper_ival == 0x0000)
	    creg = CREG_IDX_0;
	  else if (upper_ival == 0x3f80)
	    creg = CREG_IDX_1;
	  else if (upper_ival == 0xbf80)
	    creg = CREG_IDX_NEG_1;
	  else
#endif
	    new_mod = SFPLOADI_MOD0_FLOATB;
	}
      else if (upper_ival == 0)
	{
	  new_mod = SFPLOADI_MOD0_USHORT;
	  upper_val = val;
	}
      else if (upper_ival == 0xffff && (ival >> 15) != 0)
	{
	  new_mod = SFPLOADI_MOD0_SHORT;
	  upper_val = val;
	}
      else
	{
	  // Better as a float16a?
	  // FLOATA=SGN:1,EXP:5,MAN:10, bias 15
	  // FP32=SGN:1,EXP:8,MAN:23, bias 127
	  uint32_t full_value = ival | upper_ival << 16;
	  unsigned exp = (full_value >> 23) & 0xff;
	  if ((full_value & 0x00001fff) == 0
	      && exp >= (127 - 15) && exp <= (127 - 15) + 31)
	    {
	      uint32_t man = full_value & 0x7fffff;
	      uint32_t floata
		= (man >> 13)
		| ((exp - (127 - 15)) << 10)
		| ((full_value >> 31) << 15);
	      new_mod = SFPLOADI_MOD0_FLOATA;
	      upper_val = build_int_cst (unsigned_type_node, floata);
	    }
	}

      if (creg >= 0 || new_mod >= 0)
	{
	  replace_loadi (upper, call, creg, new_mod, upper_val);
	  changed = true;
	}
      else
	sfp_use = true;
    }

  if (sfp_use)
    {
      // this has other uses, can we simplify it?
      int creg = -1;

#if 0 // FIXME: disable register subst for now
      if (!ival)
	creg = CREG_IDX_0;
      else if ((ival & 0x7fff)
	       == (first_mod == SFPLOADI_MOD0_FLOATB ? 0x3f80 : SFPLOADI_MOD0_FLOATA ? 0x3c00 : -1))
	creg = ival & 0x8000 ? CREG_IDX_NEG_1 : CREG_IDX_1;
#endif
      if (creg >= 0)
	{
	  replace_loadi (call, nullptr, creg, -1, nullptr);
	  changed = true;
	}
    }
  else if (!other_use)
    {
      // FIXME: Not needed when we have an SFP DCE pass
      if (dump_file)
	{
	  fprintf (dump_file, "\nDeleting unneeded\n");
	  print_gimple_stmt (dump_file, call, 0);
	}

      auto gsi = gsi_for_stmt (call);
      gsi_remove (&gsi, true);
      changed = true;
    }
  return changed;
}

namespace {

const pass_data pass_data_rvtt_immvar_split =
{
  GIMPLE_PASS, /* type */
  "rvtt_immvar_split", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_immvar_split : public gimple_opt_pass
{
public:
  pass_rvtt_immvar_split (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_immvar_split, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    bool changed = false;
    basic_block bb;
    FOR_EACH_BB_FN (bb, fn)
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi);)
	{
	  gcall *call;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p (&insnd, &call, gsi)
	      && immvar_split (gsi, insnd, call))
	    {
	      gsi_remove (&gsi, true);
	      changed = true;
	    }
	  else
	    gsi_next (&gsi);
	}

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_immvar_split (gcc::context *ctxt)
{
  return new pass_rvtt_immvar_split (ctxt);
}

namespace {

const pass_data pass_data_rvtt_immload_shorten =
{
  GIMPLE_PASS, /* type */
  "rvtt_immload_shorten", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_immload_shorten : public gimple_opt_pass
{
public:
  pass_rvtt_immload_shorten (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_immload_shorten, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    // (a) remove var and id from now-constant immvals
    // (b) deal with setman immediate length
    // (c) optimize sfploadi{,_lv} sequences
    bool changed = false;
    basic_block bb;
    std::vector<gcall *> loads;
    std::vector<gcall *> uppers;

    FOR_EACH_BB_FN (bb, fn)
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gcall *call;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p (&insnd, &call, gsi)
	      && immvar_gather  (gsi, insnd, call, loads))
	    changed = true;
	}

    for (auto *call : loads)
      if (immvar_simplify (call, uppers))
	changed = true;

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_immload_shorten (gcc::context *ctxt)
{
  return new pass_rvtt_immload_shorten (ctxt);
}
