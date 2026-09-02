/* SFPLOADMACRO target capability tables (macro planner Layer 4).
   Copyright (C) 2026 Tenstorrent Inc.

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

/* Pure data + pure field packing.  No IR, RTL, or GCC-internal types:
   this object must stay compilable standalone so the capability data can
   be unit-tested against the frozen-pass expectations without a toolchain
   build (see rvtt-macro-tables-test.cc).  Every raw word below was
   transcribed from the frozen pass or the vendor kernels, with its
   provenance audited constant by constant (see rvtt-macro-tables.h).  */

#include "rvtt-macro-tables.h"

namespace rvtt_macro {

const char *const refusal_target_macro_encoding_unproven
  = "target-macro-encoding-unproven";

/* ------------------------------------------------------------------ */
/* Instantiate the per-CPU data from the .def files: define every     */
/* X-macro, include the .def once per array.			      */
/* ------------------------------------------------------------------ */

#define XTT_EMPTY_SCALARS(name, nt, ns, db, lop, lis, ms, ams, amb, ab, \
			  ni, ai, nb, s16, cfg, own, drain, brk)
#define XTT_EMPTY_SLOT(p, sr, sv, dr, dv, fr, fv)
#define XTT_EMPTY_REFSETC16(r, v)
#define XTT_EMPTY_SEQ(n, m, w, ne, u0, t0, d0, s0, u1, t1, d1, s1,	\
		      u2, t2, d2, s2, u3, t3, d3, s3)
#define XTT_EMPTY_MISC(n, w, has, sm)
#define XTT_EMPTY_HIDDEN(v, m, l)
#define XTT_EMPTY_REFDESC(s, d, w)

/* -------------------- Blackhole -------------------- */

namespace bh_data {

static const addr_mod_slot addr_mod_slots[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT(p, sr, sv, dr, dv, fr, fv) \
  { p, sr, sv, dr, dv, fr, fv },
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const setc16_program ref_addr_mod_setc16[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16(r, v) { r, v },
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const seq_program seq_programs[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM(n, m, w, ne, u0, t0, d0, s0, u1, t1, d1, \
			      s1, u2, t2, d2, s2, u3, t3, d3, s3)	\
  { n, m, ne, { { u0, t0, d0, s0 }, { u1, t1, d1, s1 },			\
		{ u2, t2, d2, s2 }, { u3, t3, d3, s3 } }, w },
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const misc_word_entry misc_words[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD(n, w, has, sm) { n, w, has, sm },
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const hidden_write_entry hidden_writes[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE(v, m, l) { v, m, l },
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const ref_descriptor_word ref_descriptors[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR(s, d, w) { s, d, w },
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const caps table = {
#define XTT_MACRO_CAPS_SCALARS(name, nt, ns, db, lop, lis, ms, ams,	\
			       amb, ab, ni, ai, nb, s16, cfg, own,	\
			       drain, brk)				\
  CPU_BH, name, nt, ns, db, lop, lis, ms, ams, amb, ab, ni, ai, nb,	\
  addr_mod_slots,							\
  sizeof (addr_mod_slots) / sizeof (addr_mod_slots[0]),		\
  s16, cfg, own,							\
  seq_programs, sizeof (seq_programs) / sizeof (seq_programs[0]),	\
  misc_words, sizeof (misc_words) / sizeof (misc_words[0]),		\
  hidden_writes, sizeof (hidden_writes) / sizeof (hidden_writes[0]),	\
  ref_descriptors,							\
  sizeof (ref_descriptors) / sizeof (ref_descriptors[0]),		\
  ref_addr_mod_setc16,							\
  sizeof (ref_addr_mod_setc16) / sizeof (ref_addr_mod_setc16[0]),	\
  drain, brk,
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-bh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

}  /* namespace bh_data */

/* -------------------- Wormhole -------------------- */

namespace wh_data {

static const addr_mod_slot addr_mod_slots[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT(p, sr, sv, dr, dv, fr, fv) \
  { p, sr, sv, dr, dv, fr, fv },
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const setc16_program ref_addr_mod_setc16[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16(r, v) { r, v },
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const seq_program seq_programs[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM(n, m, w, ne, u0, t0, d0, s0, u1, t1, d1, \
			      s1, u2, t2, d2, s2, u3, t3, d3, s3)	\
  { n, m, ne, { { u0, t0, d0, s0 }, { u1, t1, d1, s1 },			\
		{ u2, t2, d2, s2 }, { u3, t3, d3, s3 } }, w },
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const misc_word_entry misc_words[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD(n, w, has, sm) { n, w, has, sm },
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const hidden_write_entry hidden_writes[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE(v, m, l) { v, m, l },
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const ref_descriptor_word ref_descriptors[] = {
#define XTT_MACRO_CAPS_SCALARS XTT_EMPTY_SCALARS
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR(s, d, w) { s, d, w },
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

static const caps table = {
#define XTT_MACRO_CAPS_SCALARS(name, nt, ns, db, lop, lis, ms, ams,	\
			       amb, ab, ni, ai, nb, s16, cfg, own,	\
			       drain, brk)				\
  CPU_WH, name, nt, ns, db, lop, lis, ms, ams, amb, ab, ni, ai, nb,	\
  addr_mod_slots,							\
  sizeof (addr_mod_slots) / sizeof (addr_mod_slots[0]),		\
  s16, cfg, own,							\
  seq_programs, sizeof (seq_programs) / sizeof (seq_programs[0]),	\
  misc_words, sizeof (misc_words) / sizeof (misc_words[0]),		\
  hidden_writes, sizeof (hidden_writes) / sizeof (hidden_writes[0]),	\
  ref_descriptors,							\
  sizeof (ref_descriptors) / sizeof (ref_descriptors[0]),		\
  ref_addr_mod_setc16,							\
  sizeof (ref_addr_mod_setc16) / sizeof (ref_addr_mod_setc16[0]),	\
  drain, brk,
#define XTT_MACRO_ADDR_MOD_SLOT XTT_EMPTY_SLOT
#define XTT_MACRO_REF_ADDR_MOD_SETC16 XTT_EMPTY_REFSETC16
#define XTT_MACRO_SEQ_PROGRAM XTT_EMPTY_SEQ
#define XTT_MACRO_MISC_WORD XTT_EMPTY_MISC
#define XTT_MACRO_HIDDEN_WRITE XTT_EMPTY_HIDDEN
#define XTT_MACRO_REF_DESCRIPTOR XTT_EMPTY_REFDESC
#include "rvtt-macro-tables-wh.def"
#undef XTT_MACRO_CAPS_SCALARS
#undef XTT_MACRO_ADDR_MOD_SLOT
#undef XTT_MACRO_REF_ADDR_MOD_SETC16
#undef XTT_MACRO_SEQ_PROGRAM
#undef XTT_MACRO_MISC_WORD
#undef XTT_MACRO_HIDDEN_WRITE
#undef XTT_MACRO_REF_DESCRIPTOR
};

}  /* namespace wh_data */

/* ------------------------------------------------------------------ */
/* Lookup.  QSR is INTENTIONALLY ABSENT: no capability table exists    */
/* because its seq_id / split-VD / done macro encoding is unproven     */
/* (SFPLOADMACRO_FORMATION.md "Architecture boundary").	       */
/* ------------------------------------------------------------------ */

const caps *
rvtt_macro_caps_for_cpu (cpu_t cpu)
{
  switch (cpu)
    {
    case CPU_WH:
      return &wh_data::table;
    case CPU_BH:
      return &bh_data::table;
    case CPU_QSR:
    default:
      return nullptr;
    }
}

/* Return the stable refusal-vocabulary name explaining why CPU has no
   macro capability table ("target-macro-encoding-unproven"), or null
   when the CPU does have one.  */

const char *
rvtt_macro_caps_refusal (cpu_t cpu)
{
  return rvtt_macro_caps_for_cpu (cpu) ? nullptr
    : refusal_target_macro_encoding_unproven;
}

/* ------------------------------------------------------------------ */
/* Encoders / decoders.						      */
/* ------------------------------------------------------------------ */

bool
dst_address_encodable (const caps *c, unsigned address)
{
  if (!c)
    return false;
  /* Odd rows alias the macro VD-high encoding bit; rows above 1023
     exceed the ten-bit address field (SFPLOADMACRO_FORMATION.md "First
     opt-in executable slice"; rtl-rvtt-loadmacro.cc:743-751).  */
  return (address & 1u) == 0 && address < (1u << c->address_bits);
}

/* Pack a macro-launch instruction word for capability set C into
   *WORD: MACRO_INDEX/VD combine into the lreg_ind field, with MODE,
   ADDR_MODE and the Dst row ADDRESS in their per-target positions.
   Returns false (refusing) when any field is out of range or the
   address is not launch-encodable.  */

bool
encode_launch (const caps *c, unsigned macro_index, unsigned vd,
	       unsigned mode, unsigned addr_mode, unsigned address,
	       uint32_t *word)
{
  if (!c || macro_index > 3 || vd > 3 || mode > 0xf
      || addr_mode >= (1u << c->addr_mode_bits)
      || !dst_address_encodable (c, address))
    return false;
  unsigned lreg_ind = (macro_index << 2) | vd;
  *word = ((uint32_t) c->launch_opcode << 24)
    | (lreg_ind << c->lreg_ind_shift)
    | (mode << c->mod0_shift)
    | (addr_mode << c->addr_mode_shift)
    | address;
  return true;
}

/* Inverse of encode_launch: unpack WORD's fields into *MACRO_INDEX,
   *VD, *MODE, *ADDR_MODE and *ADDRESS.  Returns false unless WORD
   carries C's launch opcode and re-encoding the decoded fields
   reproduces WORD bit-exactly (stray bits refuse).  */

bool
decode_launch (const caps *c, uint32_t word, unsigned *macro_index,
	       unsigned *vd, unsigned *mode, unsigned *addr_mode,
	       unsigned *address)
{
  if (!c || (word >> 24) != c->launch_opcode)
    return false;
  unsigned lreg_ind = (word >> c->lreg_ind_shift) & 0xf;
  *macro_index = lreg_ind >> 2;
  *vd = lreg_ind & 3;
  *mode = (word >> c->mod0_shift) & 0xf;
  *addr_mode = (word >> c->addr_mode_shift)
    & ((1u << c->addr_mode_bits) - 1);
  *address = word & ((1u << c->address_bits) - 1);
  /* Reject stray bits between the address field and the addr-mode
     field (BH bits 12:10; WH bits 13:10).  */
  uint32_t rebuilt;
  if (!encode_launch (c, *macro_index, *vd, *mode, *addr_mode, *address,
		      &rebuilt))
    return false;
  return rebuilt == word;
}

/* Pack a SETC16 word (opcode << 24 | CONFIG_REG << 16 | VALUE); the
   field layout is identical on WH and BH.  Out-of-range fields refuse
   rather than alias under masking.  */

bool
encode_setc16 (const caps *c, unsigned config_reg, unsigned value,
	       uint32_t *word)
{
  if (!c || config_reg > 0xff || value > 0xffff)
    return false;
  *word = ((uint32_t) c->setc16_opcode << 24) | (config_reg << 16) | value;
  return true;
}

/* Unpack SETC16 WORD into its configuration-register number and 16-bit
   value.  Returns false when WORD's opcode byte is not C's SETC16
   opcode.  */

bool
decode_setc16 (const caps *c, uint32_t word, unsigned *config_reg,
	       unsigned *value)
{
  if (!c || (word >> 24) != c->setc16_opcode)
    return false;
  *config_reg = (word >> 16) & 0xff;
  *value = word & 0xffff;
  return true;
}

/* Pack an SFPCONFIG instruction word (opcode << 24 | IMM16 << 8
   | DEST << 4 | MOD1).  Reference encoder: fields are masked to their
   widths, not range-checked.  */

uint32_t
encode_sfpconfig (const caps *c, unsigned imm16, unsigned dest,
		  unsigned mod1)
{
  return ((uint32_t) c->sfpconfig_opcode << 24) | ((imm16 & 0xffff) << 8)
    | ((dest & 0xf) << 4) | (mod1 & 0xf);
}

/* Expand into OUT the owned SETC16 program (three register writes per
   capability slot: Src, Dst, fidelity) that establishes the
   Dst += DST_DELTA auto-increment address modifier from any incoming
   state; *N_OUT counts the entries written (OUT must hold at least 6).
   *NEEDS_BANK_BASE_OWNERSHIP reports whether the program's slot naming
   stands on the Base=1 SFPU platform contract.  */

bool
addr_mod_program (const caps *c, int dst_delta, setc16_program *out,
		  unsigned *n_out, bool *needs_bank_base_ownership)
{
  if (!c)
    return false;
  /* Only the exact Dst += 2 auto-increment program is proven
     (rtl-rvtt-loadmacro.cc:810-845).  Other deltas refuse until an
     architectural reference maps delta -> slot register values.  */
  if (dst_delta != 2)
    return false;
  unsigned n = 0;
  for (unsigned i = 0; i < c->n_addr_mod_slots; ++i)
    {
      const addr_mod_slot &s = c->addr_mod_slots[i];
      out[n].config_reg = s.src_reg;
      out[n++].value = s.src_val;
      out[n].config_reg = s.dst_reg;
      out[n++].value = s.dst_val;
      out[n].config_reg = s.fid_reg;
      out[n++].value = s.fid_val;
    }
  *n_out = n;
  *needs_bank_base_ownership = c->needs_bank_base_ownership;
  return true;
}

/* Pack instruction template T into *WORD (opcode << 24 | imm12 << 12
   | src_c << 8 | dest_sel << 4 | mod1).  Refuses out-of-range fields
   and destination selectors outside physical L0-L7 and the
   InstructionTemplate selector range.  */

bool
encode_template (const caps *c, const template_spec &t, uint32_t *word)
{
  if (!c)
    return false;
  if (t.imm12 > 0xfff || t.src_c > 0xf || t.mod1 > 0xf)
    return false;
  /* Destination is a physical LREG 0-7 or an InstructionTemplate
     destination selector: SFPCONFIG.md / SFPLOADMACRO.md name
     InstructionTemplate[i] through VD = 12 + i (the production
     handwritten inits write templates via that VD; the frozen shapes
     exercised 0xc/0xd, later widened to the full architectural
     0xc..0xf).  */
  if (t.dest_sel > 7 && (t.dest_sel < 0xc || t.dest_sel > 0xf))
    return false;
  /* SFP_STOCH_RND (0x8e) has a different field layout above bit 12
     (rnd_mode << 21 | imm8 << 16 | lreg_src_b << 12); the generic imm12
     packing is only valid for it when those fields are all zero.  */
  if (t.opcode == 0x8e && t.imm12 != 0)
    return false;
  *word = ((uint32_t) t.opcode << 24)
    | ((uint32_t) t.imm12 << 12)
    | ((uint32_t) t.src_c << 8)
    | ((uint32_t) t.dest_sel << 4)
    | t.mod1;
  return true;
}

/* Unpack template WORD into *OUT over the generic
   opcode/imm12/src_c/dest_sel/mod1 layout.  Purely mechanical field
   extraction; performs no validation and always succeeds.  */

bool
decode_template (uint32_t word, template_spec *out)
{
  out->opcode = word >> 24;
  out->imm12 = (word >> 12) & 0xfff;
  out->src_c = (word >> 8) & 0xf;
  out->dest_sel = (word >> 4) & 0xf;
  out->mod1 = word & 0xf;
  return true;
}

/* Find the proven sequence word for MACRO_INDEX realizing
   EVENTS[0..N_EVENTS): a whole-word lookup over C's validated sequence
   programs, never a bit-level synthesis.  Returns the word in *WORD, or
   false when no proven program matches.  */

bool
encode_sequence (const caps *c, unsigned macro_index,
		 const seq_event *events, unsigned n_events,
		 uint32_t *word)
{
  if (!c || n_events > 4)
    return false;
  for (unsigned i = 0; i < c->n_seq_programs; ++i)
    {
      const seq_program &p = c->seq_programs[i];
      if (p.macro_index != macro_index || p.n_events != n_events)
	continue;
      bool match = true;
      for (unsigned e = 0; e < n_events && match; ++e)
	{
	  const seq_event &want = events[e];
	  const seq_event &have = p.events[e];
	  if (want.unit != have.unit
	      || want.template_index != have.template_index
	      || want.is_store != have.is_store)
	    match = false;
	  /* Documented delays must match; table DELAY_UNKNOWN entries
	     accept any requested delay (the whole word is the proven
	     capability either way).  */
	  else if (have.delay != DELAY_UNKNOWN && want.delay != DELAY_UNKNOWN
		   && want.delay != have.delay)
	    match = false;
	}
      if (match)
	{
	  *word = p.word;
	  return true;
	}
    }
  return false;
}

/* Encode the select-shape misc word for a proven STORE_MOD0 store data
   format -- the only field-parameterized misc rule; the other proven
   misc words are exposed as whole-word reference data.  */

bool
encode_misc_select (const caps *c, unsigned store_mod0, uint32_t *word)
{
  if (!c || store_mod0 > 0xf)
    return false;
  /* rtl-rvtt-loadmacro.cc:1572: misc = 0x700 | StoreMod0 (select shape;
     bits 10:8 remain a hypothesis: per-macro delay-mode bits).  */
  *word = 0x700u | store_mod0;
  return true;
}

/* Return the deferred-CC visibility lag in issue slots: a CC result
   computed by a retiring macro event becomes architecturally visible
   only to instructions issued at least this many slots after the event
   executed.  */

unsigned
cc_visibility_lag ()
{
  /* The reference simulator's retirement model: "CC results computed
     by a retiring event become architecturally visible to instructions
     issued in the cycle after the event executed, matching the
     hardware's flag-forwarding latency."  The frozen select calendar
     relied on exactly this lag (the payload load issued one slot after
     the SETCC launch still reads the ambient all-lanes state).  */
  return 1;
}

/* Architectural fact: a scheduled macro store's lane predicate is the
   LIVE CC state at the store's execution cycle, never a launch-latched
   copy.  A CC write retiring in the store's own cycle is not yet
   visible to it; one retiring in any earlier cycle is.  */

bool
store_lane_mask_live_at_execution ()
{
  /* SFPLOADMACRO.md StoreSubUnit extras latch only Addr, the Mod0
     source, and the backdoor-load bit; the lane predicate keeps
     SFPSTORE's live evaluation at execution (a same-cycle CC retire is
     not yet visible; an earlier-cycle one is).  Proven on Blackhole
     hardware and modeled by the corrected reference simulator (the
     store event reads cc/cc_en from its retirement group's pre-write
     snapshot).  The prior launch-latched reading
     (store_lane_mask_latched_at_launch) was a simulator-only model
     fact the hardware adjudication invalidated.  */
  return true;
}

/* Store in *OUT the SFPSETCC instr_mod1 that encodes the complement of
   MOD1's lane test.  Only the plain register-test class {0, 2, 4, 6}
   has an architecturally defined complement (toggling the complement
   bit); immediate-operand and force-false forms refuse.  */

bool
sfpsetcc_complement_mod1 (uint64_t mod1, unsigned *out)
{
  /* SFPSETCC.md instr_mod1: bit 0 = immediate-operand form, bit 1 =
     !=0 test (else sign test), bit 2 = complement of the register
     test, bit 3 = force false.  The complement is defined only for the
     plain register-test class {0, 2, 4, 6}; every other form refuses
     (the proven select instance is 2 -> 6, LM:1568 "SETCC loaded value
     == zero" realizing the source's !=0 predicate on the
     complementary payload).  */
  if (mod1 > 6 || (mod1 & 1))
    return false;
  *out = (unsigned) (mod1 ^ 4);
  return true;
}

/* Return the hidden physical-LREG writes recorded for template WORD as
   a bitmask over LREG indices (bit N = LN, N in 0..15), or 0 when no
   proven hidden-write entry matches (WORD & mask) == value in C's
   table.  */

uint32_t
template_hidden_lreg_writes (const caps *c, uint32_t word)
{
  if (!c)
    return 0;
  for (unsigned i = 0; i < c->n_hidden_writes; ++i)
    if ((word & c->hidden_writes[i].mask) == c->hidden_writes[i].value)
      return c->hidden_writes[i].lreg_write_mask;
  return 0;
}

/* ------------------------------------------------------------------ */
/* Fixed architectural words.					      */
/* ------------------------------------------------------------------ */

bool
sfpencc_encode (uint64_t imm12, uint64_t mod1, uint32_t *word)
{
  /* TT_OP_{WH,BH}_SFPENCC (0x8a): imm12 << 12 | lreg_c << 8
     | lreg_dest << 4 | mod1; the typed rvtt_sfpencc pattern has no
     register operands, so both lreg fields encode as zero.
     Out-of-range fields refuse rather than alias under masking.  */
  if (imm12 > 0xfff || mod1 > 0xf)
    return false;
  *word = (0x8au << 24) | ((uint32_t) imm12 << 12) | (uint32_t) mod1;
  return true;
}

/* Return the fixed SFPENCC word that enables all lanes and sets all
   results from the immediate, derived through sfpencc_encode so the
   constant can never drift from the encoder.  */

uint32_t
sfpencc_all_lanes_word ()
{
  /* imm12 = SFPENCC_IMM12_BOTH (3): set both lane-enable and result;
     mod1 = SFPENCC_MOD1_EI_RI (10): take both from the immediate
     (rvtt-protos.h SFPENCC_* constants).  */
  uint32_t word = 0;
  bool ok = sfpencc_encode (3, 10, &word);
  return ok ? word : 0;		/* 0x8a00300a */
}

/* Return the fixed SETRWC word for one CR-mode Dst += 8 counter step;
   a Dst face advance issues it dst_face_advance_step_count times.  */

uint32_t
dst_step8_setrwc_word ()
{
  /* TT_OP_{WH,BH}_SETRWC (0x37): clear_ab_vld << 22 | rwc_cr << 18
     | rwc_d << 14 | rwc_b << 10 | rwc_a << 6 | BitMask, with
     (0, CR=4, D=8, B=0, A=0, mask=4): the CR-mode Dst += 8 counter step.
     Two of these advance one face (the typed rvtt_ttdstface insn);
     the same value was the frozen pass's magic word
     (rtl-rvtt-loadmacro.cc:161, deleted when the typed insn replaced
     it).  */
  return (0x37u << 24) | (4u << 18) | (8u << 14) | 4u;	/* 0x37120004 */
}

/* Return how many dst_step8_setrwc_word issues make up one
   architectural Dst face advance (two CR-mode Dst += 8 steps).  */

unsigned
dst_face_advance_step_count ()
{
  return 2;
}

/* Return the instruction word of the one explicit Dst increment a
   formed launch may absorb into its auto-increment address mode:
   TTINCRWC with Dst += 2 and every other leg zero.  */

uint32_t
absorbed_dst_increment_word ()
{
  /* TT_OP_{WH,BH}_INCRWC (0x38): rwc_cr << 18 | rwc_d << 14
     | rwc_b << 10 | rwc_a << 6, with the exact absorbed increment
     TTINCRWC (0, 2, 0, 0) (rtl-rvtt-loadmacro.cc:1401-1418).  */
  return (0x38u << 24) | (2u << 14);	/* 0x38008000 */
}

/* Decode SETRWC instruction WORD into its counter-control fields *F.
   The single home of the SETRWC field layout: dst_step8_setrwc_word
   round-trips through it.  Returns false for any other opcode byte;
   field semantics are documented at setrwc_fields in
   rvtt-macro-tables.h.  */

bool
setrwc_decode (uint32_t word, setrwc_fields *f)
{
  /* TT_OP_{WH,BH}_SETRWC (0x37): clear_ab_vld << 22 | rwc_cr << 18
     | rwc_d << 14 | rwc_b << 10 | rwc_a << 6 | bit_mask -- the same
     layout dst_step8_setrwc_word () above encodes with.  Any other
     opcode byte refuses.  */
  if ((word >> 24) != 0x37u)
    return false;
  f->clear_ab_vld = (word >> 22) & 0x3;
  f->rwc_cr = (word >> 18) & 0xf;
  f->rwc_d = (word >> 14) & 0xf;
  f->rwc_b = (word >> 10) & 0xf;
  f->rwc_a = (word >> 6) & 0xf;
  f->bit_mask = word & 0x3f;
  return true;
}

/* ------------------------------------------------------------------ */
/* Derived-calendar architectural facts.			      */
/* Provenance for everything in this section:			      */
/*  (S1) ISA SFPLOADMACRO.md (BlackholeA0 documentation) -- the       */
/*       SequenceBits format, the sub-unit legality table, the	      */
/*       (dag)/(ddag) rules, the Misc field layout;			      */
/*  (S2) the pinned reference simulator's SFPLOADMACRO event model    */
/*       and executor -- the executable model the bit-exactness       */
/*       gates run against (ready = issue + 1 + delay,		      */
/*       retire-before-issue, build_dispatch routing classes);	      */
/*  (S3) tt_llk_blackhole ckernel_sfpu_mul_int.h _init_mul_int_ --    */
/*       the production handwritten descriptor with author-annotated  */
/*       field meanings.					      */
/* See the derivation notes in rvtt-macro-derive-core.h (including the	      */
/* byte-exact re-derivation of every frozen calendar word above).     */
/* ------------------------------------------------------------------ */

bool
encode_sequence_bits (unsigned case_kind, unsigned delay, bool vd16,
		      bool route_vb, uint8_t *out)
{
  /* Case 1 is architecturally undefined; template indices stop at 3;
     the delay field is three bits.  */
  if (case_kind == 1 || case_kind > 7 || delay > SEQ_MAX_DELAY)
    return false;
  *out = (uint8_t) (case_kind | (delay << 3) | (vd16 ? 0x40u : 0)
		    | (route_vb ? 0x80u : 0));
  return true;
}

/* Field extraction of one sequence byte -- the exact inverse of
   encode_sequence_bits, kept beside it so the SequenceBits layout has
   ONE home.  Returns false for case 1 (architecturally undefined).  */

bool
decode_sequence_bits (uint8_t byte, unsigned *case_kind, unsigned *delay,
		      bool *vd16, bool *route_vb)
{
  *case_kind = byte & 7;
  *delay = (byte >> 3) & 7;
  *vd16 = (byte & 0x40u) != 0;
  *route_vb = (byte & 0x80u) != 0;
  return *case_kind != 1;
}

/* Assemble a sequence word from its four per-sub-unit BYTES: byte I,
   stored little-endian in the word, programs sub-unit I (Simple, MAD,
   Round, Store).  */

uint32_t
compose_sequence_word (const uint8_t bytes[4])
{
  return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8)
    | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
}

/* Split sequence WORD into its four per-sub-unit BYTES -- the exact
   inverse of compose_sequence_word.  */

void
decompose_sequence_word (uint32_t word, uint8_t bytes[4])
{
  for (unsigned i = 0; i < 4; ++i)
    bytes[i] = (uint8_t) (word >> (8 * i));
}

/* The normative per-sub-unit opcode legality table (S1), transcribed
   for the WH/BH-common opcode bytes of sfpu-ops-{wh,bh}.h (the two
   files assign identical bytes to every instruction below; SFPMUL24
   is BH-only, S2/S3).  Raw opcode bytes are capability-table data by
   design.  Omitted opcodes have no proven placement and refuse.  */

struct subunit_legal_entry
{
  uint8_t opcode;
  uint8_t mask;		/* bit N = executable on sub-unit byte N      */
};

static const subunit_legal_entry subunit_legality[] = {
  /* Simple column (S1).  */
  { 0x7d, 1u << SEQ_UNIT_SIMPLE },	/* SFPABS	*/
  { 0x7e, 1u << SEQ_UNIT_SIMPLE },	/* SFPAND	*/
  { 0x90, 1u << SEQ_UNIT_SIMPLE },	/* SFPCAST	*/
  { 0x8b, 1u << SEQ_UNIT_SIMPLE },	/* SFPCOMPC	*/
  { 0x91, 1u << SEQ_UNIT_SIMPLE },	/* SFPCONFIG	*/
  { 0x76, 1u << SEQ_UNIT_SIMPLE },	/* SFPDIVP2	*/
  { 0x8a, 1u << SEQ_UNIT_SIMPLE },	/* SFPENCC	*/
  { 0x77, 1u << SEQ_UNIT_SIMPLE },	/* SFPEXEXP	*/
  { 0x78, 1u << SEQ_UNIT_SIMPLE },	/* SFPEXMAN	*/
  { 0x79, 1u << SEQ_UNIT_SIMPLE },	/* SFPIADD	*/
  { 0x81, 1u << SEQ_UNIT_SIMPLE },	/* SFPLZ	*/
  { 0x7c, 1u << SEQ_UNIT_SIMPLE },	/* SFPMOV	*/
  { 0x80, 1u << SEQ_UNIT_SIMPLE },	/* SFPNOT	*/
  { 0x7f, 1u << SEQ_UNIT_SIMPLE },	/* SFPOR	*/
  { 0x7b, 1u << SEQ_UNIT_SIMPLE },	/* SFPSETCC	*/
  { 0x82, 1u << SEQ_UNIT_SIMPLE },	/* SFPSETEXP	*/
  { 0x83, 1u << SEQ_UNIT_SIMPLE },	/* SFPSETMAN	*/
  { 0x89, 1u << SEQ_UNIT_SIMPLE },	/* SFPSETSGN	*/
  { 0x7a, 1u << SEQ_UNIT_SIMPLE },	/* SFPSHFT	*/
  { 0x92, 1u << SEQ_UNIT_SIMPLE },	/* SFPSWAP (ddag)	*/
  { 0x8c, 1u << SEQ_UNIT_SIMPLE },	/* SFPTRANSP	*/
  { 0x8d, 1u << SEQ_UNIT_SIMPLE },	/* SFPXOR	*/
  /* MAD column (S1; SFPMUL24 by S2/S3).  */
  { 0x85, 1u << SEQ_UNIT_MAD },		/* SFPADD	*/
  { 0x75, 1u << SEQ_UNIT_MAD },		/* SFPADDI	*/
  { 0x73, 1u << SEQ_UNIT_MAD },		/* SFPLUT	*/
  { 0x95, 1u << SEQ_UNIT_MAD },		/* SFPLUTFP32	*/
  { 0x84, 1u << SEQ_UNIT_MAD },		/* SFPMAD	*/
  { 0x86, 1u << SEQ_UNIT_MAD },		/* SFPMUL	*/
  { 0x74, 1u << SEQ_UNIT_MAD },		/* SFPMULI	*/
  { 0x98, 1u << SEQ_UNIT_MAD },		/* SFPMUL24 (BH) */
  /* Round column (S1).  */
  { 0x94, 1u << SEQ_UNIT_ROUND },	/* SFPSHFT2	*/
  { 0x8e, 1u << SEQ_UNIT_ROUND },	/* SFPSTOCHRND	*/
  /* SFPNOP may schedule on any compute sub-unit (S1).  */
  { 0x8f, (1u << SEQ_UNIT_SIMPLE) | (1u << SEQ_UNIT_MAD)
	  | (1u << SEQ_UNIT_ROUND) },
  /* Store column (S1); realized as case 3, never as a template.  */
  { 0x72, 1u << SEQ_UNIT_STORE },	/* SFPSTORE	*/
};

/* Return the sub-units that can execute OPCODE as a bitmask with bit N
   = sequence byte N (SEQ_UNIT_SIMPLE .. SEQ_UNIT_STORE), or 0 when the
   opcode has no entry on record (the refusing default).  */

unsigned
subunit_legal_mask (const caps *c, uint8_t opcode)
{
  if (!c)
    return 0;
  for (const subunit_legal_entry &e : subunit_legality)
    if (e.opcode == opcode)
      return e.mask;
  return 0;
}

/* Return the result latency, in issue slots, of a value produced on
   SEQ_UNIT (a SEQ_UNIT_* index): the earliest a consuming event may
   execute after its producer.  */

unsigned
subunit_result_latency (unsigned seq_unit)
{
  /* Simple/Round: every proven chain steps one slot per dependence
     (signbit shift->cast->store, cast-round cast->rnd->store).  MAD:
     the handwritten MulInt32 places its store two slots after the
     MUL24 in BOTH calendar variants (store delay = mad delay + 2),
     the multiply pipeline's writeback distance (S3).  */
  return seq_unit == SEQ_UNIT_MAD ? 2 : 1;
}

/* Architectural operand-routing class of value OPCODE under
   capability set C: which source fields (VB/VC) the sequencer's
   dispatch reroutes for that opcode, RC_NONE when unrouted or C is
   null.  The ranges mirror the simulator's dispatch table.  */

route_class
opcode_route_class (const caps *c, uint8_t opcode)
{
  /* S1 field-override rules; S2 build_dispatch ranges.  */
  if (!c)
    return RC_NONE;
  if (opcode == 0x94)
    return RC_SHFT2;
  if ((opcode >= 0x84 && opcode <= 0x86) || opcode == 0x8e
      || opcode == 0x98)
    return RC_VB_VC;
  if ((opcode >= 0x79 && opcode <= 0x83) || opcode == 0x89
      || opcode == 0x90 || opcode == 0x97 || opcode == 0x99)
    return RC_VC;
  return RC_NONE;
}

/* Return whether OPCODE reads its VD operand (today only SFPSWAP), in
   which case the event can be neither redirected to LReg16 nor have its
   planned VC routing overridden.  */

bool
opcode_reads_vd (const caps *c, uint8_t opcode)
{
  /* SFPSWAP's VD operand is an INPUT (SFPSWAP.md; S2 executor): the
     event cannot be redirected to LReg16 and route must stay 1 so a
     planned VC survives.  */
  return c && opcode == 0x92;
}

/* Return whether OPCODE may be realized with its result redirected to
   the LReg16 staging register.  Such an event has no encodable VD and
   executes through the simulator's direct template evaluator, which is
   proven only for the opcode set below; every other opcode keeps the
   VD-direct or staging-copy realization or refuses.  */

bool
opcode_l16_target_proven (const caps *c, uint8_t opcode)
{
  /* The ORACLE-PROVEN LReg16-target evaluator set.  An event
     redirected to the LReg16 staging
     register has no encodable VD, so it executes through the direct
     template evaluator -- and that path is proven ONLY for the opcode
     set the reviewed oracle implements (the reference simulator's
     direct template evaluator).  The first shape outside the
     set to reach formation -- an SFPABS (0x7d) row
     admitted by the entry-ambient derivation -- was adjudicated WRONG
     on Blackhole hardware (device correctness FAIL)
     and refused by the oracle (UnsupportedFunctionality), so the old
     assumption that any simple-unit template can stage via LReg16 is
     architecturally false.  Opcodes outside this set keep the VD-direct
     or staging-copy realizations (rewritten-word execution, full
     opcode support) or refuse by name.  */
  if (!c)
    return false;
  switch (opcode)
    {
    case 0x79:	/* SFPIADD	*/
    case 0x7e:	/* SFPAND	*/
    case 0x7f:	/* SFPOR	*/
    case 0x80:	/* SFPNOT	*/
    case 0x84:	/* SFPMAD	*/
    case 0x85:	/* SFPADD	*/
    case 0x86:	/* SFPMUL	*/
    case 0x89:	/* SFPSETSGN	*/
    case 0x8e:	/* SFPSTOCHRND	*/
    case 0x90:	/* SFPCAST	*/
    case 0x94:	/* SFPSHFT2	*/
    case 0x98:	/* SFPMUL24	*/
    case 0x99:	/* SFPARECIP	*/
      return true;
    default:
      return false;
    }
}

/* Return whether OPCODE is subject to the SFPSWAP adjacency rule: the
   MAD sub-unit must host nothing in the swap's execution cycle, and
   Simple and Round must host nothing in the cycle after it.  */

bool
opcode_needs_swap_adjacency (const caps *c, uint8_t opcode)
{
  /* S1 (ddag): SWAP on Simple needs MAD idle in its execution cycle and
     Simple+Round idle (or NOP) in the next.  The frozen minmax copy
     delay (3, not the dependence-minimal 2) is this rule in action.  */
  return c && opcode == 0x92;
}

/* Fill *OUT with the one proven realization of a staging copy into the
   transient LReg16 (opcode, mod1, and hosting sub-unit).  Returns false
   only when C has no capability table.  */

bool
staging_copy_realization (const caps *c, staging_copy_facts *out)
{
  if (!c)
    return false;
  /* The frozen minmax transient copy (ref descriptor dest1 0x940000d6):
     SFPSHFT2 immediate-shift-0 = a plain copy of the launch VD (the
     SHFT_IMM VB<-VD override, S1), Round sub-unit (SFPSHFT2's only
     legal placement), writing LReg16.  The one proven staging form;
     SFPMOV-on-Simple would be plausible but is unproven and refuses.  */
  out->opcode = 0x94;
  out->mod1 = 6;		/* SHFT_IMM (sfpu-ops SFPSHFT2 mod1 6) */
  out->seq_unit = SEQ_UNIT_ROUND;
  return true;
}

/* Pack the macro misc configuration word from its three four-bit
   fields: STORE_MOD0 (store data format, bits 3:0), the per-macro
   USES_LOAD_MOD0_MASK (bits 7:4), and the per-sub-unit DELAY_KIND_MASK
   (bits 11:8, instruction-counted rather than cycle-counted delays).  */

uint32_t
encode_misc_fields (unsigned store_mod0, unsigned uses_load_mod0_mask,
		    unsigned delay_kind_mask)
{
  return (store_mod0 & 0xfu) | ((uses_load_mod0_mask & 0xfu) << 4)
    | ((delay_kind_mask & 0xfu) << 8);
}

/* Unpack misc WORD into its three four-bit fields -- the exact inverse
   of encode_misc_fields.  */

void
decode_misc_fields (uint32_t word, unsigned *store_mod0,
		    unsigned *uses_load_mod0_mask,
		    unsigned *delay_kind_mask)
{
  *store_mod0 = word & 0xfu;
  *uses_load_mod0_mask = (word >> 4) & 0xfu;
  *delay_kind_mask = (word >> 8) & 0xfu;
}

/* Return whether a DERIVED calendar may absorb the row's Dst stride
   into a launch's auto-increment address mode on C's CPU.  True for BH
   and WH, where the derived absorbed-stride calendar is simulator
   bit-exact through the owned single-slot SETC16 program; CPUs without
   a capability table refuse.  */

bool
derived_stride_absorption_proven (const caps *c)
{
  /* BH: the derived unary max/min calendar (absorbed stride through
     the owned single-slot SETC16 program, launch auto-increment mode)
     is bit-exact against the reference simulator through the generic
     path.  WH: the former refusal's grounding failure (hardware trace:
     position-shuffled tiles after the first, every latched launch
     dst_row/mask correct) was adjudicated as the dual-slot SETC16
     program clobbering LLK's live base-0 ADDR_MOD_2 and corrupting the
     NEXT tile's datacopy -- the machinery was wrong, not unproven.
     With this table's corrected single-slot Base=1 program the derived
     absorbed-stride calendar is bit-exact on the faithful WH reference
     simulator (derived unary max/min multi-tile).  QSR has no
     capability entry and refuses.  */
  return c && (c->cpu == CPU_BH || c->cpu == CPU_WH);
}

}  /* namespace rvtt_macro */
