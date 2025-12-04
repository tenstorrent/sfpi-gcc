/* TT helper routines
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#define INCLUDE_STRING
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
#include <unordered_map>

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

DEBUG_FUNCTION void debug_tree (tree node);

unsigned int rvtt_sfpu_lreg_count_global;

const int rvtt_name_stub_no_arch_len = 15;
const int rvtt_name_stub_len = 18;

struct str_cmp
{
  bool operator()(const char *a, const char *b) const
  {
     return std::strcmp(a, b) == 0;
  }
};

struct str_hash
{
  std::size_t operator()(const char *cstr) const
  {
    std::size_t hash = 5381;
    for (; *cstr != '\0' ; ++cstr)
      hash = (hash * 33) + *cstr;
    return hash;
  }
};

unsigned int rvtt_cmp_ex_to_setcc_mod1_map[] = {
  0,
  SFPSETCC_MOD1_LREG_LT0,
  SFPSETCC_MOD1_LREG_EQ0,
  SFPSETCC_MOD1_LREG_GTE0,
  SFPSETCC_MOD1_LREG_NE0,
};

static const char* arch_name_abbrev_list[] = {
  "_wh_",
  "_bh_",
};

static std::unordered_map<const char*, rvtt_insn_data&, str_hash, str_cmp> insn_map;
static const int NUMBER_OF_ARCHES = 2;
static const int NUMBER_OF_INTRINSICS = 136;

static GTY(()) rvtt_insn_data sfpu_insn_data_target[NUMBER_OF_ARCHES][NUMBER_OF_INTRINSICS] = {
  {
#define RVTT_RTL_ONLY(id, fl, nip, gp) { rvtt_insn_data::id, #id, nullptr, fl, -1, -1, 0, nip, gp, 0, 0 },
#define RVTT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_WH_RTL_ONLY(id, fl, sched) { rvtt_insn_data::id, #id, nullptr, fl, -1, -1, sched, -1, -1, 0, 0 },
#define RVTT_WH_PAD_RTL_ONLY(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#define RVTT_WH_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_WH_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_WH_PAD_BUILTIN(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#define RVTT_WH_PAD_NO_TGT_BUILTIN(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#include "rvtt-insn.h"
    { rvtt_insn_data::nonsfpu, "nonsfpu", nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 }
  },
  {
#define RVTT_RTL_ONLY(id, fl, nip, gp) { rvtt_insn_data::id, #id, nullptr, fl, -1, -1, 0, nip, gp, 0, 0 },
#define RVTT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_BH_RTL_ONLY(id, fl, sched) { rvtt_insn_data::id, #id, nullptr, fl, -1, -1, sched, -1, -1, 0, 0 },
#define RVTT_BH_PAD_RTL_ONLY(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#define RVTT_BH_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_BH_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) { rvtt_insn_data::id, #id, nullptr, fl, dap, mp, sched, nip, -1, nim, nis },
#define RVTT_BH_PAD_BUILTIN(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#define RVTT_BH_PAD_NO_TGT_BUILTIN(id) { rvtt_insn_data::id, #id, nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 },
#include "rvtt-insn.h"
    { rvtt_insn_data::nonsfpu, "nonsfpu", nullptr, 0x00, 0, 0, 0, 0, -1, 0, 0 }
  }
};

static std::vector<const rvtt_insn_data *> sfpu_rtl_insn_ptrs;

static rvtt_insn_data *sfpu_insn_data = sfpu_insn_data_target[0];
static const char* rvtt_builtin_name_stub;

// We have to put this decl here so gengtype emits the root here.
extern GTY(()) rtx rvtt_vec0_rtx;
rtx rvtt_vec0_rtx;

void
rvtt_insert_insn(int idx, const char* name, tree decl)
{
  // Search the table starting from where we left off last time
  static int start = 0;

  int arch;
  if (TARGET_XTT_TENSIX_WH)
    arch = 0;
  else if (TARGET_XTT_TENSIX_BH)
    arch = 1;
  else
    return;

  int offset = start;
  while (offset < NUMBER_OF_INTRINSICS)
    {
      // string is __rvtt_builtin_XX_<name>
      if ((strcmp(sfpu_insn_data_target[arch][offset].name, name) == 0) ||
	  (strcmp(sfpu_insn_data_target[arch][offset].name, &name[rvtt_name_stub_len]) == 0) ||
	  (strcmp(sfpu_insn_data_target[arch][offset].name, &name[rvtt_name_stub_no_arch_len]) == 0))
	{
	  sfpu_insn_data_target[arch][offset].decl = decl;
	  insn_map.insert(std::pair<const char*, rvtt_insn_data&>(name, sfpu_insn_data_target[arch][offset]));
	  start = offset + 1;
	  return;
	}
      offset++;
    }

  fprintf(stderr, "Failed to match insn %d named %s to builtin for arch index %d starting at builtin %d\n",
	  idx, name, arch, offset);
  gcc_assert(0);
}

// Match the rtl insns to the insn table
//
// There are lots of oddities given sfpx insns, gimple insns, rtl post-expand
// insns, internal insns, etc.
//
// Decided to put all of these into the insn table in rvtt-insn and making the
// matching explicit at the cost of more entries to manage.
//
// Note:
//  - gimple insns match w/ __builtin_ at the front
//  - rtl only (internal/post-expand) insns match w/o _rvtt_[xx]_ where xx is
//    the arch
static const rvtt_insn_data *
init_rtx_insnd(int code, int arch)
{
  DUMP("trying to lookup rtx name %d %s\n", code, insn_data[code].name);

  bool matches_other_arch = false;
  for (int i = 0; i < NUMBER_OF_ARCHES; i++) {
    if (i != arch &&
	strstr(insn_data[code].name, arch_name_abbrev_list[i]) != nullptr) {
      matches_other_arch = true;
      break;
    }
  }

  if (strncmp(insn_data[code].name, "rvtt_", 5) == 0 && !matches_other_arch) {
    // Try <__builtin_>name
    char name[100];
    sprintf(name, "__builtin_%s", insn_data[code].name);
    const rvtt_insn_data *tmp = rvtt_get_insn_data (name);
    if (tmp->id != rvtt_insn_data::nonsfpu) return tmp;

    // Try name
    tmp = rvtt_get_insn_data (&insn_data[code].name[5]);
    if (tmp->id != rvtt_insn_data::nonsfpu) return tmp;

    // Try name minus arch
    tmp = rvtt_get_insn_data(&insn_data[code].name[8]);
    if (tmp->id != rvtt_insn_data::nonsfpu) return tmp;

#if CHECKING_P
    fprintf (stderr, "Failed to match rvtt insn %s for arch index %d\n", insn_data[code].name, arch);
    gcc_unreachable ();
#endif
  }

  return &sfpu_insn_data[rvtt_insn_data::nonsfpu];
}

void
rvtt_init_builtins()
{
#if CHECKING_P
  {
    static const char *const wh_ids[] = {
#define RVTT_WH_RTL_ONLY(id, fl, sched) #id,
#define RVTT_WH_PAD_RTL_ONLY(id) #id,
#define RVTT_WH_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) #id,
#define RVTT_WH_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) #id,
#define RVTT_WH_PAD_BUILTIN(id) #id,
#define RVTT_WH_PAD_NO_TGT_BUILTIN(id) #id,
#include "rvtt-insn.h"
    };

    static const char *const bh_ids[] = {
#define RVTT_BH_RTL_ONLY(id, fl, sched) #id,
#define RVTT_BH_PAD_RTL_ONLY(id) #id,
#define RVTT_BH_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) #id,
#define RVTT_BH_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) #id,
#define RVTT_BH_PAD_BUILTIN(id) #id,
#define RVTT_BH_PAD_NO_TGT_BUILTIN(id) #id,
#include "rvtt-insn.h"
    };

    gcc_assert (sizeof (wh_ids) == sizeof (bh_ids));
    for (unsigned ix = 0; ix != sizeof (wh_ids) / sizeof (wh_ids[0]); ix++)
      gcc_assert (!strcmp (wh_ids[ix], bh_ids[ix]));
  }
#endif
  int arch;
  if (TARGET_XTT_TENSIX_WH) {
    arch = 0;
    rvtt_builtin_name_stub = "__builtin_rvtt_wh";
  } else if (TARGET_XTT_TENSIX_BH) {
    arch = 1;
    rvtt_builtin_name_stub = "__builtin_rvtt_bh";
  } else {
    return;
  }
  sfpu_insn_data = sfpu_insn_data_target[arch];

  // Fill in the non-builtin internal insns, sanity check the table
  for (int i = 0; i < NUMBER_OF_INTRINSICS; i++)
    {
      const int all_types_flag = (INSN_FLAGS_RISCV | INSN_FLAGS_EMPTY);
      const int type_flag = sfpu_insn_data[i].flags & all_types_flag;
      // At most one bit can be set.
      gcc_assert(type_flag == (type_flag & -type_flag));

      if (sfpu_insn_data[i].rtl_only_p ())
	insn_map.insert (std::pair<const char*, rvtt_insn_data&> (sfpu_insn_data[i].name,
								  sfpu_insn_data[i]));
    }

  // If these asserts fire, the rvtt-insn.h instruction tables are out of sync
  // across architectures
  for (int i = 1; i < NUMBER_OF_ARCHES; i++)
    {
      for (int j = 0; j < NUMBER_OF_INTRINSICS; j++)
	gcc_assert (sfpu_insn_data_target[0][j].id == sfpu_insn_data_target[i][j].id);

      gcc_assert(sfpu_insn_data_target[i][NUMBER_OF_INTRINSICS - 1].id == rvtt_insn_data::nonsfpu);
    }

  sfpu_rtl_insn_ptrs.resize(NUM_INSN_CODES);
  for (unsigned int i = 0; i < NUM_INSN_CODES; i++)
    sfpu_rtl_insn_ptrs[i] = init_rtx_insnd (i, arch);

  // Make synth_opcode and sfpnovalue const fns
  TREE_READONLY (sfpu_insn_data[rvtt_insn_data::synth_opcode].decl) = true;
  TREE_READONLY (sfpu_insn_data[rvtt_insn_data::sfpnovalue].decl) = true;

  // The real value cache is not available yet. :(
  // Perhaps there's a better place for this?
  rtx sf0 = rtx_alloc (CONST_DOUBLE);
  PUT_MODE (sf0, SFmode);
  real_from_integer (&sf0->u.rv, SFmode, 0, SIGNED);
  rvtt_vec0_rtx = gen_const_vec_duplicate (V64SFmode, sf0);
}

const char *
rvtt_get_builtin_name_stub ()
{
  return rvtt_builtin_name_stub;
}

const rvtt_insn_data *
rvtt_get_insn_data (const rvtt_insn_data::insn_id id)
{
  auto *res = &sfpu_insn_data[id];
  gcc_assert (!res->decl || TREE_CODE (res->decl) == FUNCTION_DECL);
  return res;
}

const rvtt_insn_data*
rvtt_get_insn_data(const char *name)
{
  auto match = insn_map.find(name);
  auto *res = match == insn_map.end () ? &sfpu_insn_data[rvtt_insn_data::nonsfpu] : &match->second;
  gcc_assert (!res->decl || TREE_CODE (res->decl) == FUNCTION_DECL);
  return res;
}

const rvtt_insn_data *
rvtt_get_insn_data (const gcall *stmt)
{
  tree fn_ptr = gimple_call_fn (stmt);

  return fn_ptr ? rvtt_get_insn_data (IDENTIFIER_POINTER (DECL_NAME (TREE_OPERAND (fn_ptr, 0)))) : nullptr;
}

bool
rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple *g)
{
  bool found = false;

  if (g == nullptr || g->code != GIMPLE_CALL) return false;

  *stmt = dyn_cast<gcall *> (g);
  tree fn_ptr = gimple_call_fn (*stmt);

  if (fn_ptr && TREE_CODE (fn_ptr) == ADDR_EXPR)
    {
      tree fn_decl = TREE_OPERAND (fn_ptr, 0);
      if (TREE_CODE(fn_decl) == FUNCTION_DECL)
	{
	  *insnd = rvtt_get_insn_data(IDENTIFIER_POINTER (DECL_NAME (fn_decl)));
	  found = (*insnd != nullptr && (*insnd)->id != rvtt_insn_data::nonsfpu);
	}
    }

  return found;
}

bool
rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi)
{
  bool found = false;
  gimple *g = gsi_stmt (gsi);

  if (g != nullptr && g->code == GIMPLE_CALL)
    {
      found = rvtt_p(insnd, stmt, g);
    }

  return found;
}

bool
rvtt_p(const rvtt_insn_data **insnd, const rtx_insn *insn)
{
  int code = INSN_CODE(insn);
  *insnd = (code == -1) ? &sfpu_insn_data[rvtt_insn_data::nonsfpu] : sfpu_rtl_insn_ptrs[code];
  return (*insnd)->id != rvtt_insn_data::nonsfpu;
}

// Relies on live instructions being next in sequence in the insn table
const rvtt_insn_data *
rvtt_get_live_version(const rvtt_insn_data *insnd)
{
  const rvtt_insn_data *out = nullptr;

  if (insnd->id < rvtt_insn_data::nonsfpu)
    {
      if (sfpu_insn_data[insnd->id + 1].live_p())
	{
	  out = &sfpu_insn_data[insnd->id + 1];
	}
    }

  return out;
}

const rvtt_insn_data *
rvtt_get_notlive_version(const rvtt_insn_data *insnd)
{
  const rvtt_insn_data *out = insnd;

  if (insnd->live_p())
    {
      out = &sfpu_insn_data[insnd->id - 1];
    }

  return out;
}

static long int
get_int_arg(gcall *stmt, unsigned int arg)
{
  tree decl = gimple_call_arg(stmt, arg);
  if (decl)
  {
    gcc_assert(TREE_CODE(decl) == INTEGER_CST);
    return *(decl->int_cst.val);
  }
  return -1;
}

bool
rvtt_sets_cc(const rvtt_insn_data *insnd, gcall *stmt)
{
  bool sets_cc = false;

  if (insnd->can_set_cc_p())
    {
      long int arg = (insnd->mod_pos != -1) ? get_int_arg (stmt, insnd->mod_pos) : 0;
      if (insnd->id == rvtt_insn_data::sfpxiadd_i)
	{
	  if (arg & SFPXCMP_MOD1_CC_MASK)
	    sets_cc = true;
	}
      else if (insnd->id == rvtt_insn_data::sfpxiadd_v)
	{
	  if (arg & SFPXCMP_MOD1_CC_MASK)
	    sets_cc = true;
	}
      else if (insnd->id == rvtt_insn_data::sfpexexp)
	{
	  if (arg == 2 || arg == 3 || arg == 8 || arg == 9 || arg == 10 || arg == 11)
	    sets_cc = true;
	}
      else if (insnd->id == rvtt_insn_data::sfplz)
	{
	  if (arg == 2 || arg == 8 || arg == 10)
	    sets_cc = true;
	}
      else
	{
	  sets_cc = true;
	}
    }

  return sets_cc;
}

bool rvtt_permutable_operands(const rvtt_insn_data *insnd, gcall *stmt)
{
  return
      insnd->id == rvtt_insn_data::sfpand ||

      insnd->id == rvtt_insn_data::sfpor ||

      insnd->id == rvtt_insn_data::sfpxor ||

      (insnd->id == rvtt_insn_data::sfpxiadd_v &&
       (get_int_arg (stmt, 2) & SFPXIADD_MOD1_IS_SUB) == 0);
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

char const *
rvtt_synth_insn_pattern (rtx *operands, unsigned clobber_op)
{
  uint32_t reg_mask = 0;
  uint32_t reg_ops = 0;
  rtx src_reg = operands[SYNTH_src];
  if (REG_P (src_reg))
    {
      unsigned src_shift = unsigned (INTVAL (operands[SYNTH_src_shift]));
      reg_mask |= 0xf << src_shift;
      reg_ops |= rvtt_sfpu_regno (src_reg) << src_shift;
    }
  bool has_dst = clobber_op > SYNTH_dst;
  if (has_dst)
    {
      rtx dst_reg = operands[SYNTH_dst];
      gcc_assert (REG_P (dst_reg));
      unsigned dst_shift = unsigned (INTVAL (operands[SYNTH_dst_shift]));
      reg_mask |= 0xf << dst_shift;
      reg_ops |= rvtt_sfpu_regno (dst_reg) << dst_shift;
    }
  gcc_assert (!reg_mask == !REG_P (operands[clobber_op]));

  uint32_t opcode = INTVAL (operands[SYNTH_opcode]);
  static char pattern[100];
  unsigned pos = 0;
  unsigned synth_opno = SYNTH_synthed;
  if (uint32_t reg_change = (opcode & reg_mask) ^ reg_ops)
    {
      // The register assignments here are different from those of the
      // first synth encountered.  We must adjust the incomming
      // pattern.
      opcode ^= reg_change;
      operands[SYNTH_opcode] = gen_rtx_CONST_INT (SImode, reg_change);
      pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		       "li\t%%%d,%%%d\n\txor\t%%%d,%%%d,%%%d\n\t",
		       clobber_op, SYNTH_opcode,
		       clobber_op, clobber_op, SYNTH_synthed);
      synth_opno = clobber_op;
    }

  pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		   "sw\t%%%u, %%%d\t# %d:%x",
		   synth_opno, SYNTH_mem,
		   unsigned (INTVAL (operands[SYNTH_id])), opcode);
  bool has_lv = false;
  if (has_dst)
    {
      pos += snprintf (&pattern[pos], sizeof (pattern) - pos, " %%%d :=", SYNTH_dst);
      has_lv = REG_P (operands[SYNTH_lv]);
      if (has_lv)
	pos += snprintf (&pattern[pos], sizeof (pattern) - pos, " LV");
    }
  if (REG_P (src_reg))
    pos += snprintf (&pattern[pos], sizeof (pattern) - pos, &", %%%d"[!has_lv], SYNTH_src);

  // NOPS was a grayskull feature
  unsigned nops = unsigned (INTVAL (operands[SYNTH_flags])) & INSN_SCHED_NOP_MASK;
  gcc_assert (!nops);

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
  int mod0 = get_int_arg(stmt, insnd->mod_pos);
  bool representable = false;
  tree arg = gimple_call_arg(stmt, SFPXLOADI_IMM_POS);

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

void rvtt_emit_sfpassignlreg(rtx dst, rtx lr)
{
  int lregnum = INTVAL(lr);
  SET_REGNO(dst, SFPU_REG_FIRST + lregnum);
  emit_insn(gen_rvtt_sfpassignlreg_int(dst));
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

rtx
rvtt_sfpsynth_insn_dst (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			rtx src, unsigned src_shift, rtx dst, unsigned dst_shift, rtx lv)
{
  return gen_rvtt_sfpsynth_insn_dst
    (gen_rtx_MEM (SImode, addr), GEN_INT (icode), GEN_INT (flags), synth, GEN_INT (opcode), id,
     src, GEN_INT (src_shift), dst, GEN_INT (dst_shift), lv ? lv : rvtt_vec0_rtx);
}

rtx
rvtt_sfpsynth_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
		    rtx src, unsigned src_shift)
{
  return gen_rvtt_sfpsynth_insn
    (gen_rtx_MEM (SImode, addr), GEN_INT (icode), GEN_INT (flags), synth, GEN_INT (opcode), id,
     src, GEN_INT (src_shift));
}

rtx
rvtt_sfpsynth_store_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
		          rtx src, unsigned src_shift)
{
  return gen_rvtt_sfpsynth_store_insn
    (gen_rtx_MEM (SImode, addr), GEN_INT (icode), GEN_INT (flags), synth, GEN_INT (opcode), id,
     src, GEN_INT (src_shift));
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
  tree immarg = gimple_call_arg(stmt, insnd->nonimm_pos);

  // Insert insns to generate:
  //   sum = unique_id + ((raw & nonimm_mask) << nonimm_shft)
  tree mask = emit_mask(immarg, insnd->nonimm_mask, &gsi, stmt);
  tree shft = emit_shift(mask, insnd->nonimm_shft, &gsi, stmt);
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
  gimple_call_set_arg(stmt, insnd->nonimm_pos + 1, sum);
  // Save unique_id in insn's id field
  gimple_call_set_arg(stmt, insnd->nonimm_pos + 2,
		      build_int_cst(integer_type_node, unique_id));
  update_stmt (stmt);
}

// The code below makes me sad.  This is much harder because the operands can
// either be an array or a single operand (rather than an array of 1)...
int rvtt_get_insn_operand_count(const rtx_insn *insn)
{
  rtx pat = PATTERN(insn);
  int code = GET_CODE(pat);
  rtx operands;
  int count;

  if (code == PARALLEL) {
    pat = XVECEXP(pat, 0, 0);
    code = GET_CODE(pat);
  }

  switch (code) {
  case SET:
    operands = XEXP(pat, 1);
    if (GET_CODE(operands) == UNSPEC_VOLATILE) {
      count = XVECLEN(operands, 0);
    } else if (GET_CODE(operands) == REG) {
      count = 1;
    } else {
      // XXXXX the only way to hit this is w/ a a load/store/spill
      // which makes the program invalid, return anything
      // This will be an easter egg when load/store/spill is supported...
      gcc_assert(GET_CODE(operands) == MEM);
      return 0;
    }
    break;

  case UNSPEC_VOLATILE:
    operands = pat;
    count = XVECLEN(operands, 0);
    break;

  default:
    fprintf(stderr, "unexpected pattern in sfpu insn, %d %s\n", code, insn_data[INSN_CODE(insn)].name);
    gcc_assert(0);
    break;
  }

  return count;
}

rtx rvtt_get_insn_operand(int which, const rtx_insn *insn)
{
  rtx pat = PATTERN(insn);
  int code = GET_CODE(pat);
  rtx operands;
  rtx op;

  if (code == PARALLEL) {
    pat = XVECEXP(pat, 0, 0);
    code = GET_CODE(pat);
  }

  switch (code) {
  case SET:
    operands = XEXP(pat, 1);
    if (GET_CODE(operands) == UNSPEC_VOLATILE) {
      op = XVECEXP(operands, 0, which);
    } else {
      gcc_assert(GET_CODE(operands) == REG);
      op = operands;
    }
    break;

  case UNSPEC_VOLATILE:
    operands = pat;
    op = XVECEXP(operands, 0, which);
    break;

  default:
    fprintf(stderr, "unexpected pattern in sfpu insn, %d %s", code, insn_data[INSN_CODE(insn)].name);
    gcc_assert(0);
    break;
  }

  return op;
}

int rvtt_get_insn_dst_regno(const rtx_insn *insn)
{
  rtx pat = PATTERN(insn);
  if (GET_CODE (pat) == PARALLEL)
    {
      pat = XVECEXP(pat, 0, 0);
    }
  if (GET_CODE (pat) == SET)
    {
      rtx reg = XEXP(pat, 0);
      return REGNO(reg);
    }
  else
    {
      return -1;
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

#if !RVTT_DEBUG_MAKE_ALL_LOADS_L1_LOADS
      tree decl = (TREE_CODE(exp) == PARM_DECL ||
		   TREE_CODE(exp) == VAR_DECL) ? exp : TREE_OPERAND(exp, 0);
      if (decl != NULL_TREE &&
	  lookup_attribute(attrib, TYPE_ATTRIBUTES(TREE_TYPE(decl))))
	{
	  return true;
	}
#else
      return true;
#endif
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

#include "gt-rvtt.h"
