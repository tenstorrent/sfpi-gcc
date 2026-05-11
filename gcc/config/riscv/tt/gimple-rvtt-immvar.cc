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
#include "tree-ssa-propagate.h"
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
    {
      uint32_t ival = TREE_INT_CST_LOW (lower);
      // We want to optimize the simple cases here, so we can detect when one's
      // trying to init the const regs in the immvar simplify pass
      if (!(ival & 0xffff))
	{
	  ival >>= 16;
	  mod = SFPLOADI_MOD0_FLOATB;
	  needs_both = false;
	}
      else if (!(ival >> 16))
	{
	  mod = SFPLOADI_MOD0_USHORT;
	  needs_both = false;
	}
      else if ((ival >> 15) == 0x1ffff)
	{
	  mod = SFPLOADI_MOD0_SHORT;
	  needs_both = false;
	}
      else
	val = build_int_cst (TREE_TYPE (val), ival >> 16);
      lower = build_int_cst (TREE_TYPE (lower), ival & 0xffff);
    }

  tree tmp = emit_sfploadi (gsi, loc, mod, nullptr, addr, lower,
			    needs_both ? nullptr : res);

  if (needs_both)
    {
      tree shift = val;
      if (SSA_VAR_P (shift))
	{
	  shift = make_ssa_name (TREE_TYPE (val));
	  auto *shift_stmt = gimple_build_assign
	    (shift, RSHIFT_EXPR, val, build_int_cst (unsigned_type_node, 16));
	  gimple_set_location (shift_stmt, loc);
	  gsi_insert_before (&gsi, shift_stmt, GSI_SAME_STMT);
	}

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
  gcc_assert (new_insnd->mod_info ().is_xmod ()
	      || (1u << TREE_INT_CST_LOW (mod)) & new_insnd->mod_info ().mod ());
  gimple *stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_set_location (stmt, gimple_location (call));
  gimple_call_set_arg (stmt, new_insnd->src_arg () + !imm_first, imm);
  gimple_call_set_arg (stmt, new_insnd->src_arg () + imm_first,
		       gimple_call_arg (call, insnd->src_arg ()));
  gimple_call_set_arg (stmt, new_insnd->mod_arg (), mod);
  gimple_call_set_lhs (stmt, gimple_call_lhs (call));
  gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);

  // Caller will delete CALL
  return true;
}

static bool
immvar_expand (gimple_stmt_iterator &gsi, const rvtt_insn_data *insnd, gcall *call)
{
  if (!insnd->has_var ())
    return false;
  tree imm = gimple_call_arg (call, insnd->imm_arg ());
  tree addr = insnd->has_var () ? gimple_call_arg (call, 0) : nullptr;
  tree mod = insnd->has_mod () ? gimple_call_arg (call, insnd->mod_arg ()) : nullptr;
  switch (insnd->id)
    {
    default:
      if (!insnd->is_expanded ())
	break;
      {
	bool expand = SSA_VAR_P (imm);	
	auto info = insnd->ops[0];
	bool is_signed = info.kind () == rvtt_insn_data::op_t::SIGNED;
	if (!expand)
	  {
	    HOST_WIDE_INT i_imm = TREE_INT_CST_LOW (imm);
	    HOST_WIDE_INT bound = (1u << info.bits ()) - 1;
	    if (is_signed)
	      {
		bound >>= 1;
		if (i_imm < ~bound)
		  expand = true;
	      }
	    else if (i_imm < 0)
	      expand = true;
	    if (i_imm > bound)
	      expand = true;
	  }
	if (expand)
	  {
	    tree tmp = emit_loadimm (gsi, gimple_location (call), is_signed ? -32 : 32, addr, imm, nullptr);
	    return emit_replacement (gsi, insnd, call,
				     rvtt_insn_data::insn_id (insnd->id - 1), false, tmp, mod);
	  }
      }
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
	}
      break;
#if 0
    case rvtt_insn_data::sfpsetman_i:
      // setman only has a 12-bit immediate field
      if (SSA_VAR_P (imm))
	{
	  tree tmp = emit_loadimm (gsi, gimple_location (call), -24, addr, imm, nullptr);

	  return emit_replacement (gsi, insnd, call,
				   rvtt_insn_data::sfpsetman_v, false, tmp, mod);
	}
      break;
#endif
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

#if 0
    case rvtt_insn_data::sfpsetman_i:
      if (maybe_split_setman (gsi, insnd, call))
	{
	  gsi_remove (&gsi, true);
	  gsi_prev (&gsi);
	  changed = true;
	}
      break;
#endif
    case rvtt_insn_data::sfploadi:
      // We've not done LV optimizing yet, so we don't have to capture sfploadi_lv
      loads.push_back (call);
      break;
    }

  return changed;
}

static void
replace_loadi (gcall *call, gcall *earlier, int op, tree val)
{
  const auto *new_insnd = rvtt_get_insn_data
    (val ? rvtt_insn_data::sfploadi : rvtt_insn_data::sfpreadlreg);

  gimple *new_stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_set_location (new_stmt, gimple_location (call));
  gimple_call_set_lhs (new_stmt, gimple_call_lhs (call));
  if (val)
    {
      gimple_call_set_arg (new_stmt, 0, null_pointer_node);
      gimple_call_set_arg (new_stmt, new_insnd->imm_arg (), val);
      gimple_call_set_arg (new_stmt, new_insnd->var_arg (), integer_zero_node);
      gimple_call_set_arg (new_stmt, new_insnd->id_arg (), integer_zero_node);
    }
  gimple_call_set_arg (new_stmt, val ? new_insnd->mod_arg () : 0,
		       build_int_cst (unsigned_type_node, op));

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

  int sfp_use = 0;
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
	continue;

      if (first_mod == SFPLOADI_MOD0_USHORT
	  && use_insnd->id == rvtt_insn_data::sfploadi_lv
	  && integer_zerop (gimple_call_arg (use_call, 0))
	  && (TREE_INT_CST_LOW (gimple_call_arg (use_call, use_insnd->mod_arg ()))
	      == SFPLOADI_MOD0_UPPER))
	uppers.push_back (use_call);
      else
	{
	  sfp_use |= 1;
	  if (use_insnd->id == rvtt_insn_data::sfpwriteconfig_v)
	    {
	      int reg = TREE_INT_CST_LOW (gimple_call_arg (use_call, 1));
	      if (reg == CREG_IDX_0 || reg == CREG_IDX_1 || reg == CREG_IDX_NEG_1)
		// We must not simplify this to a load of the constant register
		// that we're intializing!
		sfp_use |= 2;
	    }
	}
    }

  bool changed = false;
  for (auto *upper : uppers)
    {
      tree upper_val = gimple_call_arg (upper, insnd[1].imm_arg ());
      uint32_t upper_ival = TREE_INT_CST_LOW (upper_val);
      int op = -1;

      if (!ival)
	{
	  if (upper_ival == 0x0000)
	    {
	      op = CREG_IDX_0;
	      upper_val = nullptr;
	    }
	  else if ((upper_ival & 0x7fff) == 0x3f80)
	    {
	      op = upper_ival & 0x8000 ? CREG_IDX_NEG_1 : CREG_IDX_1;
	      upper_val = nullptr;
	    }
	  else
	    op = SFPLOADI_MOD0_FLOATB;
	}
      else if (upper_ival == 0)
	{
	  op = SFPLOADI_MOD0_USHORT;
	  upper_val = val;
	}
      else if (upper_ival == 0xffff && (ival >> 15) != 0)
	{
	  op = SFPLOADI_MOD0_SHORT;
	  upper_val = val;
	}
      else
	{
	  // Better as a float16a?
	  // FLOATA=SGN:1,EXP:5,MAN:10, bias 15
	  // FP32=SGN:1,EXP:8,MAN:23, bias 127
	  // Stay away from denorms & nans/infs
	  uint32_t full_value = ival | upper_ival << 16;
	  unsigned exp = (full_value >> 23) & 0xff;
	  if ((full_value & 0x00001fff) == 0
	      && exp > (127 - 15) && exp < (127 - 15) + 31)
	    {
	      uint32_t man = full_value & 0x7fffff;
	      uint32_t floata
		= (man >> 13)
		| ((exp - (127 - 15)) << 10)
		| ((full_value >> 31) << 15);
	      op = SFPLOADI_MOD0_FLOATA;
	      upper_val = build_int_cst (unsigned_type_node, floata);
	    }
	}

      if (op >= 0)
	{
	  replace_loadi (upper, call, op, upper_val);
	  changed = true;
	}
      else
	sfp_use |= 1;
    }

  if (sfp_use == 1)
    {
      // this has other uses, can we simplify it?
      int op = -1;

      if (!ival)
	op = CREG_IDX_0;
      else if (int (ival & 0x7fff)
	       == (first_mod == SFPLOADI_MOD0_FLOATB ? 0x3f80
		   : first_mod == SFPLOADI_MOD0_FLOATA ? 0x3c00
		   : ~0))
	op = ival & 0x8000 ? CREG_IDX_NEG_1 : CREG_IDX_1;

      if (op >= 0)
	{
	  replace_loadi (call, nullptr, op, nullptr);
	  changed = true;
	}
    }

  return changed;
}

// CALL has a SCALAR variant, if its second op is from a LOADI and the value
// being loaded fits in the immediate slot, make it so.

static bool
immload_combine (gimple_stmt_iterator gsi, const rvtt_insn_data *call_insnd,
		 gcall *call, const rvtt_insn_data *scalar_insnd)
{
  gcc_assert (!call_insnd->is_live () && !scalar_insnd->is_live ());

  int mod = TREE_INT_CST_LOW (gimple_call_arg (call, call_insnd->mod_arg ()));
  bool maybe_flip_sign = (call_insnd->id == rvtt_insn_data::sfpiadd_v
			  && mod & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST);
  if (maybe_flip_sign)
    mod ^= SFPIADD_MOD1_ARG_2SCOMP_LREG_DST;
  if (!(scalar_insnd->mod_info ().mod () & (1u << mod)))
    // Mod is incompatible
    return false;

  tree imm_op = gimple_call_arg (call, maybe_flip_sign ? 0 : 1);
  gimple *def = SSA_NAME_DEF_STMT (imm_op);

  auto *def_insnd = rvtt_get_insn_data (def);
  if (!def_insnd)
    return false;

  auto *def_call = as_a <gcall *> (def);
  int32_t imm = 0;
  switch (def_insnd->id)
    {
    default:
      return false;

    case rvtt_insn_data::sfploadi:
      {
	tree imm_op = gimple_call_arg (def_call, def_insnd->imm_arg ());
	if (SSA_VAR_P (imm_op))
	  return false;
	imm = TREE_INT_CST_LOW (imm_op);
	tree mod_op = gimple_call_arg (def_call, def_insnd->mod_arg ());
	switch (TREE_INT_CST_LOW (mod_op))
	  {
	  default:
	    return false;

	  case SFPLOADI_MOD0_USHORT:
	    break;

	  case SFPLOADI_MOD0_SHORT:
	    imm = imm << 16 >> 16;
	    break;
	  }

	if (maybe_flip_sign)
	  imm = -imm;

	auto info = scalar_insnd->ops[0];
	int32_t bound = (1u << info.bits ()) - 1;
	if (info.kind () == rvtt_insn_data::op_t::SIGNED)
	  {
	    bound >>= 1;
	    if (imm < ~bound)
	      return false;
	  }

	if (imm > bound)
	  return false;
      }
      break;

    case rvtt_insn_data::sfpreadlreg:
      if (TREE_INT_CST_LOW (gimple_call_arg (def_call, 0)) != CREG_IDX_0)
	return false;

      // imm is zero,
      break;
    }

  if (dump_file)
    {
      fprintf (dump_file, "Combining:\n");
      print_gimple_stmt (dump_file, def_call, 2);
      print_gimple_stmt (dump_file, call, 2);
    }

  if (call_insnd->id == rvtt_insn_data::sfpiadd_v && !imm
      && !call_insnd->sets_cc (mod))
    {
      // We can elide call entirely
      if (dump_file)
	fprintf (dump_file, "to nothing\n");

      tree input = gimple_call_arg (call, maybe_flip_sign ? 1 : 0);
      if (tree output = gimple_call_lhs (call))
	{
	  gimple *stmt;
	  imm_use_iterator ssa_iter;
	  FOR_EACH_IMM_USE_STMT (stmt, ssa_iter, output)
	    {
	      use_operand_p use_p;
	      FOR_EACH_IMM_USE_ON_STMT (use_p, ssa_iter)
		propagate_value (use_p, input);
	      update_stmt (stmt);
	      if (dump_file)
		{
		  fprintf (dump_file, "Updated ");
		  print_gimple_stmt (dump_file, stmt, 2);
		}
	    }
	}
      if (dump_file)
	fprintf (dump_file, "\n");
      return true;
    }
  
  // Replace the CALL with one of SCALAR
  gimple *new_call = gimple_build_call (scalar_insnd->decl, scalar_insnd->num_args ());
  gimple_set_location (new_call, gimple_location (call));
  gimple_call_set_lhs (new_call, gimple_call_lhs (call));
  gimple_call_set_arg (new_call, 0, null_pointer_node);
  gimple_call_set_arg (new_call, 1, gimple_call_arg (call, maybe_flip_sign ? 1 : 0));

  gcc_assert (scalar_insnd->imm_arg () == 2 && scalar_insnd->mod_arg () == 5);
  gimple_call_set_arg (new_call, 2, build_int_cst (integer_type_node, imm));
  gimple_call_set_arg (new_call, 3, integer_zero_node);
  gimple_call_set_arg (new_call, 4, integer_zero_node);

  gimple_call_set_arg (new_call, 5, build_int_cst (unsigned_type_node, mod));

  // Copy remaining args
  for (unsigned ix = scalar_insnd->num_args (); --ix != 5;)
    gimple_call_set_arg (new_call, ix, gimple_call_arg (call, ix - 3));

  gsi_insert_before (&gsi, new_call, GSI_SAME_STMT);

  if (dump_file)
    {
      fprintf (dump_file, "to:\n");
      print_gimple_stmt (dump_file, new_call, 2);
      fprintf (dump_file, "\n");
    }
  return true;
}

namespace {

const pass_data pass_data_rvtt_immvar_expand =
{
  GIMPLE_PASS, /* type */
  "rvtt_immvar_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_immvar_expand : public gimple_opt_pass
{
public:
  pass_rvtt_immvar_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_immvar_expand, ctxt)
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
	      && immvar_expand (gsi, insnd, call))
	    {
	      gsi_remove (&gsi, true);
	      changed = true;
	      continue;
	    }

	  gsi_next (&gsi);
	}

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_immvar_expand (gcc::context *ctxt)
{
  return new pass_rvtt_immvar_expand (ctxt);
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

namespace {

const pass_data pass_data_rvtt_immload_combine =
{
  GIMPLE_PASS, /* type */
  "rvtt_immload_combine", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_immload_combine : public gimple_opt_pass
{
public:
  pass_rvtt_immload_combine (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_immload_combine, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }
  virtual unsigned execute (function *fn) override
  {
    // See if insns with a scalar variant can use that instead of an input loadi
    bool changed = false;
    basic_block bb;

    FOR_EACH_BB_FN (bb, fn)
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi);)
	{
	  gcall *call;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p (&insnd, &call, gsi))
	    if (auto *scalar = insnd->get_scalar ())
	      if (immload_combine (gsi, insnd, call, scalar))
		{
		  gsi_remove (&gsi, true);
		  changed = true;
		  continue;
		}

	  gsi_next (&gsi);
	}

    return changed ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_immload_combine (gcc::context *ctxt)
{
  return new pass_rvtt_immload_combine (ctxt);
}
