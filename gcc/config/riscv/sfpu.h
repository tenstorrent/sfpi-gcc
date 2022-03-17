#include <tree.h>

#include <map>

#ifndef GCC_RISCV_SFPU_H
#define GCC_RISCV_SFPU_H

constexpr unsigned int SFP_LREG_COUNT = 4;

constexpr unsigned int SFPMAD_MOD1_OFFSET_NONE = 0;
constexpr unsigned int SFPMAD_MOD1_OFFSET_POSH = 1;
constexpr unsigned int SFPMAD_MOD1_OFFSET_NEGH = 3;

constexpr unsigned int SFPLOADI_IMM_ARG_POS = 2;
constexpr unsigned int SFPLOADI_LV_IMM_ARG_POS = 3;
constexpr unsigned int SFPLOADI_MOD0_FLOATB = 0;
constexpr unsigned int SFPLOADI_MOD0_FLOATA = 1;
constexpr unsigned int SFPLOADI_MOD0_USHORT = 2;
constexpr unsigned int SFPLOADI_MOD0_SHORT = 4;
constexpr unsigned int SFPLOADIEX_MOD0_FLOAT = 16;

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

constexpr unsigned int SFPIADD_MOD1_ARG_LREG_DST = 0;
constexpr unsigned int SFPIADD_MOD1_ARG_IMM = 1;
constexpr unsigned int SFPIADD_MOD1_ARG_2SCOMP_LREG_DST = 2;
constexpr unsigned int SFPIADD_MOD1_CC_LT0 = 0;
constexpr unsigned int SFPIADD_MOD1_CC_NONE = 4;
constexpr unsigned int SFPIADD_MOD1_CC_GTE0 = 8;

constexpr unsigned int SFPCMP_EX_MOD1_CC_NONE = 0;
constexpr unsigned int SFPCMP_EX_MOD1_CC_LT = 1;
constexpr unsigned int SFPCMP_EX_MOD1_CC_EQ = 2;
constexpr unsigned int SFPCMP_EX_MOD1_CC_GTE = 3;
constexpr unsigned int SFPCMP_EX_MOD1_CC_NE = 4;
constexpr unsigned int SFPCMP_EX_MOD1_CC_LTE = 5;
constexpr unsigned int SFPCMP_EX_MOD1_CC_GT = 6;
constexpr unsigned int SFPCMP_EX_MOD1_CC_MASK = 7;

constexpr unsigned int SFPSCMP_EX_MOD1_FMT_A = 8;

constexpr unsigned int SFPIADD_EX_MOD1_IS_SUB = 16;
constexpr unsigned int SFPIADD_EX_SRC_ARG_POS = 1;
constexpr unsigned int SFPIADD_EX_IMM_ARG_POS = 2;

constexpr unsigned int SFPIADD_I_EX_MOD1_SIGNED = 8;
constexpr unsigned int SFPIADD_I_EX_MOD1_IS_12BITS = 32;

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

struct riscv_sfpu_insn_data {
  enum insn_id {
#define SFPU_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) id,
#define SFPU_NO_TGT_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) id,
#include "sfpu-insn.h"

    nonsfpu
  };

  const enum insn_id id;
  const char *name;
  tree decl;
  const bool can_set_cc;
  const bool live;
  const bool has_half_offset;
  const int dst_arg_pos;
  const int mod_pos;

  inline bool uses_dst_as_src() const { return dst_arg_pos != -1; }
};

extern void riscv_sfpu_insert_insn(int idx, const char*name, tree decl);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const char *call);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const riscv_sfpu_insn_data::insn_id id);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const gcall *stmt);

extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_live_version(const riscv_sfpu_insn_data *insnd);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_notlive_version(const riscv_sfpu_insn_data *insnd);

extern bool riscv_sfpu_sets_cc(const riscv_sfpu_insn_data *insnd, gcall *stmt);
extern bool riscv_sfpu_permutable_operands(const riscv_sfpu_insn_data *insnd, gcall *stmt);

extern void riscv_sfpu_prep_stmt_for_deletion(gimple *stmt);

#endif
