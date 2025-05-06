/* TT helper routines
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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

#include <tree.h>

#include <map>
#include <vector>
#include "rvtt-protos.h"

#ifndef GCC_RVTT_H
#define GCC_RVTT_H

// Set this to 1 before running gcc test suite so the hll code paths get
// heavily exercised (vs skipped entirely if not set)
#define RVTT_DEBUG_MAKE_ALL_LOADS_L1_LOADS 0

constexpr unsigned int SFPMAD_MOD1_OFFSET_NONE = 0;
// A * B + C
constexpr unsigned int SFPMAD_MOD1_BH_COMPL_A = 1; // negate A operand
constexpr unsigned int SFPMAD_MOD1_BH_COMPL_C = 2; // negate C operand

constexpr unsigned int SFPMOV_MOD1_NONE = 0;
constexpr unsigned int SFPMOV_MOD1_COMPL = 1; // negate
constexpr unsigned int SFPMOV_MOD1_ALL = 2; // copy all lanes
constexpr unsigned int SFPMOV_MOD1_CFG = 8; // read cfg register

constexpr unsigned int SFPLOADI_IMM_ARG_POS = 2;
constexpr unsigned int SFPLOADI_LV_IMM_ARG_POS = 3;
constexpr unsigned int SFPLOADI_MOD0_FLOATB = 0;
constexpr unsigned int SFPLOADI_MOD0_FLOATA = 1;
constexpr unsigned int SFPLOADI_MOD0_USHORT = 2;
constexpr unsigned int SFPLOADI_MOD0_SHORT = 4;
constexpr unsigned int SFPLOADI_MOD0_UPPER = 8;
constexpr unsigned int SFPLOADI_MOD0_LOWER = 10;
constexpr unsigned int SFPXLOADI_MOD0_32BIT_MASK = 16;
constexpr unsigned int SFPXLOADI_MOD0_INT32 = 16;
constexpr unsigned int SFPXLOADI_MOD0_UINT32 = 17;
constexpr unsigned int SFPXLOADI_MOD0_FLOAT = 18;
constexpr unsigned int SFPXLOADI_IMM_POS = 2;

constexpr unsigned int SFPEXEXP_MOD1_DEBIAS = 0;
constexpr unsigned int SFPEXEXP_MOD1_NODEBIAS = 1;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_SGN_EXP = 2;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_COMP_EXP = 8;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP = 10;

constexpr unsigned int SFPSETCC_MOD1_LREG_LT0 = 0;
constexpr unsigned int SFPSETCC_MOD1_IMM_BIT0 = 1;
constexpr unsigned int SFPSETCC_MOD1_LREG_NE0 = 2;
constexpr unsigned int SFPSETCC_MOD1_LREG_GTE0 = 4;
constexpr unsigned int SFPSETCC_MOD1_LREG_EQ0 = 6;
constexpr unsigned int SFPSETCC_MOD1_COMP = 8;

constexpr unsigned int SFPPUSHCC_MOD1_PUSH = 0;
constexpr unsigned int SFPPUSHCC_MOD1_REPLACE = 1;

constexpr unsigned int SFPPOPCC_MOD1_POP = 0;

constexpr unsigned int SFPCAST_MOD1_INT32_TO_FP32_RNE = 0;
constexpr unsigned int SFPCAST_MOD1_INT32_TO_FP32_RNS = 1;
// Added in BlackHole:
constexpr unsigned int SFPCAST_MOD1_SM32_TO_INT32 = 2;
constexpr unsigned int SFPCAST_MOD1_INT32_TO_SM32 = 3;

constexpr unsigned int SFPIADD_MOD1_ARG_LREG_DST = 0;
constexpr unsigned int SFPIADD_MOD1_ARG_IMM = 1;
constexpr unsigned int SFPIADD_MOD1_ARG_2SCOMP_LREG_DST = 2;
constexpr unsigned int SFPIADD_MOD1_CC_LT0 = 0;
constexpr unsigned int SFPIADD_MOD1_CC_NONE = 4;
constexpr unsigned int SFPIADD_MOD1_CC_GTE0 = 8;

constexpr unsigned int SFPXIADD_MOD1_SIGNED = 8;
constexpr unsigned int SFPXIADD_MOD1_IS_SUB = 16;
constexpr unsigned int SFPXIADD_MOD1_12BIT = 32;
constexpr unsigned int SFPXIADD_MOD1_16BIT = 64;
constexpr unsigned int SFPXIADD_MOD1_DST_UNUSED = 128;
constexpr unsigned int SFPXIADD_SRC_ARG_POS = 1;
constexpr unsigned int SFPXIADD_IMM_ARG_POS = 2;

constexpr unsigned int SFPXCMP_MOD1_CC_NONE = 0;
constexpr unsigned int SFPXCMP_MOD1_CC_LT = 1;
constexpr unsigned int SFPXCMP_MOD1_CC_EQ = 2;
constexpr unsigned int SFPXCMP_MOD1_CC_GTE = 3;
constexpr unsigned int SFPXCMP_MOD1_CC_NE = 4;
constexpr unsigned int SFPXCMP_MOD1_CC_LTE = 5;
constexpr unsigned int SFPXCMP_MOD1_CC_GT = 6;
constexpr unsigned int SFPXCMP_MOD1_CC_MASK = 7;

constexpr unsigned int SFPXSCMP_MOD1_FMT_A = 8;
constexpr unsigned int SFPXSCMP_MOD1_FMT_B = 16;
constexpr unsigned int SFPXSCMP_MOD1_FMT_FLOAT = 32;
constexpr unsigned int SFPXSCMP_MOD1_FMT_MASK = 0x38;
constexpr unsigned int SFPXSCMP_SRC_ARG_POS = 1;

constexpr unsigned int SFPXBOOL_MOD1_OR = 1;
constexpr unsigned int SFPXBOOL_MOD1_AND = 2;
constexpr unsigned int SFPXBOOL_MOD1_NOT = 3;
constexpr unsigned int SFPXBOOL_LEFT_TREE_ARG_POS = 1;
constexpr unsigned int SFPXBOOL_RIGHT_TREE_ARG_POS = 2;

constexpr unsigned int SFPXCONDB_TREE_ARG_POS = 0;
constexpr unsigned int SFPXCONDB_START_ARG_POS = 1;

constexpr unsigned int SFPXCONDI_TREE_ARG_POS = 0;

constexpr unsigned int SFPSHFT_MOD1_SHFT_IMM = 1;
constexpr unsigned int SFPSHFT_MOD1_SHFT_REG = 0;
// Added in BlackHole
constexpr unsigned int SFPSHFT_MOD1_LOGICAL = 0;
constexpr unsigned int SFPSHFT_MOD1_ARITHMETIC = 2;
constexpr unsigned int SFPSHFT_MOD1_SRC_LREG_C = 4;

constexpr unsigned int CREG_IDX_0 = 4;
constexpr unsigned int CREG_IDX_0P692871094 = 5;
constexpr unsigned int CREG_IDX_NEG_1P00683594 = 6;
constexpr unsigned int CREG_IDX_1P442382813 = 7;
constexpr unsigned int CREG_IDX_0P836914063 = 8;
constexpr unsigned int CREG_IDX_NEG_0P5 = 9;
constexpr unsigned int CREG_IDX_1 = 10;
constexpr unsigned int CREG_IDX_NEG_1 = 11;
constexpr unsigned int CREG_IDX_0P001953125 = 12;
constexpr unsigned int CREG_IDX_NEG_0P67480469 = 13;
constexpr unsigned int CREG_IDX_NEG_0P34472656 = 14;
constexpr unsigned int CREG_IDX_TILEID = 15;

constexpr unsigned int INSN_FLAGS_CAN_SET_CC         = 0x01;
constexpr unsigned int INSN_FLAGS_LIVE               = 0x02;
constexpr unsigned int INSN_FLAGS_HAS_HALF_OFFSET    = 0x04;
constexpr unsigned int INSN_FLAGS_RTL_ONLY           = 0x08;  // true if no builtin
// Next 3 are exclusive
constexpr unsigned int INSN_FLAGS_NON_SFPU           = 0x10;  // true if not an sfpu insn (eg, incrwc)
constexpr unsigned int INSN_FLAGS_NON_TT             = 0x20;  // true if not a tt insn (eg, load_immediate)
constexpr unsigned int INSN_FLAGS_EMPTY              = 0x40;  // true if doesn't emit asm (eg, assignlreg)

constexpr unsigned int SFPU_LREG_COUNT_WH = 8;
constexpr unsigned int SFPU_LREG_COUNT_BH = 8;
extern unsigned int rvtt_sfpu_lreg_count_global;

struct GTY(()) rvtt_insn_data {
  enum insn_id : unsigned;

  const enum insn_id id;
  const char *name;
  tree decl;
  const unsigned short flags;  // see flags above
  const short dst_arg_pos;
  const short mod_pos;
  const short schedule;    // see INSN_SCHEDULE_* flags in rvtt-protos.h
  const short nonimm_pos;  // pos of nonimm insn args, -1 val to store, +0 op, +1 loadimm id/fallback flag
  const short generic_pos ; // arg pos of arg w/ schedule info or -1 if na
  const unsigned int nonimm_mask;
  const short nonimm_shft;

  inline bool can_set_cc_p() const { return flags & INSN_FLAGS_CAN_SET_CC; }
  inline bool live_p() const { return flags & INSN_FLAGS_LIVE; }
  inline bool has_half_offset_p() const { return flags & INSN_FLAGS_HAS_HALF_OFFSET; }
  inline bool rtl_only_p() const { return flags & INSN_FLAGS_RTL_ONLY; }
  inline bool odd_bird_p() const { return flags & (INSN_FLAGS_NON_SFPU | INSN_FLAGS_NON_TT | INSN_FLAGS_EMPTY); }
  inline bool non_tt_p() const { return flags & INSN_FLAGS_NON_TT; }
  inline bool empty_p() const { return flags & INSN_FLAGS_EMPTY; }
  inline bool dst_as_src_p() const { return dst_arg_pos != -1; }
  inline bool schedule_p() const { return schedule != -1; }
  inline bool schedule_in_arg_p() const { return generic_pos != -1; }
  inline int schedule_arg_pos() const { return generic_pos; }
  inline bool schedule_from_arg_p(rtx_insn *insn) const;
  inline bool schedule_dynamic_p(rtx_insn *insn) const;
  inline int schedule_static_nops(rtx_insn *insn) const;
  inline bool schedule_has_dynamic_dependency_p(rtx_insn *insn) const;

  inline int nonimm_val_arg_pos() const { return nonimm_pos - 1; }
  inline int nonimm_op_arg_pos() const { return nonimm_pos; }
  inline int nonimm_idflag_arg_pos() const { return nonimm_pos + 1; }
};

enum rvtt_insn_data::insn_id : unsigned {
  // Note: this only pulls the "id" from the macros so WH/BH/etc are equivalent
#define RVTT_RTL_ONLY(id, nip, gp) id,
#define RVTT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) id,
#define RVTT_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) id,
#define RVTT_WH_RTL_ONLY(id, fl, sched) id,
#define RVTT_WH_PAD_RTL_ONLY(id) id,
#define RVTT_WH_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) id,
#define RVTT_WH_NO_TGT_BUILTIN(id, fmt, fl, dap, mp, sched, nip, nim, nis) id,
#define RVTT_WH_PAD_BUILTIN(id) id,
#define RVTT_WH_PAD_NO_TGT_BUILTIN(id) id,
#include "rvtt-insn.h"

  nonsfpu,
    };

extern unsigned int rvtt_cmp_ex_to_setcc_mod1_map[];

extern void rvtt_insert_insn(int idx, const char*name, tree decl);
extern void rvtt_init_builtins();
extern const char * rvtt_get_builtin_name_stub();
extern tree rvtt_emit_nonimm_prologue(unsigned int unique_id,
				      const rvtt_insn_data *insnd,
				      gcall *stmt,
				      gimple_stmt_iterator gsi);
extern void rvtt_link_nonimm_prologue(std::vector<tree> &load_imm_map,
				      unsigned int unique_id,
				      tree old_add,
				      const rvtt_insn_data *insnd,
				      gcall *stmt);
extern void rvtt_cleanup_nonimm_lis(function *fun);
extern int rvtt_get_insn_operand_count(const rtx_insn *insn);
extern rtx rvtt_get_insn_operand(int which, const rtx_insn *insn);

extern const rvtt_insn_data * rvtt_get_insn_data(const char *name);
extern const rvtt_insn_data * rvtt_get_insn_data(const rvtt_insn_data::insn_id id);
extern const rvtt_insn_data * rvtt_get_insn_data(const gcall *stmt);

extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);
extern bool rvtt_p(const rvtt_insn_data **insnd, const rtx_insn *insn);

extern const rvtt_insn_data * rvtt_get_live_version(const rvtt_insn_data *insnd);
extern const rvtt_insn_data * rvtt_get_notlive_version(const rvtt_insn_data *insnd);

extern bool rvtt_sets_cc(const rvtt_insn_data *insnd, gcall *stmt);
extern bool rvtt_permutable_operands(const rvtt_insn_data *insnd, gcall *stmt);

extern void rvtt_prep_stmt_for_deletion(gimple *stmt);
extern bool rvtt_get_fp16b(tree *value, gcall *stmt, const rvtt_insn_data *insnd);

extern uint32_t rvtt_fp32_to_fp16a(const uint32_t val);
extern uint32_t rvtt_fp32_to_fp16b(const uint32_t val);
extern uint32_t rvtt_scmp2loadi_mod(int mod);
extern bool rvtt_get_next_insn(const rvtt_insn_data **insnd,
			       gcall **stmt,
			       gimple_stmt_iterator gsi,
			       bool test_initial = false,
			       int allow_flags = 0);
extern bool rvtt_get_next_insn(const rvtt_insn_data **insnd,
			       rtx_insn **next_insn,
			       rtx_insn *insn,
			       bool test_initial = false,
			       int allow_flags = 0);
extern int rvtt_get_insn_dst_regno(const rtx_insn *insn);

inline bool rvtt_insn_data::schedule_from_arg_p(rtx_insn *insn) const
{
  return INTVAL(rvtt_get_insn_operand(schedule_arg_pos(), insn)) & (INSN_SCHED_NOP_MASK | INSN_SCHED_DYN);
}

inline bool rvtt_insn_data::schedule_dynamic_p(rtx_insn *insn) const
{
  return schedule_in_arg_p() ?
    (INTVAL(rvtt_get_insn_operand(schedule_arg_pos(), insn)) & INSN_SCHED_DYN) :
    (schedule & INSN_SCHED_DYN);
}

inline bool rvtt_insn_data::schedule_has_dynamic_dependency_p(rtx_insn *insn) const
{
  return schedule_in_arg_p() ?
    (INTVAL(rvtt_get_insn_operand(schedule_arg_pos(), insn)) & INSN_SCHED_DYN_DEP) :
    (schedule & INSN_SCHED_DYN_DEP);
}
inline int rvtt_insn_data::schedule_static_nops(rtx_insn *insn) const
{
  return schedule_in_arg_p() ?
    (INTVAL(rvtt_get_insn_operand(schedule_arg_pos(), insn)) & INSN_SCHED_NOP_MASK) :
    (schedule & INSN_SCHED_NOP_MASK);
}

extern bool rvtt_store_has_restrict_p(const rtx pat);
extern bool rvtt_reg_store_p(const rtx pat);
extern bool rvtt_l1_store_p(const rtx pat);

#endif
