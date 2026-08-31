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
#include "rvtt-refuse.h"
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
  while (ops[ix])
    {
      auto kind = ops[ix].kind ();
      if (kind == op_t::MOD || kind == op_t::XMOD)
	{
	  gcc_assert (!has_mod ());
	  if (kind == op_t::MOD)
	    gcc_assert (ops[ix].mod () != 0);
	  flags = flags_t (flags | HAS_MOD);
	  mod_pos = argno;
	}
      gcc_assert (TREE_CODE (TREE_VALUE (arg_types)) == INTEGER_TYPE);
      ops.set_argno (ix, argno);

      arg_types = TREE_CHAIN (arg_types);
      argno++;
      ix++;
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

/* The reassociation license key (owner ratification 2026-08-21).
   Value-changing FP reassociation fires ONLY when the user passed BOTH
   -fassociative-math (GCC's explicit opt-in: reassociation "may change
   the computation result", invoke.texi) and our default-off
   -mtt-tensix-optimize-reassoc.  Either flag absent = every FP
   reassociation site fails closed with a named refusal and codegen is
   byte-identical.  */

bool
rvtt_reassoc_fp_licensed_p (void)
{
  return riscv_tt_opt_reassoc > 0 && flag_associative_math;
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

/* Set by rtl-rvtt-spill-diag.cc when it has reported (and deleted)
   allocated SFPU memory moves in this compilation.  */
bool rvtt_spill_diag_reported;

void rvtt_mov_error (const rtx_insn *insn, bool is_load)
{
  /* The named user diagnosis of allocated SFPU memory moves lives in
     rtl-rvtt-spill-diag.cc (lreg-pressure-exceeded), which runs
     directly after allocation.  Only when THAT diagnosis has fired may
     the backstop stand down (a stray placeholder in the discarded
     assembly of a failed compilation is harmless).  An SFPU memory
     move on any other stream -- including one where unrelated user
     errors were reported -- remains what it always was: a compiler
     bug.  */
  if (rvtt_spill_diag_reported && seen_error ())
    return;
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
  // Any SSA definition removed by an RVTT lowering may still be named by
  // GIMPLE_DEBUG_BIND statements when compiling with -g.  Debug uses are not
  // semantic uses and must not keep an otherwise-deleted definition alive.
  reset_debug_uses (stmt);

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

void
rvtt_merge_lv_src (rtx *lv, rtx *src)
{
  if (noval_operand (*lv, GET_MODE (*lv)))
    return;

  rtx tmp = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfpassign_lv (tmp, *lv, *src));
  *src = tmp;
  *lv = gen_rtx_UNSPEC (XTT32SImode,
			gen_rtvec (1, const0_rtx), UNSPEC_SFPOMIT);
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

static int rvtt_cmp_ex_to_setcc_mod1_map[] = {
  SFPSETCC_MOD1_LREG_LT0,
  SFPSETCC_MOD1_LREG_GTE0,
  SFPSETCC_MOD1_LREG_EQ0,
  SFPSETCC_MOD1_LREG_NE0,
  -1,
  -1,
  -1,
  -1,
};

// FIXME: Remnants of old sfpxloadi scheme,
// FIXME: Move functionality in to immvar passes

void
rvtt_emit_sfpxloadi (rtx dst, rtx lv, rtx imm)
{
  // Early nonimm pass assures this
  gcc_assert (CONST_INT_P (imm));

  // FIXME: we're just moving bits around here, the type of the input value
  // doesnt matter.
  uint32_t int_imm = INTVAL (imm);
  int new_mod = -1;

  if (int_imm <= 0x7fff || int_imm >= 0xffff8000)
    new_mod = SFPLOADI_MOD0_SHORT;
  else if (int_imm <= 0xffff)
    new_mod = SFPLOADI_MOD0_USHORT;
  else if (!(int_imm & 0xffff))
    {
      imm = GEN_INT (int_imm >> 16);
      new_mod = SFPLOADI_MOD0_FLOATB;
    }
  else if (!(int_imm & 0x1FFF))
    {
      int exp = (int_imm >> 23) & 0xFF;

      if (exp < 127 + 16 && exp >= 127 - 14)
	  {
	    // Fits in fp16a
	    imm = GEN_INT (((int_imm >> 13) & 0x3ff)
			   | ((int_imm >> 16) & 0x8000)
			   | ((exp - 0x70) << 10));
	    new_mod = SFPLOADI_MOD0_FLOATA;
	  }
    }

  if (new_mod >= 0)
    emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx, const0_rtx,
					 imm,
					 rvtt_gen_rtx_noval (XTT32SImode),
					 lv, GEN_INT (new_mod)));
  else
    {
      // A full literal is assembled in place.  The UPPER form reads the
      // preceding low half from LV, and the MD pattern ties LV to its result;
      // using DST for both avoids materializing a distinct temporary (and the
      // SFPMOV reload needed solely to satisfy that tie).
      emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx, const0_rtx,
					   GEN_INT (int_imm & 0xFFFF),
					   rvtt_gen_rtx_noval (XTT32SImode),
					   lv, GEN_INT (SFPLOADI_MOD0_USHORT)));
      emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx, const0_rtx,
					   GEN_INT (int_imm >> 16),
					   rvtt_gen_rtx_noval (XTT32SImode),
					   dst, GEN_INT (SFPLOADI_MOD0_UPPER)));
    }
}

// -mtt-tensix-optimize-native-compare: admission for replacing the
// strict-greater / less-or-equal float compare-against-zero SETCC webs
// with the single BH-native SFPGT/SFPLE SET_CC compare against the
// constant +0.0 register (CREG_IDX_0 == L9).
//
// Pointwise equivalence (tt/proofs/native-compare-gtle/, exhaustive over
// all 2^32 compared bit patterns):
//   GT web  {SETCC mod4 (sign clear); SETCC mod2 (bits != 0)}
//     == sign-magnitude total order (v > +0.0)   [SFPGT SET_CC]
//   LE web  {SETCC mod4; SETCC mod2; COMPC}
//     == sign-magnitude total order (v <= +0.0)  [SFPLE SET_CC]
// including the -0.0, +0.0, Inf and NaN classes (total order:
// -NaN < -Inf < ... < -0 < +0 < ... < +Inf < +NaN, so "> +0" is exactly
// sign-clear-and-nonzero and "<= +0" its lane complement).  The COMPC's
// fence dependency disappears: SET_CC writes only enabled lanes, which
// is the same lane set the fenced complement reconstructs.
//
// Fail-closed: BH only (the insns do not exist on WH; QSR keeps the
// established lowering pending its own audit), flag default-off, and
// the compared value must be a plain register (the SFPGT/SFPLE VD field
// is architecturally a read in SET_CC form but gas limits the operand
// to L0-L7 and the sim verifies lreg_dest < 8; constant-register
// spellings keep the SETCC web).

static bool
rvtt_native_compare_gtle_p (rtx v)
{
  /* Named telemetry for the previously silent unit (plan item #1;
     AUDIT-interprocedural.md native-compare).  */
  if (!TARGET_XTT_TENSIX_BH || !riscv_tt_opt_native_compare)
    {
      rvtt_refuse (RVTT_REF_NATIVE_COMPARE_TARGET_UNGATED, dump_file,
		   "native-compare refused"
		   " (native-compare-target-ungated)\n");
      return false;
    }
  if (!REG_P (v))
    {
      rvtt_refuse (RVTT_REF_NATIVE_COMPARE_OPERAND_SHAPE, dump_file,
		   "native-compare refused"
		   " (native-compare-operand-shape)\n");
      return false;
    }
  return true;
}

static void
rvtt_emit_native_compare_gtle (rtx v, unsigned int cmp)
{
  rtx zero = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_0);
  if (cmp == SFPXCMP_MOD1_CC_GT)
    emit_insn (gen_rvtt_sfpgt_cc (v, zero));
  else
    {
      gcc_assert (cmp == SFPXCMP_MOD1_CC_LE);
      emit_insn (gen_rvtt_sfple_cc (v, zero));
    }
}

void
rvtt_emit_sfpxfcmps (rtx v, rtx f, rtx mod)
{
  bool need_sub = false;
  rtx ref_val = gen_reg_rtx (XTT32SImode);

  // gimple synth expand guarantees this
  gcc_assert (CONST_INT_P (f));
  unsigned int fval = INTVAL (f);

  // Wrapper will convert 0 to -0
  if (fval != 0 && fval != 0x8000)
    {
      need_sub = true;
      // FIXME: Just teach sfpxloadi about this. (add in one of the immvar opt pass)
      if (fval == 0x3f800000)
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_1);
      else if (fval == 0xbf800000)
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);
      else
	rvtt_emit_sfpxloadi (ref_val, rvtt_gen_rtx_noval (XTT32SImode), f);
    }

  // FIXME: a lot of the below is sfpxfcmpv
  unsigned int cmp = INTVAL (mod) & SFPXCMP_MOD1_CC_MASK;
  rtx setcc_mod = GEN_INT (rvtt_cmp_ex_to_setcc_mod1_map[cmp]);
  if (need_sub)
    {
      rtx tmp = gen_reg_rtx (XTT32SImode);
      rtx neg_one = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);

      emit_insn (gen_rvtt_sfpmad (tmp, ref_val, neg_one, v, const0_rtx));
      v = tmp;
    }

  if (cmp == SFPXCMP_MOD1_CC_LE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      if (rvtt_native_compare_gtle_p (v))
	rvtt_emit_native_compare_gtle (v, cmp);
      else
	{
	  emit_insn (gen_rvtt_sfpsetcc_v (v, GEN_INT (SFPSETCC_MOD1_LREG_GTE0)));
	  emit_insn (gen_rvtt_sfpsetcc_v (v, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
	  if (cmp == SFPXCMP_MOD1_CC_LE)
	    emit_insn (gen_rvtt_sfpcompc ());
	}
    }
  else
    emit_insn (gen_rvtt_sfpsetcc_v (v, setcc_mod));
}

// Compare two vectors by subtracting v2 from v1 and doing a setcc
void
rvtt_emit_sfpxfcmpv (rtx v1, rtx v2, rtx mod)
{
  rtx tmp = gen_reg_rtx (XTT32SImode);
  rtx neg1 = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);

  emit_insn (gen_rvtt_sfpmad (tmp, v2, neg1, v1, const0_rtx));

  unsigned int cmp = INTVAL (mod) & SFPXCMP_MOD1_CC_MASK;
  if (cmp == SFPXCMP_MOD1_CC_LE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      if (rvtt_native_compare_gtle_p (tmp))
	rvtt_emit_native_compare_gtle (tmp, cmp);
      else
	{
	  emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (SFPSETCC_MOD1_LREG_GTE0)));
	  emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
	  if (cmp == SFPXCMP_MOD1_CC_LE)
	    emit_insn (gen_rvtt_sfpcompc ());
	}
    }
  else
    emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
}

// Extended (or external?) iadd_i
// Handles:
//   - signed/unsigned immediate value
//   - >12 bits (>11 bits for unsigned)
//   - comparators: <, ==, !=, >=, <=, >
//   - use of SETCC vs IADD for perf
//
// For comparisons:
//   compare  < 0 or >= 0  use setcc
//   compare == 0 or != 0  use setcc
//
//   <=, > use multiple instructions, <= uses a COMPC which relies on the
//   wrapper emitting a PUSHC as a "fence" for the COMPC when needed
//
// Below, n is either not 0 or unknown
//   compare  < n or >= n  use iadd_i (subtract and compare)
//   compare == n or != n  use iadd_i and setcc (subtract then compare)
//
// Note: wrapper/instruction combining cannot create the case where the op
// is either <= n or > n and we care about the result.  The code below doesn't
// handle it and if it did, the result would be inefficient.
//
void
rvtt_emit_sfpxiadd_i (rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used)
{
  unsigned int modi = INTVAL (mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      rtx tmp = gen_reg_rtx (XTT32SImode);
      rvtt_emit_sfpxiadd_i (tmp, lv, addr, src, imm, GEN_INT (base_mod | SFPXCMP_MOD1_CC_GE), true);
      rvtt_emit_sfpxiadd_i (dst, lv, addr, tmp, const0_rtx, GEN_INT (base_mod | SFPXCMP_MOD1_CC_NE));
      if (cmp == SFPXCMP_MOD1_CC_LE)
	emit_insn (gen_rvtt_sfpcompc ());
      return;
    }

  bool need_loadi = true;
  bool is_const_int = CONST_INT_P (imm);
  bool is_sub = bool (modi & SFPXIADD_MOD1_IS_SUB);
  int iv = is_const_int ? INTVAL (imm) : 0xffffffff;
  // gcc_assert (is_sub); sub may not be set due to combine optimization we have

  // Figure out if we need to do a loadi (>12 bits signed)
  if (is_const_int)
    {
      iv = is_sub ? -iv : iv;
      if (iv < 2048 && iv >= -2048)
	{
	  need_loadi = false;
	  imm = GEN_INT (iv);
	}
    }

  rtx set_cc_arg = src;
  bool need_setcc = true;
  if (need_loadi)
    {
      // Load imm into dst
      rvtt_emit_sfpxloadi (dst, rvtt_gen_rtx_noval (XTT32SImode), imm);
      
      unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;
      if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GE)
	{
	  // Perform op w/ compare
	  mod1 |= cmp == SFPXCMP_MOD1_CC_LT ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	  emit_insn (gen_rvtt_sfpiadd_v (dst, dst, src, GEN_INT (mod1)));
	  need_setcc = false;
	}
      else
	{
	  // Perform op w/o compare, compare with SETCC
	  mod1 |= SFPIADD_MOD1_CC_NONE;
	  emit_insn (gen_rvtt_sfpiadd_v (dst, dst, src, GEN_INT (mod1)));
	  set_cc_arg = dst;
	}
    }
  else
    {
      gcc_assert (is_const_int);
      if (iv != 0)
	{
	  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GE)
	    {
	      // Perform op w/ compare
	      unsigned mod1 = cmp == SFPXCMP_MOD1_CC_LT
		? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	      emit_insn (gen_rvtt_sfpiadd_i_lv (dst, const0_rtx, lv, src,
						imm, const0_rtx, const0_rtx,
						GEN_INT (mod1 | SFPIADD_MOD1_ARG_IMM)));
	      need_setcc = false;
	    }
	  else
	    {
	      // Perform op w/o compare
	      emit_insn (gen_rvtt_sfpiadd_i_lv (dst, const0_rtx, lv, src,
						imm, const0_rtx, const0_rtx,
						GEN_INT (SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
	      set_cc_arg = dst;
	    }
	}
      else if (dst_used)
	{
	  rtx insn;
	  if (true || REG_P (lv))
	    insn = gen_rvtt_sfpassign_lv (dst, lv, src);
	  else
	    insn = gen_rvtt_sfpassign (dst, src);
	  emit_insn (insn);
	}
    }

  if (need_setcc)
    emit_insn (gen_rvtt_sfpsetcc_v (set_cc_arg, GEN_INT (rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
}

// See comment block above sfpiadd_i
void
rvtt_emit_sfpxiadd_v (rtx dst, rtx srcb, rtx srca, rtx mod)
{
  unsigned int modi = INTVAL (mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      rvtt_emit_sfpxiadd_v (dst, srcb, srca, GEN_INT (base_mod | SFPXCMP_MOD1_CC_GE));
      emit_insn(gen_rvtt_sfpsetcc_v (dst, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
      if (cmp == SFPXCMP_MOD1_CC_LE)
	emit_insn (gen_rvtt_sfpcompc ());
      return;
    }

  bool is_sub = bool (modi & SFPXIADD_MOD1_IS_SUB);
  gcc_assert (is_sub);
  unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;

  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GE)
    {
      // Perform op w/ compare
      mod1 |= cmp == SFPXCMP_MOD1_CC_LT ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
      emit_insn (gen_rvtt_sfpiadd_v (dst, srcb, srca, GEN_INT (mod1)));
    }
  else
    {
    // Perform op w/o compare
    mod1 |= SFPIADD_MOD1_CC_NONE;
    emit_insn (gen_rvtt_sfpiadd_v (dst, srcb, srca, GEN_INT (mod1)));
    // Must be EQ0 or NE0, compare with SETCC
    gcc_assert (cmp == SFPXCMP_MOD1_CC_EQ || cmp == SFPXCMP_MOD1_CC_NE);
    emit_insn (gen_rvtt_sfpsetcc_v (dst, GEN_INT (rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
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
