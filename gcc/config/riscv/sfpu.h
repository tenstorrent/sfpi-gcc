#include <tree.h>

#include <map>
#include <vector>

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
#define SFPU_INTERNAL(id, nim, sched) id,
#define SFPU_BUILTIN(id, fmt, cc, lv, hho, dap, mp, sched, nip, in, nim, nis) id,
#define SFPU_NO_TGT_BUILTIN(id, fmt, cc, lv, hho, dap, mp, sched, nip, in, nim, nis) id,
#define SFPU_GS_BUILTIN(id, fmt, cc, lv, hho, dap, mp, sched, nip, in, nim, nis) id,
#define SFPU_GS_NO_TGT_BUILTIN(id, fmt, cc, lv, hho, dap, mp, sched, nip, in, nim, nis) id,
#define SFPU_GS_PAD_BUILTIN(id) id,
#define SFPU_GS_PAD_NO_TGT_BUILTIN(id) id,
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
  const int schedule;   // see INSN_SCHEDULE_* flags above
  const int nonimm_pos;	// +0 raw, +1 raw + load_immediate, +2 unique_id/insn value
  const bool internal;
  const unsigned int nonimm_mask;
  const int nonimm_shft;

  inline bool uses_dst_as_src() const { return dst_arg_pos != -1; }
};

extern unsigned int riscv_sfpu_cmp_ex_to_setcc_mod1_map[];

extern void riscv_sfpu_insert_insn(int idx, const char*name, tree decl);
extern void riscv_sfpu_init_builtins();
extern const char * riscv_sfpu_get_builtin_name_stub();
extern tree riscv_sfpu_emit_nonimm_prologue(unsigned int unique_id,
					    const riscv_sfpu_insn_data *insnd,
					    gcall *stmt,
					    gimple_stmt_iterator gsi);
extern void riscv_sfpu_link_nonimm_prologue(std::vector<tree> &load_imm_map,
					    unsigned int unique_id,
					    tree old_add,
					    const riscv_sfpu_insn_data *insnd,
					    gcall *stmt);
extern void riscv_sfpu_cleanup_nonimm_lis(function *fun);
extern rtx riscv_sfpu_get_insn_operands(int *noperands, rtx_insn *insn);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const char *name);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const riscv_sfpu_insn_data::insn_id id);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const gcall *stmt);

extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);
extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, rtx_insn *insn);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_live_version(const riscv_sfpu_insn_data *insnd);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_notlive_version(const riscv_sfpu_insn_data *insnd);

extern bool riscv_sfpu_sets_cc(const riscv_sfpu_insn_data *insnd, gcall *stmt);
extern bool riscv_sfpu_permutable_operands(const riscv_sfpu_insn_data *insnd, gcall *stmt);

extern void riscv_sfpu_prep_stmt_for_deletion(gimple *stmt);
extern bool riscv_sfpu_get_fp16b(tree *value, gcall *stmt, const riscv_sfpu_insn_data *insnd);

extern uint32_t riscv_sfpu_fp32_to_fp16a(const uint32_t val);
extern uint32_t riscv_sfpu_fp32_to_fp16b(const uint32_t val);
extern uint32_t riscv_sfpu_scmp2loadi_mod(int mod);
extern bool riscv_sfpu_get_next_sfpu_insn(const riscv_sfpu_insn_data **insnd,
					  gcall **stmt,
					  gimple_stmt_iterator gsi,
					  bool allow_non_sfpu = false);
extern bool riscv_sfpu_get_next_sfpu_insn(const riscv_sfpu_insn_data **insnd,
					  rtx_insn **next_insn,
					  rtx_insn *insn,
					  bool allow_non_sfpu = false);

#endif
