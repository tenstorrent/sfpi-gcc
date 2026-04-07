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

// order of operands:
// XTT_IPTR:CST0/SSA:iptr
// XTT_VEC:SSA:LVs[]
// XTT_VEC:SSA:src ops[]
// UINT:CST/SSA:immediate
// UINT:CST0/SSA:imm var
// UINT:CST:imm id
// UINT:CST:mod & int-ops

// This doesn't need to be GTY as the decls are also held in a riscv_builtin
// GTY array.
struct rvtt_insn_data {
  enum insn_id : uint8_t {
#define RVTT_FN(id, av, sfx, fmt, fl, ops) id,
#include "rvtt-insn.def"
  hwm
    };
public:
  enum flags_t : uint16_t {
    MOD_SHIFT,
    VAR_SHIFT,
    LV_SHIFT,
    COMMUTE_SHIFT,
    NUM_CLOBBERS_SHIFT,
    NUM_CLOBBERS_BITS = 2,
    CLOBBER_SHIFT = NUM_CLOBBERS_SHIFT + NUM_CLOBBERS_BITS,
    CLOBBER_BITS = 3,

    HWM = CLOBBER_SHIFT + CLOBBER_BITS,

    // Initialized via int operand, but not stored with this type.
    CC_MASK_SHIFT = 16,

    HAS_MOD = 1 << MOD_SHIFT, // Has a MOD operand
    HAS_VAR = 1 << VAR_SHIFT, // Has a variable immediate operand
    HAS_LV = 1 << LV_SHIFT,   // Has an explicit live value operand
    COMMUTES = 1 << COMMUTE_SHIFT, // First 2 srcs commute

    NUM_CLOBBERS_MASK = (1 << NUM_CLOBBERS_BITS) - 1,
    CLOBBER_MASK = (1 << CLOBBER_BITS) - 1,
  };
  static_assert (HWM <= 16);

public:
  class op_t
  {
  public:
    enum kind_t {
      NONE, // No arg
      SIGNED, // Signed integer
      UNSIGNED, // Unsigned integer
      EITHER, // Either signed or unsigned
      MOD, // Mod operand
      XMOD, // XMod operand (one of the x pseudo builtins)
      RUNTIME, // Runtime value

      EARLY = 1 << 6,
      CHECKED = 1 << 7,
    };

  private:
    unsigned enc;

    enum {
      // MOD overlays BITS, ENCODE & BIAS
      MOD_shift = 0,
      MOD_bits = 16,
      BITS_shift = 0,
      BITS_bits = 5,
      ENCODE_shift = BITS_shift + BITS_bits,
      ENCODE_bits = 5,
      BIAS_shift = ENCODE_shift + ENCODE_bits,
      BIAS_bits = 5,
      ENC_shift = BIAS_shift + BIAS_bits,

      KIND_shift = MOD_shift + MOD_bits,
      KIND_bits = 8,

      ARGNO_shift = KIND_shift + KIND_bits,
      ARGNO_bits = 8,
      HWM_shift = ARGNO_shift + ARGNO_bits,
    };
    static_assert (ENC_shift <= MOD_shift + MOD_bits);
    static_assert (HWM_shift <= 32);

  public:
    constexpr op_t (kind_t kind_and_check = NONE, unsigned bits_or_mask = 0, unsigned encode = 0, unsigned bias = 0)
      : enc (bits_or_mask | encode << ENCODE_shift | bias << BIAS_shift | kind_and_check << KIND_shift)
    {}

    static kind_t early (kind_t kind) { return kind_t (kind | EARLY | CHECKED); }
    static kind_t checked (kind_t kind) { return kind_t (kind | CHECKED); }
    operator bool () const { return enc != 0; }
    bool is_checked () const { return bool ((enc >> KIND_shift) & CHECKED); }
    bool is_early () const { return bool ((enc >> KIND_shift) & EARLY); }
    kind_t kind () const { return kind_t ((enc >> KIND_shift) & (((1u << KIND_bits) - 1u) ^ (CHECKED | EARLY))); }
    bool is_mod () const { return kind () == MOD; }
    bool is_xmod () const { return kind () == XMOD; }
    bool is_runtime () const { return kind () == RUNTIME; }

    unsigned mod () const { return (enc >> MOD_shift) & ((1u << MOD_bits) - 1u); }
    unsigned bits () const { return (enc >> BITS_shift) & ((1u << BITS_bits) - 1u); }
    unsigned encode () const { return (enc >> ENCODE_shift) & ((1u << ENCODE_bits) - 1u); }
    unsigned bias () const { return (enc >> BIAS_shift) & ((1u << BIAS_bits) - 1u); }

    int argno () const { return (enc >> ARGNO_shift) & ((1u << ARGNO_bits) - 1u); }

    void argno (unsigned n) { enc |= n << ARGNO_shift; }
  };

public:
  // If (when?) we had variadic macros, this interstital class would not be
  // needed. It exists to allow us to wrap operand info in () inside the
  // defining macros, and then use that as an argument to a ctor.
  class ops_t
  {
    op_t ops[6];

  public:
    // This is so we can use parens in the builtin definitions We deliberately
    // have fewer args here, so that we can just use the operand's bool operator
    // for find the end and don't care about length specifically.
    constexpr ops_t (op_t a = op_t (),
		     op_t b = op_t (),
		     op_t c = op_t (),
		     op_t d = op_t (),
		     op_t e = op_t ())
    {
      ops[0] = a;
      ops[1] = b;
      ops[2] = c;
      ops[3] = d;
      ops[4] = e;
    }
    ops_t (const ops_t &) = default;

    constexpr op_t const &operator[] (unsigned ix) const
    { return ops[ix]; };
    void set_argno (unsigned ix, unsigned argno) {
      ops[ix].argno (argno);
    }
  };

public:
  constexpr rvtt_insn_data (insn_id id_, const char *name_, uint32_t flags_, ops_t ops_)
    : decl (nullptr), name (name_), flags (flags_t (flags_ & 0xffff)),
      cc_mask (uint16_t ((flags_ >> CC_MASK_SHIFT) & 0xffff)),
      id (id_), clobber_pos (0), src_pos (-1), arg_num (0), ops (ops_) {}

public:
  void init ();
  void override (flags_t flags_, const ops_t &ops_) {
    flags = flags_;
    ops = ops_;
  }

public:
  tree decl;
  const char *name;

private:
  flags_t flags;
  uint16_t cc_mask;

public:
  insn_id id;

private:
  uint8_t mod_pos = 0;
  uint8_t clobber_pos : 4;
  int8_t src_pos : 4;
  uint8_t arg_num : 4;

public:
  ops_t ops;

public:
  bool is_live () const { return flags & HAS_LV; }
  int live_arg () const { return has_var (); }

  // We know these objects are in an array.
  // We never ask for the live version of the last entry.
  const rvtt_insn_data *get_live () const {
    if (this[1].is_live () && this[1].decl)
      return this + 1;
    return nullptr;
  }
  const rvtt_insn_data *get_not_live () const {
    // Never ask for the notlive version of sfpassign_lv
    return this - int (is_live ());
  }

public:
  int num_args () const { return arg_num; };

  bool has_mod () const { return flags & HAS_MOD; }
  int mod_arg () const { return mod_pos; }

  bool has_var () const { return flags & HAS_VAR; }
  int imm_arg () const { return ops[0].argno (); }
  int var_arg () const { return imm_arg () + 1; }
  int id_arg () const { return imm_arg () + 2; }

  bool has_src () const { return src_pos >= 0; }
  int src_arg () const { return src_pos; }

  unsigned imm_bits () const {
    return ops[0].bits () + bool (ops[0].bias ());
  }
  unsigned imm_encode () const { return ops[0].encode (); }

  int num_src_clobbers () const {
    return (flags >> NUM_CLOBBERS_SHIFT) & NUM_CLOBBERS_MASK;
  }
  int first_clobber_arg () const { return clobber_pos; }

public:
  bool sets_cc (gcall *stmt) const;
  bool srcs_commute (gcall *stmt) const;
};

extern unsigned int rvtt_cmp_ex_to_setcc_mod1_map[];

extern void rvtt_init_builtins ();
extern bool rvtt_record_builtin (unsigned idx, char const *, tree decl);
extern tree rvtt_emit_nonimm_prologue(unsigned int unique_id,
				      const rvtt_insn_data *insnd,
				      gcall *stmt,
				      gimple_stmt_iterator gsi);
extern void rvtt_link_nonimm_prologue(std::vector<tree> &load_imm_map,
				      unsigned int unique_id,
				      tree old_add,
				      const rvtt_insn_data *insnd,
				      gcall *stmt);

extern const rvtt_insn_data * rvtt_get_insn_data(const rvtt_insn_data::insn_id id);
extern const rvtt_insn_data * rvtt_get_insn_data(const gcall *stmt);

extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple *gimp);
extern bool rvtt_p(const rvtt_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi);

extern void rvtt_prep_stmt_for_deletion(gimple *stmt);
extern bool rvtt_get_fp16b(tree *value, gcall *stmt, const rvtt_insn_data *insnd);

extern bool rvtt_store_has_restrict_p(const rtx pat);
extern bool rvtt_reg_store_p(const rtx pat);
extern bool rvtt_l1_store_p(const rtx pat);

#endif
