/* TT helper routines
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "stringpool.h"
#include "attribs.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "df.h"
#include "ssa.h"
#include "tree-ssa-propagate.h"
#include "tree-ssa.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "diagnostic-core.h"
#include "tm_p.h"
#include "../riscv-protos.h"

DEBUG_FUNCTION void debug_tree (tree node);

static rvtt_insn_data sfpu_insn_data[] = {
#define RVTT_FN(id, av, sfx, fmt, fl, ops) \
  { rvtt_insn_data::id, #id, fl, rvtt_insn_data::ops_t ops },
#include "rvtt-insn.def"
};

static unsigned riscv_builtin_rvtt_first;

void
rvtt_init_builtins ()
{
  if (!TARGET_XTT_TENSIX)
    return;

  gcc_assert (sfpu_insn_data[0].decl);

  static const auto tensixbh = []() { return TARGET_XTT_TENSIX_BH; };
  static const auto tensixqsr = []() { return TARGET_XTT_TENSIX_QSR; };
  static const auto tensixbh_qsr = []() { return TARGET_XTT_TENSIX_BH_QSR; };
  static const struct {
    bool (*avail) ();
    rvtt_insn_data::insn_id index;
    rvtt_insn_data::flags_t flags;
    rvtt_insn_data::ops_t ops;
  } overrides[] = {
#define RVTT_OVR(id, av, sfx, fmt, fl, ops)		\
    { tensix##av, rvtt_insn_data::id, rvtt_insn_data::flags_t (fl), rvtt_insn_data::ops_t ops },
#include "rvtt-insn.def"
  };

  // Process overrides
  for (auto const &ovr : overrides)
    if (ovr.avail ())
      sfpu_insn_data[ovr.index].override (ovr.flags, ovr.ops);

  for (auto &insn : sfpu_insn_data)
    if (insn.decl)
      insn.init ();
}

void
rvtt_insn_data::init ()
{
  // Compute derived fields;
  int argno = 0, ix = 0;
  tree arg_types = TYPE_ARG_TYPES (TREE_TYPE (decl));

  if (POINTER_TYPE_P (TREE_VALUE (arg_types)))
    {
      // The instrn ptr operand
      gcc_assert (!argno
		  && VOID_TYPE_P (TREE_TYPE (TREE_VALUE (arg_types))));
      flags = flags_t (flags | HAS_VAR);
      arg_types = TREE_CHAIN (arg_types);
      argno++;
    }

  if (is_live ())
    {
      // Skip live vector
      gcc_assert (TREE_CODE (TREE_VALUE (arg_types)) == VECTOR_TYPE);
      arg_types = TREE_CHAIN (arg_types);
      argno++;
    }

  // Skip src vectors
  int num_srcs = 0;
  for (; TREE_CODE (TREE_VALUE (arg_types)) == VECTOR_TYPE;
       arg_types = TREE_CHAIN (arg_types))
    {
      argno++;
      num_srcs++;
    }
  if (num_srcs)
    src_pos = argno - num_srcs;

  if (has_var ())
    {
      // imm, var & id operands
      ops.set_argno (ix, argno);
      arg_types = TREE_CHAIN (arg_types);

      argno++;
      ix++;

      gcc_assert (TREE_CODE (TREE_VALUE (arg_types)) == INTEGER_TYPE);
      arg_types = TREE_CHAIN (arg_types);
      argno++;

      gcc_assert (TREE_CODE (TREE_VALUE (arg_types)) == INTEGER_TYPE);
      arg_types = TREE_CHAIN (arg_types);
      argno++;
    }

  // Remaining arguments must be integers
  for (bool first = true; ops[ix]; first = false, ix++)
    {
      auto kind = ops[ix].kind ();
      if (kind == op_t::MOD || kind == op_t::XMOD)
	{
	  gcc_assert (first);
	  gcc_assert (!(kind == op_t::MOD && !ops[ix].mod ()));
	  flags = flags_t (flags | HAS_MOD);
	  mod_pos = argno;
	}
      gcc_assert (TREE_CODE (TREE_VALUE (arg_types)) == INTEGER_TYPE);
      ops.set_argno (ix, argno);

      arg_types = TREE_CHAIN (arg_types);
      argno++;
    }
  gcc_assert (VOID_TYPE_P (TREE_VALUE (arg_types)));
  arg_num = argno;
}

bool
rvtt_record_builtin (unsigned ix, char const *name, tree decl)
{
  if (!TARGET_XTT_TENSIX)
    return false;

  if (ix < 300)
    // Save a bunch of strcmps on the grounds there are at least this many others.
    return false;

  if (!riscv_builtin_rvtt_first)
    {
      if (strncmp (name, "__builtin_rvtt_", 15) != 0)
	return false;

      riscv_builtin_rvtt_first = ix;

      // Make synth_opcode a const fn, it's the only one.
      TREE_READONLY (decl) = true;
    }

  ix -= riscv_builtin_rvtt_first;

  if (ix >= rvtt_insn_data::hwm)
    return false;

  sfpu_insn_data[ix].decl = decl;

  return !ix;
}

const rvtt_insn_data *
rvtt_get_insn_data (rvtt_insn_data::insn_id id)
{
  return &sfpu_insn_data[id];
}

const rvtt_insn_data *
rvtt_get_insn_data (gcall const *call)
{
  tree decl = gimple_call_fndecl (call);
  if (!decl)
    return nullptr;

  if (!fndecl_built_in_p (decl, BUILT_IN_MD))
    return nullptr;

  auto code = DECL_MD_FUNCTION_CODE (decl);
  if ((code & RISCV_BUILTIN_CLASS) != RISCV_BUILTIN_GENERAL)
    return nullptr;

  unsigned ix = (code >> RISCV_BUILTIN_SHIFT) - riscv_builtin_rvtt_first;
  if (ix >= rvtt_insn_data::hwm)
    return nullptr;

  return &sfpu_insn_data[ix];
}

const rvtt_insn_data *
rvtt_get_insn_data (gimple const *stmt)
{
  if (!is_a <gcall const *> (stmt))
    return nullptr;
  return rvtt_get_insn_data (as_a <gcall const *> (stmt));
}

bool
rvtt_insn_data::sets_cc (gcall *stmt) const
{
  if (auto mask = cc_mask)
    {
      if (!has_mod ())
	return true;

      unsigned mod = TREE_INT_CST_LOW (gimple_call_arg (stmt, mod_arg ()));
      if ((1 << (mod & 0xf)) & mask)
	return true;
    }
  return false;
}

void rvtt_mov_error (const rtx_insn *insn, bool is_load)
{
  if (INSN_HAS_LOCATION (insn))
    input_location = INSN_LOCATION (insn);
  debug_rtx (insn);
  internal_error ("cannot %s sfpu register (register %s)",
		  is_load ? "load" : "store",
		  is_load ? "fill" : "spill");
}

// If a stmt's single use args aren't tracked back to their
// defs and deleted prior to deleting the stmt, errors occur w/
// flag_checking=1
// There has to be an internal version of this...
void rvtt_prep_stmt_for_deletion(gimple *stmt)
{
  for (unsigned int i = 0; i < gimple_call_num_args (stmt); i++)
    {
      tree arg = gimple_call_arg(stmt, i);

      if (TREE_CODE(arg) == SSA_NAME && num_imm_uses (arg) == 1)
	{
	  gimple *def_g = SSA_NAME_DEF_STMT (arg);

	  if (def_g->code == GIMPLE_PHI)
	    {
	      // XXXX handle phi
	      // this seems to work fine and SSA checks are ok w/ doing nothing
	    }
	  else if (def_g->code == GIMPLE_CALL)
	    {
	      tree lhs_name = gimple_call_lhs (def_g);
	      gimple_call_set_lhs(def_g, NULL_TREE);
	      release_ssa_name(lhs_name);
	      update_stmt (def_g);
	    }
	  else if (def_g->code == GIMPLE_ASSIGN)
	    {
	      unlink_stmt_vdef(def_g);
	      gimple_stmt_iterator gsi = gsi_for_stmt(def_g);
	      gsi_remove(&gsi, true);
	      release_defs(def_g);
	    }
	}
    }
}

// Generate the assembly for an sfpsynt_insn{,_dst} insn.

const char *
rvtt_synth::pattern (unsigned is_synthed, const char *tmpl,
		     rtx operands[], bool is_set, int tmp_ix)
{
  if (!is_synthed || tmpl[0] == '#')
    return tmpl;

  operands += is_set; // Whee!

  auto enc = rvtt_synth (INTVAL (operands[rvtt_synth::IX_encode]));
  uint32_t reg_mask = 0;
  uint32_t reg_ops = 0;

  bool has_src = true;
  {
    auto src_op = operands[rvtt_synth::IX_src];
    unsigned src_regno;
    if (REG_P (src_op))
      src_regno = REGNO (src_op) - SFPU_REG_FIRST;
    else
      {
	gcc_assert (GET_CODE (src_op) == UNSPEC);
	if (XINT (src_op, 1) == UNSPEC_SFPCSTLREG)
	  src_regno = INTVAL (XVECEXP (src_op, 0, 0));
	else
	  has_src = false;
      }

    if (has_src)
      {
	unsigned src_shift = enc.src_shift ();
	reg_mask |= 0xf << src_shift;
	reg_ops |= src_regno << src_shift;
      }
  }

  if (is_set)
    {
      rtx dst_reg = operands[-1];
      gcc_assert (REG_P (dst_reg));
      unsigned dst_shift = enc.dst_shift ();
      reg_mask |= 0xf << dst_shift;
      reg_ops |= (REGNO (dst_reg) - SFPU_REG_FIRST) << dst_shift;
    }
  gcc_assert (!reg_mask == (tmp_ix < 0));

  uint32_t opcode = INTVAL (operands[rvtt_synth::IX_opcode]);
  static char pattern[100];
  unsigned pos = 0;
  if (uint32_t reg_change = (opcode & reg_mask) ^ reg_ops)
    {
      // The register assignments here are different from those of the
      // first synth encountered.  We must adjust the incomming
      // pattern.
      // Swap, so the templ prints the temp reg
      std::swap (operands[rvtt_synth::IX_insn], operands[tmp_ix - is_set]);
      opcode ^= reg_change;
      operands[rvtt_synth::IX_opcode] = gen_rtx_CONST_INT (SImode, reg_change);
      pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		       "li\t%%%d,%%%d\n\txor\t%%%d,%%%d,%%%d\n\t",
		       is_set + rvtt_synth::IX_insn,
		       is_set + rvtt_synth::IX_opcode,
		       is_set + rvtt_synth::IX_insn,
		       is_set + rvtt_synth::IX_insn, tmp_ix);
    }

  pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		   "sw\t%%%u, %%%d\t# %d:%s",
		   is_set + rvtt_synth::IX_insn, is_set + rvtt_synth::IX_mem,
		   enc.id (), tmpl);

  gcc_assert (pos < sizeof (pattern));

  return pattern;
}

rvtt_arg_info::rvtt_arg_info (tree arg)
  : arg (arg)
{
  if (!SSA_VAR_P (arg))
    {
      imm = arg;
      cst = TREE_INT_CST_LOW (imm);
      return;
    }

  auto *d = SSA_NAME_DEF_STMT (arg);
  auto *insnd = rvtt_get_insn_data (d);
  if (!insnd)
    return;

  auto *call = as_a <gcall *> (d);
  switch (insnd->id)
    {
    default:
      return;

    case rvtt_insn_data::sfpreadlreg:
      // We only care about detecting zero here
      if (TREE_INT_CST_LOW (gimple_call_arg (call, 0))
	  != CREG_IDX_0)
	return;
      break;

    case rvtt_insn_data::sfploadi:
      if (!integer_zerop (gimple_call_arg (call, 0)))
	// Runtime computed value
	return;

      cst = TREE_INT_CST_LOW (gimple_call_arg (call, insnd->imm_arg ()));
      switch (TREE_INT_CST_LOW (gimple_call_arg (call, insnd->mod_arg ())))
	{
	default:
	  return;

	case SFPLOADI_MOD0_SHORT:
	  cst = int32_t (cst << 16) >> 16;
	  break;

	case SFPLOADI_MOD0_USHORT:
	  break;
	}
      break;
    }
  def = call;
}

rtx
rvtt_gen_rtx_creg (machine_mode mode, unsigned sfpu_regno)
{
  return gen_rtx_UNSPEC (mode,
			 gen_rtvec (1, GEN_INT (sfpu_regno)), UNSPEC_SFPCSTLREG);
}

rtx
rvtt_gen_rtx_noval (machine_mode mode)
{
  return gen_rtx_UNSPEC (mode,
			 gen_rtvec (1, const0_rtx), UNSPEC_SFPNOVAL);
}

bool
rvtt_merge_lv_src (rtx *lv, rtx *src, rtx *commute)
{
  if (noval_operand (*lv, GET_MODE (*lv)))
    return false;

  bool commuted = commute && *lv != *src && *lv == *commute;
  if (commuted)
    std::swap (*src, *commute);

  rtx tmp = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfpassign_lv (tmp, *lv, *src));
  *src = tmp;
  *lv = gen_rtx_UNSPEC (XTT32SImode,
			gen_rtvec (1, const0_rtx), UNSPEC_SFPOMIT);
  return commuted;
}

void
rvtt_substitute_value (tree orig, tree replacement)
{
  if (!orig)
    return;

  gimple *stmt;
  imm_use_iterator orig_iter;
  FOR_EACH_IMM_USE_STMT (stmt, orig_iter, orig)
    {
      use_operand_p orig_use;
      FOR_EACH_IMM_USE_ON_STMT (orig_use, orig_iter)
	propagate_value (orig_use, replacement);
      update_stmt (stmt);
      if (dump_file)
	{
	  fprintf (dump_file, "Updated ");
	  print_gimple_stmt (dump_file, stmt, 2);
	}
    }
}

static bool rvtt_has_attrib_p(const char *attrib, rtx pat)
{
  if (GET_CODE(pat) == ZERO_EXTEND ||
      GET_CODE(pat) == SIGN_EXTEND)
    {
      pat = XEXP(pat, 0);
    }

  if (GET_CODE(pat) == MEM &&
      MEM_EXPR(pat) != NULL_TREE)
    {
      tree exp = MEM_EXPR(pat);
      if (TREE_CODE(exp) == PARM_DECL ||
	  TREE_CODE(exp) == VAR_DECL)
	{
	  // Top level PARM/VAR DECL's are address calculation
	  // (fingers crossed...)
	  return false;
	}

      while (TREE_CODE(exp) != MEM_REF &&
	     TREE_CODE(exp) != TARGET_MEM_REF &&
	     TREE_CODE(exp) != PARM_DECL &&
	     TREE_CODE(exp) != VAR_DECL)
	{
	  if (TREE_CODE(exp) == ARRAY_REF ||
	      TREE_CODE(exp) == COMPONENT_REF ||
	      TREE_CODE(exp) == BIT_FIELD_REF ||
	      TREE_CODE(exp) == VIEW_CONVERT_EXPR ||
	      TREE_CODE(exp) == REALPART_EXPR ||
	      TREE_CODE(exp) == IMAGPART_EXPR)
	    {
	      exp = TREE_OPERAND(exp, 0);
	    }
	  else if (TREE_CODE(exp) == STRING_CST ||
		   TREE_CODE(exp) == VECTOR_CST ||
		   TREE_CODE(exp) == RESULT_DECL)
	    {
	      // CST won't be in L1
	      return false;
	    }
	  else
	    {
	      debug_rtx(pat);
	      debug_tree(MEM_EXPR(pat));
	      gcc_unreachable();
	    }
	}
      gcc_assert(TREE_CODE(exp) == MEM_REF ||
		 TREE_CODE(exp) == TARGET_MEM_REF ||
		 TREE_CODE(exp) == PARM_DECL ||
		 TREE_CODE(exp) == VAR_DECL);

      tree decl = (TREE_CODE(exp) == PARM_DECL ||
		   TREE_CODE(exp) == VAR_DECL) ? exp : TREE_OPERAND(exp, 0);
      if (decl != NULL_TREE &&
	  lookup_attribute(attrib, TYPE_ATTRIBUTES(TREE_TYPE(decl))))
	return true;
    }

  return false;
}

bool rvtt_store_has_restrict_p(const rtx pat)
{
  if (GET_CODE(pat) == SET)
    {
      rtx dst = SET_DEST(pat);

      if (GET_CODE(dst) == MEM &&
	  MEM_EXPR(dst) != NULL_TREE)
	{
	  tree exp = MEM_EXPR(dst);
	  while (TREE_CODE(exp) != MEM_REF &&
		 TREE_CODE(exp) != TARGET_MEM_REF &&
		 TREE_CODE(exp) != PARM_DECL &&
		 TREE_CODE(exp) != VAR_DECL)
	    {
	      if (TREE_CODE(exp) == ARRAY_REF ||
		  TREE_CODE(exp) == COMPONENT_REF ||
		  TREE_CODE(exp) == BIT_FIELD_REF ||
		  TREE_CODE(exp) == VIEW_CONVERT_EXPR ||
		  TREE_CODE(exp) == REALPART_EXPR ||
		  TREE_CODE(exp) == IMAGPART_EXPR)
		{
		  exp = TREE_OPERAND(exp, 0);
		}
	      else if (TREE_CODE(exp) == STRING_CST ||
		       TREE_CODE(exp) == VECTOR_CST ||
		       TREE_CODE(exp) == RESULT_DECL)
		{
		  return false;
		}
	      else
		{
		  debug_rtx(pat);
		  debug_tree(MEM_EXPR(dst));
		  gcc_unreachable();
		}
	    }
	  gcc_assert(TREE_CODE(exp) == MEM_REF ||
		     TREE_CODE(exp) == TARGET_MEM_REF ||
		     TREE_CODE(exp) == PARM_DECL ||
		     TREE_CODE(exp) == VAR_DECL);

	  tree decl = (TREE_CODE(exp) == PARM_DECL ||
		       TREE_CODE(exp) == VAR_DECL) ? exp : TREE_OPERAND(exp, 0);
	  if (decl != NULL_TREE &&
	      TYPE_RESTRICT(TREE_TYPE(decl)))
	    {
	      return true;
	    }
	}
    }

  return false;
}

bool rvtt_l1_load_p(const rtx pat)
{
  if (GET_CODE(pat) == SET)
    {
      return rvtt_has_attrib_p("rvtt_l1_ptr", SET_SRC(pat));
    }

  return false;
}

bool rvtt_reg_load_p(const rtx pat)
{
  if (GET_CODE(pat) == SET)
    {
      return rvtt_has_attrib_p("rvtt_reg_ptr", SET_SRC(pat));
    }

  return false;
}

bool rvtt_hll_p(const rtx pat)
{
  return rvtt_l1_load_p(pat) || rvtt_reg_load_p(pat);
}

bool rvtt_l1_store_p(const rtx pat)
{
  if (GET_CODE(pat) == SET)
    {
      return rvtt_has_attrib_p("rvtt_l1_ptr", SET_DEST(pat));
    }

  return false;
}

bool rvtt_reg_store_p(const rtx pat)
{
  if (GET_CODE(pat) == SET)
    {
      return rvtt_has_attrib_p("rvtt_reg_ptr", SET_DEST(pat));
    }

  return false;
}
