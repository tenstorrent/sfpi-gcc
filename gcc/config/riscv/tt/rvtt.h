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

#include <tree.h>

#include <map>
#include <vector>
#include "rvtt-protos.h"

#ifndef GCC_RVTT_H
#define GCC_RVTT_H

constexpr unsigned int INSN_FLAGS_CAN_SET_CC         = 0x01; // builtin property
constexpr unsigned int INSN_FLAGS_LIVE               = 0x02; // builtin property
// no longer needed 0x04;
// no longer needed 0x08;
// no longer needed 0x20;
// no longer needed 0x40;

struct GTY(()) rvtt_insn_data {
  enum insn_id : unsigned;

  const enum insn_id id;
  const char *name;
  tree decl;
  const unsigned short flags;  // see flags above
  const short dst_arg_pos;
  const short mod_pos;

  const short nonimm_pos;  // pos of nonimm insn args, -1 val to store, +0 op, +1 loadimm id/fallback flag
  const unsigned int nonimm_mask;
  const short nonimm_shft;

  inline bool can_set_cc_p() const { return flags & INSN_FLAGS_CAN_SET_CC; }
  inline bool live_p() const { return flags & INSN_FLAGS_LIVE; }

  inline bool dst_as_src_p() const { return dst_arg_pos != -1; }

  inline int nonimm_val_arg_pos() const { return nonimm_pos - 1; }
  inline int nonimm_op_arg_pos() const { return nonimm_pos; }
  inline int nonimm_idflag_arg_pos() const { return nonimm_pos + 1; }
};

enum rvtt_insn_data::insn_id : unsigned {
  // Note: this only pulls the "id" from the macros so WH/BH/etc are equivalent
#define RVTT_FN(id, fmt, fl, dap, mp, nip, nim, nis) id,
#define RVTT_WH_FN(id, fmt, fl, dap, mp, nip, nim, nis) id,
#define RVTT_WH_PFN(id) id,
#include "rvtt-insn.def"

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

extern const rvtt_insn_data * rvtt_get_insn_data(const char *name);
extern const rvtt_insn_data * rvtt_get_insn_data(const rvtt_insn_data::insn_id id);
extern const rvtt_insn_data * rvtt_get_insn_data(const gcall *stmt);

extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);

extern const rvtt_insn_data * rvtt_get_live_version(const rvtt_insn_data *insnd);
extern const rvtt_insn_data * rvtt_get_notlive_version(const rvtt_insn_data *insnd);

extern bool rvtt_sets_cc(const rvtt_insn_data *insnd, gcall *stmt);
extern bool rvtt_permutable_operands(const rvtt_insn_data *insnd, gcall *stmt);

extern void rvtt_prep_stmt_for_deletion(gimple *stmt);
extern bool rvtt_get_fp16b(tree *value, gcall *stmt, const rvtt_insn_data *insnd);

extern uint32_t rvtt_fp32_to_fp16a(const uint32_t val);
extern uint32_t rvtt_fp32_to_fp16b(const uint32_t val);
extern uint32_t rvtt_scmp2loadi_mod(int mod);

extern bool rvtt_store_has_restrict_p(const rtx pat);
extern bool rvtt_reg_store_p(const rtx pat);
extern bool rvtt_l1_store_p(const rtx pat);

#endif
