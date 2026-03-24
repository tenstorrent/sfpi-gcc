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
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "stringpool.h"
#include "attribs.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "df.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "diagnostic-core.h"
#include "../riscv-protos.h"

DEBUG_FUNCTION void debug_tree (tree node);

unsigned int rvtt_cmp_ex_to_setcc_mod1_map[] = {
  0,
  SFPSETCC_MOD1_LREG_LT0,
  SFPSETCC_MOD1_LREG_EQ0,
  SFPSETCC_MOD1_LREG_GTE0,
  SFPSETCC_MOD1_LREG_NE0,
};

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

  if (num_src_clobbers ())
    clobber_pos = argno + int ((flags >> CLOBBER_SHIFT) & CLOBBER_MASK);

  // Skip src vectors
  for (; TREE_CODE (TREE_VALUE (arg_types)) == VECTOR_TYPE;
       arg_types = TREE_CHAIN (arg_types)) 
    argno++;
  if (num_src_clobbers ())
    gcc_assert (clobber_pos + num_src_clobbers () <= argno);

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
rvtt_get_insn_data (const rvtt_insn_data::insn_id id)
{
  auto *res = &sfpu_insn_data[id];
  gcc_assert (!res->decl || TREE_CODE (res->decl) == FUNCTION_DECL);
  return res;
}

const rvtt_insn_data *
rvtt_get_insn_data (const gcall *stmt)
{
  tree decl = gimple_call_fndecl (stmt);
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

bool
rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple *g)
{
  *stmt = dyn_cast<gcall *> (g);
  if (!*stmt)
    return false;
  *insnd = rvtt_get_insn_data (*stmt);
  return *insnd;
}

bool
rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi)
{
  return rvtt_p (insnd, stmt, gsi_stmt (gsi));
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

bool rvtt_insn_data::srcs_commute (gcall *stmt) const
{
  if (!(flags & COMMUTES))
    return false;

  if (id == sfpxiadd_v)
    {
      auto mod = TREE_INT_CST_LOW (gimple_call_arg (stmt, mod_arg ()));
      return !(mod & SFPXIADD_MOD1_IS_SUB);
    }

  return true;
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

rtx rvtt_clamp_signed(rtx v, unsigned int mask)
{
  int i = INTVAL(v);
  int out = i & mask;

  if (i & (mask + 1)) {
    out |= ~mask;
  }

  return GEN_INT(out);
}

rtx rvtt_clamp_unsigned(rtx v, unsigned int mask)
{
  int i = INTVAL(v);
  int out = i & mask;

  return GEN_INT(out);
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
  if (!is_synthed)
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

uint32_t rvtt_fp32_to_fp16a(const uint32_t val)
{
    // https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion
    // Handles denorms.  May be costly w/ non-immediate values
    const unsigned int b = val + 0x00001000;
    const unsigned int e = (b & 0x7F800000) >> 23;
    const unsigned int m = b & 0x007FFFFF;
    const unsigned int result =
       (b & 0x80000000) >> 16 |
       (e > 112) * ((((e - 112) << 10) &0x7C00) | m >> 13) |
       ((e < 113) & (e > 101)) * ((((0x007FF000 + m) >> (125 -e )) + 1) >> 1) |
       (e > 143) * 0x7FFF;
#if 0
    // Simple/faster but less complete
    const unsigned int result =
       ((val >> 16) & 0x8000) |
       ((((val & 0x7F800000) - 0x38000000) >> 13) & 0x7c00) |
       ((val >> 13) & 0x03FF);
#endif

    return result;
}

uint32_t rvtt_fp32_to_fp16b(const uint32_t val)
{
    return val >> 16;
}

uint32_t rvtt_scmp2loadi_mod(int mod)
{
  int fmt = mod & SFPXSCMP_MOD1_FMT_MASK;

  if (fmt == SFPXSCMP_MOD1_FMT_A) {
    return SFPLOADI_MOD0_FLOATA;
  }
  if (fmt == SFPXSCMP_MOD1_FMT_B) {
    return SFPLOADI_MOD0_FLOATB;
  }

  return SFPXLOADI_MOD0_FLOAT;
}

bool rvtt_get_fp16b(tree *value, gcall *stmt, const rvtt_insn_data *insnd)
{
  int mod0 = TREE_INT_CST_LOW (gimple_call_arg (stmt, insnd->mod_arg ()));
  bool representable = false;
  tree arg = gimple_call_arg(stmt, insnd->imm_arg ());

  switch (mod0) {
  case SFPLOADI_MOD0_FLOATB:
    *value = arg;
    representable = true;
    break;

  case SFPLOADI_MOD0_FLOATA:
    // Corner case.  Someone requested fp16a, but the value fits in fp16b
    // XXXXX ignore for now
    break;

  case SFPXLOADI_MOD0_FLOAT:
    if (TREE_CODE(arg) == INTEGER_CST) {
      unsigned int inval = *(arg->int_cst.val);
      unsigned int man = inval & 0x007FFFFF;

      if ((man & 0xFFFF) == 0) {
       // Fits in fp16b
       representable = true;
       *value = build_int_cst(integer_type_node, rvtt_fp32_to_fp16b(inval));
      }
    }
    break;

  default:
    // Other fmts are int fmts
    break;
  }

  return representable;
}

rtx
rvtt_gen_rtx_creg (machine_mode mode, unsigned sfpu_regno)
{
  return gen_rtx_UNSPEC (mode, gen_rtvec (1, GEN_INT (sfpu_regno)), UNSPEC_SFPCSTLREG);
}

rtx
rvtt_gen_rtx_noval (machine_mode mode)
{
  return gen_rtx_UNSPEC (mode, gen_rtvec (1, const0_rtx), UNSPEC_SFPNOVAL);
}

static void
finish_new_insn(gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gimple *stmt)
{
  gcc_assert(new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
  gimple_set_block (new_stmt, gimple_block (stmt));
  update_stmt (new_stmt);
  if (insert_before)
    {
      gsi_insert_before(gsip, new_stmt, GSI_SAME_STMT);
    }
  else
    {
      gsi_insert_after(gsip, new_stmt, GSI_SAME_STMT);
    }
}

static tree
emit_mask(tree val, unsigned int mask, gimple_stmt_iterator *gsip, gimple *stmt)
{
  tree tmp = make_temp_ssa_name (unsigned_type_node, NULL, "mask");
  gimple *new_stmt = gimple_build_assign(tmp, BIT_AND_EXPR, val,
					 build_int_cst(unsigned_type_node, mask));
  finish_new_insn(gsip, true, new_stmt, stmt);
  return tmp;
}

static tree
emit_shift(tree val, int shft, gimple_stmt_iterator *gsip, gimple *stmt)
{
  if (shft != 0)
    {
      tree_code dir;
      if (shft < 0)
	{
	  shft = -shft;
	  dir = RSHIFT_EXPR;
	}
      else
	{
	  dir = LSHIFT_EXPR;
	}

      tree tmp = make_temp_ssa_name (unsigned_type_node, NULL, "shift");
      gimple *new_stmt = gimple_build_assign(tmp, dir, val,
					     build_int_cst(unsigned_type_node, shft));
      finish_new_insn(gsip, true, new_stmt, stmt);
      return tmp;
    }
  return val;
}

static tree
emit_load_imm(unsigned int id, gimple_stmt_iterator *gsip, gimple *stmt)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::synth_opcode);

  tree tmp = make_temp_ssa_name (unsigned_type_node, NULL, "li");
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2);
  gimple_call_set_arg(new_stmt, 0, build_int_cst(unsigned_type_node, 0));
  gimple_call_set_arg(new_stmt, 1, build_int_cst(unsigned_type_node, id));
  gimple_call_set_lhs (new_stmt, tmp);
  finish_new_insn(gsip, true, new_stmt, stmt);

  return tmp;
}

static tree
emit_add(tree lop, tree rop, gimple_stmt_iterator *gsip, gimple *stmt)
{
  tree tmp = make_temp_ssa_name (unsigned_type_node, NULL, "sum");
  gimple *new_stmt = gimple_build_assign(tmp, PLUS_EXPR, lop, rop);
  finish_new_insn(gsip, true, new_stmt, stmt);
  return tmp;
}

tree
rvtt_emit_nonimm_prologue(unsigned int unique_id,
				const rvtt_insn_data *insnd,
				gcall *stmt,
				gimple_stmt_iterator gsi)
{
  // nonimm_pos contains the raw value
  // nonimm_pos+1 contains the shifted/masked value + load_immediate
  // nonimm_pos+2 (will) contain the unique_id
  tree immarg = gimple_call_arg(stmt, insnd->imm_arg ());

  // Insert insns to generate:
  //   sum = unique_id + ((raw & nonimm_mask) << nonimm_shft)
  int iupper = insnd->imm_is_upper () ? 16 : 0;
  uint32_t imask = ((1u << insnd->imm_bits ()) - 1) << iupper;
  tree mask = emit_mask(immarg, imask, &gsi, stmt);
  int ishift = int (insnd->imm_encode ()) - iupper;
  tree shft = emit_shift(mask, ishift, &gsi, stmt);
  tree li = emit_load_imm(unique_id, &gsi, stmt);
  tree sum = emit_add(shft, li, &gsi, stmt);

  return sum;
}

// Determine if a prologue has been emitted for the current instruction based
// on the unique id.  If so, re-use it, if not emit and track it
void
rvtt_link_nonimm_prologue(std::vector<tree> &load_imm_map,
				unsigned int unique_id,
				tree old_add,
				const rvtt_insn_data *insnd,
				gcall *stmt)
{
  gimple *add_stmt = SSA_NAME_DEF_STMT(old_add);

  if (load_imm_map.size() <= unique_id)
   {
      load_imm_map.resize(unique_id + 1);
    }

  tree sum;
  if (load_imm_map[unique_id] == nullptr)
    {
      sum = rvtt_emit_nonimm_prologue(unique_id, insnd, stmt, gsi_for_stmt(add_stmt));
      load_imm_map[unique_id] = sum;
    }
  else
    {
      sum = load_imm_map[unique_id];
    }

  // Update insn to make insnd->nonimm_pos+1 contain the sum
  gimple_call_set_arg(stmt, insnd->var_arg (), sum);
  // Save unique_id in insn's id field
  gimple_call_set_arg(stmt, insnd->id_arg (),
		      build_int_cst(integer_type_node, unique_id));
  update_stmt (stmt);
}

// FIXME: break out regular sfploadi builtin. Teach sfpxloadi to just look at
// bit patterns and about constant regs.

void
rvtt_emit_sfpxloadi (rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id)
{
  int int_mod = INTVAL (mod);

  if (!(int_mod & SFPXLOADI_MOD0_32BIT_MASK))
    {
      auto mem = const0_rtx;
      auto opc = const0_rtx;
      auto enc = const0_rtx;
      if (!CONST_INT_P (imm))
	{
	  mem = gen_rtx_MEM (SImode, addr);
	  int op
	    = TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPLOADI (0, int_mod, 0)
	    : TARGET_XTT_TENSIX_BH ? TT_OP_BH_SFPLOADI (0, int_mod, 0)
	    : 0;
	  opc = GEN_INT (op);
	  enc = GEN_INT (rvtt_synth (UINTVAL (id)).src_shift (0).dst_shift (20));
	  imm = nonimm;
	}
      else
	imm = rvtt_clamp_unsigned (imm, 0xffff);

      emit_insn (gen_rvtt_sfploadi_int
		 (dst, mem, opc, enc, imm,
		  rvtt_gen_rtx_noval (XTT32SImode),
		  lv, mod));
      return;
    }

  // Early nonimm pass assures this
  gcc_assert (CONST_INT_P (imm));

  // FIXME: we're just moving bits around here, the type of the input value
  // doesnt matter.
  unsigned int_imm = INTVAL (imm);
  bool load_32bit = true;
  unsigned int new_mod;

  switch (int_mod)
    {
    case SFPXLOADI_MOD0_INT32:
      // This gets interesting since we can do a signed load of a 16 bit
      // positive integer by using an unsigned load to fill the upper bits
      // with 0s
      if (int_imm <= 0x7fff && int(int_imm) >= -0x8000)
	{
	  new_mod = SFPLOADI_MOD0_SHORT;
	  load_32bit = false;
	}
      else if (int(int_imm) >= 0 && int_imm <= 0xffff)
	{
	  new_mod = SFPLOADI_MOD0_USHORT;
	  load_32bit = false;
	}
      break;

    case SFPXLOADI_MOD0_UINT32:
      if (int_imm <= 0xffff)
	{
	  new_mod = SFPLOADI_MOD0_USHORT;
	  load_32bit = false;
	}
      break;

    case SFPXLOADI_MOD0_FLOAT:
      {
	unsigned man = int_imm & 0x007FFFFF;
	int exp = ((int_imm >> 23) & 0xFF) - 127;

	if ((man & 0xFFFF) == 0)
	  {
	    // Fits in fp16b
	    load_32bit = false;
	    new_mod = SFPLOADI_MOD0_FLOATB;
	    int_imm = rvtt_fp32_to_fp16b (int_imm);
	  }
	else if ((man & 0x1FFF) == 0 && exp < 16 && exp >= -14)
	  {
	    // Fits in fp16a
	    load_32bit = false;
	    new_mod = SFPLOADI_MOD0_FLOATA;
	    int_imm = rvtt_fp32_to_fp16a (int_imm);
	  }

	if (!load_32bit)
	  imm = GEN_INT (int_imm);
      }
      break;

    default:
      gcc_unreachable ();
    }

  if (load_32bit)
    {
      rtx tmp = gen_reg_rtx (XTT32SImode);
      emit_insn (gen_rvtt_sfploadi_int (tmp, const0_rtx, const0_rtx, const0_rtx,
					GEN_INT (int_imm >> 16),
					rvtt_gen_rtx_noval (XTT32SImode),
					lv, GEN_INT (SFPLOADI_MOD0_UPPER)));
      emit_insn (gen_rvtt_sfploadi_int (dst, const0_rtx, const0_rtx, const0_rtx,
					GEN_INT (int_imm & 0xFFFF),
					rvtt_gen_rtx_noval (XTT32SImode),
					tmp, GEN_INT (SFPLOADI_MOD0_LOWER)));
    }
  else
    emit_insn (gen_rvtt_sfploadi_int (dst, const0_rtx, const0_rtx, const0_rtx,
				      imm,
				      rvtt_gen_rtx_noval (XTT32SImode),
				      lv, GEN_INT (new_mod)));
}

void
rvtt_emit_sfpxfcmps (rtx addr, rtx v, rtx f, rtx mod)
{
  bool need_sub = false;
  rtx ref_val = gen_reg_rtx (XTT32SImode);
  int int_mod = INTVAL (mod);

  // gimple synth expand guarantees this
  gcc_assert (CONST_INT_P (f));
  unsigned int fval = INTVAL (f);

  // Wrapper will convert 0 to -0
  unsigned int fmt = int_mod & SFPXSCMP_MOD1_FMT_MASK;
  if (fval != 0 &&
      ((fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x80000000)
       || (fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x8000)))
    {
      need_sub = true;
      // FIXME: Just teach sfpxloadi about this.
      if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f800000)
	  || (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f80))
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_1);
      else if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf800000)
	       || (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf80))
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);
      else
	rvtt_emit_sfpxloadi (ref_val, rvtt_gen_rtx_noval (XTT32SImode), addr,
			     GEN_INT (rvtt_scmp2loadi_mod (fmt)), f,
			     const0_rtx, const0_rtx);
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

  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      emit_insn (gen_rvtt_sfpsetcc_v (v, GEN_INT (SFPSETCC_MOD1_LREG_GTE0)));
      emit_insn (gen_rvtt_sfpsetcc_v (v, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
      if (cmp == SFPXCMP_MOD1_CC_LTE)
	emit_insn (gen_rvtt_sfpcompc ());
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
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (SFPSETCC_MOD1_LREG_GTE0)));
      emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
      if (cmp == SFPXCMP_MOD1_CC_LTE)
	emit_insn (gen_rvtt_sfpcompc ());
    }
  else
    emit_insn (gen_rvtt_sfpsetcc_v (tmp, GEN_INT (rvtt_cmp_ex_to_setcc_mod1_map[INTVAL(mod)])));
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
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      rtx tmp = gen_reg_rtx (XTT32SImode);
      rvtt_emit_sfpxiadd_i (tmp, lv, addr, src, imm, GEN_INT (base_mod | SFPXCMP_MOD1_CC_GTE), true);
      rvtt_emit_sfpxiadd_i (dst, lv, addr, tmp, const0_rtx, GEN_INT (base_mod | SFPXCMP_MOD1_CC_NE));
      if (cmp == SFPXCMP_MOD1_CC_LTE)
	emit_insn (gen_rvtt_sfpcompc ());
      return;
    }

  bool need_loadi = true;
  bool is_signed = (modi & SFPXIADD_MOD1_SIGNED) == SFPXIADD_MOD1_SIGNED;
  bool is_12bits = modi & SFPXIADD_MOD1_12BIT;
  bool is_const_int = CONST_INT_P (imm);
  bool is_sub = bool (modi & SFPXIADD_MOD1_IS_SUB);
  int iv = is_const_int ? INTVAL (imm) : 0xffffffff;

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
  else if (is_12bits)
    // Future work
    //need_loadi = false;
    gcc_unreachable ();

  rtx set_cc_arg = src;

  bool need_setcc = bool (cmp & SFPXCMP_MOD1_CC_MASK);
  if (need_loadi)
    {
      // Load imm into dst
      int loadi_mod = is_signed ? SFPXLOADI_MOD0_INT32 : SFPXLOADI_MOD0_UINT32;
      rvtt_emit_sfpxloadi (dst, rvtt_gen_rtx_noval (XTT32SImode), addr,
			   GEN_INT (loadi_mod), imm, const0_rtx, const0_rtx);
      
      unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;
      if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE)
	{
	  // Perform op w/ compare
	  mod1 |= cmp == SFPXCMP_MOD1_CC_LT ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	  emit_insn (gen_rvtt_sfpiadd_v_int (dst, dst, src, GEN_INT (mod1)));
	  need_setcc = false;
	}
      else
	{
	  // Perform op w/o compare, compare with SETCC
	  mod1 |= SFPIADD_MOD1_CC_NONE;
	  emit_insn (gen_rvtt_sfpiadd_v_int (dst, dst, src, GEN_INT (mod1)));
	  set_cc_arg = dst;
	}
    }
  else if (is_const_int)
    {
      if (iv != 0)
	{
	  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE)
	    {
	      // Perform op w/ compare
	      unsigned int mod1 = (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	      emit_insn (gen_rvtt_sfpiadd_i_int (dst, lv, src, imm, GEN_INT(mod1 | SFPIADD_MOD1_ARG_IMM)));
	      need_setcc = false;
	    }
	  else
	    {
	      // Perform op w/o compare
	      emit_insn(gen_rvtt_sfpiadd_i_int (dst, lv, src, imm,
						GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
	      set_cc_arg = dst;
	    }
	}
      else if (dst_used || !(modi & SFPXIADD_MOD1_DST_UNUSED))
	{
	  rtx insn;
	  if (REG_P (lv))
	    insn = gen_rvtt_sfpassign_lv (dst, lv, src);
	  else
	    insn = gen_rvtt_sfpassign (dst, src);
	  emit_insn (insn);
	}
    }
  else
    {
      // This code path could handle the case where the operand isn't a CONST_INT (so
      // the value isn't known at compile time) but some (future) mechanism
      // (wrapper API or pragma) ensures that the resulting value fits in 12
      // bits and so an IADDI can be used.
      gcc_assert (is_12bits);
      gcc_unreachable ();
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
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      rvtt_emit_sfpxiadd_v (dst, srcb, srca, GEN_INT (base_mod | SFPXCMP_MOD1_CC_GTE));
      emit_insn(gen_rvtt_sfpsetcc_v (dst, GEN_INT (SFPSETCC_MOD1_LREG_NE0)));
      if (cmp == SFPXCMP_MOD1_CC_LTE)
	emit_insn (gen_rvtt_sfpcompc ());
      return;
    }

  bool is_sub = bool (modi & SFPXIADD_MOD1_IS_SUB);
  unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;

  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE)
    {
      // Perform op w/ compare
      mod1 |= cmp == SFPXCMP_MOD1_CC_LT ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
      emit_insn (gen_rvtt_sfpiadd_v_int (dst, srcb, srca, GEN_INT (mod1)));
    }
  else
    {
    // Perform op w/o compare
    mod1 |= SFPIADD_MOD1_CC_NONE;
    emit_insn (gen_rvtt_sfpiadd_v_int (dst, srcb, srca, GEN_INT (mod1)));
    if (cmp != 0)
      // Must be EQ0 or NE0, compare with SETCC
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
