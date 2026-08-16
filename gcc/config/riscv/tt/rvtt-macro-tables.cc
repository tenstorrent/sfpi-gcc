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
   build (see rvtt-macro-tables-test.cc).  Provenance for every raw word:
   NOTES-wp6-prep.md in this directory.  */

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

bool
encode_setc16 (const caps *c, unsigned config_reg, unsigned value,
	       uint32_t *word)
{
  if (!c || config_reg > 0xff || value > 0xffff)
    return false;
  *word = ((uint32_t) c->setc16_opcode << 24) | (config_reg << 16) | value;
  return true;
}

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

uint32_t
encode_sfpconfig (const caps *c, unsigned imm16, unsigned dest,
		  unsigned mod1)
{
  return ((uint32_t) c->sfpconfig_opcode << 24) | ((imm16 & 0xffff) << 8)
    | ((dest & 0xf) << 4) | (mod1 & 0xf);
}

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

bool
encode_template (const caps *c, const template_spec &t, uint32_t *word)
{
  if (!c)
    return false;
  if (t.imm12 > 0xfff || t.src_c > 0xf || t.mod1 > 0xf)
    return false;
  /* Destination is a physical LREG 0-7 or a proven routing selector.  */
  if (t.dest_sel > 7 && t.dest_sel != 0xc && t.dest_sel != 0xd)
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

bool
encode_misc_select (const caps *c, unsigned store_mod0, uint32_t *word)
{
  if (!c || store_mod0 > 0xf)
    return false;
  /* rtl-rvtt-loadmacro.cc:1572: misc = 0x700 | StoreMod0 (select shape;
     bits 10:8 hypothesis: per-macro delay-mode bits, see
     NOTES-wp6-prep.md).  */
  *word = 0x700u | store_mod0;
  return true;
}

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

uint32_t
dst_step8_setrwc_word ()
{
  /* TT_OP_{WH,BH}_SETRWC (0x37): clear_ab_vld << 22 | rwc_cr << 18
     | rwc_d << 14 | rwc_b << 10 | rwc_a << 6 | BitMask, with
     (0, CR=4, D=8, B=0, A=0, mask=4): the CR-mode Dst += 8 counter step.
     Two of these advance one face (typed rvtt_ttdstface, WP1); the same
     value was the frozen pass's magic word (rtl-rvtt-loadmacro.cc:161,
     deleted at WP1).  */
  return (0x37u << 24) | (4u << 18) | (8u << 14) | 4u;	/* 0x37120004 */
}

unsigned
dst_face_advance_step_count ()
{
  return 2;
}

uint32_t
absorbed_dst_increment_word ()
{
  /* TT_OP_{WH,BH}_INCRWC (0x38): rwc_cr << 18 | rwc_d << 14
     | rwc_b << 10 | rwc_a << 6, with the exact absorbed increment
     TTINCRWC (0, 2, 0, 0) (rtl-rvtt-loadmacro.cc:1401-1418).  */
  return (0x38u << 24) | (2u << 14);	/* 0x38008000 */
}

}  /* namespace rvtt_macro */
