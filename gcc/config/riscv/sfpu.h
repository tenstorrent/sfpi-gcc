#include <tree.h>

#include <map>

#ifndef GCC_RISCV_SFPU_H
#define GCC_RISCV_SFPU_H

struct riscv_sfpu_insn_data {
  enum insn_id {
#define SFPU_BUILTIN(id, fmt, en, cc, lv, pslv) id,
#define SFPU_NO_TGT_BUILTIN(id, fmt, en, cc, lv, pslv) id,
#include "sfpu-insn.h"

    nonsfpu
  };

  const enum insn_id id;
  const char *name;
  tree decl;
  const bool can_set_cc;
  const bool live;
  const bool pseudo_live;
};

extern void riscv_sfpu_insert_insn(int idx, const char*name, tree decl);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const char *call);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const riscv_sfpu_insn_data::insn_id id);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_insn_data(const gcall *stmt);

extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);
extern bool riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, const gcall *stmt);

extern const riscv_sfpu_insn_data * riscv_sfpu_get_live_version(const riscv_sfpu_insn_data *insnd);
extern const riscv_sfpu_insn_data * riscv_sfpu_get_notlive_version(const riscv_sfpu_insn_data *insnd);

extern bool riscv_sfpu_sets_cc(const riscv_sfpu_insn_data *insnd, gcall *stmt);

#endif
