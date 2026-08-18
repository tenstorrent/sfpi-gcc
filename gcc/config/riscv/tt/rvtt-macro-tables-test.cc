/* Standalone unit tests for the SFPLOADMACRO capability tables (WP6).
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

/* Host-compiled, self-contained (the tables layer is freestanding by
   design; a DejaGnu compiler test cannot exercise it until the planner
   consumes it at WP7).  Build and run from this directory:

     g++ -std=c++11 -Wall -Wextra -Werror -I. \
	 rvtt-macro-tables-test.cc rvtt-macro-tables.cc \
	 -o /tmp/rvtt-macro-tables-test && /tmp/rvtt-macro-tables-test

   Every hex literal below is a TEST EXPECTATION transcribed from the
   frozen Min/Max pass (4e045d31d, rtl-rvtt-loadmacro.cc), its DejaGnu
   assertions, or the TT_OP_* tables -- the legitimate home for such
   values per the non-negotiable compiler rule.  Derivations are cited
   inline; the full provenance audit is NOTES-wp6-prep.md.  */

#include "rvtt-macro-tables.h"

#include <stdio.h>
#include <string.h>

using namespace rvtt_macro;

static int failures;
static int checks;

#define CHECK(cond)							\
  do									\
    {									\
      ++checks;								\
      if (!(cond))							\
	{								\
	  ++failures;							\
	  fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,	\
		   #cond);						\
	}								\
    }									\
  while (0)

#define CHECK_EQ_HEX(a, b)						\
  do									\
    {									\
      ++checks;								\
      uint32_t a_ = (a), b_ = (b);					\
      if (a_ != b_)							\
	{								\
	  ++failures;							\
	  fprintf (stderr, "FAIL %s:%d: %s == %s (0x%08x != 0x%08x)\n",	\
		   __FILE__, __LINE__, #a, #b, a_, b_);			\
	}								\
    }									\
  while (0)

/* -------------------- QSR refusal -------------------- */

static void
test_qsr_refusal ()
{
  CHECK (rvtt_macro_caps_for_cpu (CPU_QSR) == nullptr);
  const char *r = rvtt_macro_caps_refusal (CPU_QSR);
  CHECK (r != nullptr
	 && strcmp (r, "target-macro-encoding-unproven") == 0);
  CHECK (rvtt_macro_caps_refusal (CPU_WH) == nullptr);
  CHECK (rvtt_macro_caps_refusal (CPU_BH) == nullptr);
}

/* -------------------- launch words -------------------- */

static void
test_launch_bh (const caps *bh)
{
  uint32_t w;

  /* Frozen formula (rtl-rvtt-loadmacro.cc:993-1003): 0x93000000
     | ((macro << 2 | vd) << 20) | mode << 16 | addr_mode << 13 | addr.
     Canonical minmax-bh test operands: addresses 0/64/128, mode 0,
     no-inc 7, auto-inc 6 (loadmacro-periodic-minmax-body.h).  */
  CHECK (encode_launch (bh, 0, 0, 0, 7, 0, &w));	/* m0 row even  */
  CHECK_EQ_HEX (w, 0x9300e000u);
  CHECK (encode_launch (bh, 0, 1, 0, 7, 0, &w));	/* m0 row odd   */
  CHECK_EQ_HEX (w, 0x9310e000u);
  CHECK (encode_launch (bh, 1, 3, 0, 6, 128, &w));	/* m1 store L3  */
  CHECK_EQ_HEX (w, 0x9370c080u);

  /* Signbit launch (rtl-rvtt-loadmacro.cc:975-980): 0x93100000
     | mode << 16 | addr_mode << 13 | addr; macro 0, VD = L1,
     store mode DEFAULT (0), auto-increment 6.  */
  CHECK (encode_launch (bh, 0, 1, 0, 6, 0, &w));
  CHECK_EQ_HEX (w, 0x9310c000u);

  /* Round trip.  */
  unsigned m, vd, mode, am, addr;
  CHECK (decode_launch (bh, 0x9370c080u, &m, &vd, &mode, &am, &addr));
  CHECK (m == 1 && vd == 3 && mode == 0 && am == 6 && addr == 128);

  /* 9(a) resolution ground truth (2026-08-17): the shipped 8/8-CRAQ
     minmax-final-craq-v2 BH oracle ELFs contain exactly the launch words
     0x9300E000 and 0x9370C000 (RISC-V-embedded as 0x4C038002/0x4DC30002,
     tensix = ror32 (embedded, 2)); riscv-tt-elf-objdump decodes them as
     `sfploadmacro 0,L0,0,0,7` / `sfploadmacro 1,L3,0,0,6`.  Pin that the
     3-bit addr modes 7/6 sit in bits 15:13 with InstrMod0 (19:16) zero:
     at the stale << 14 mode 7 would need bit 16 and could never decode
     with a zero mode nibble.  */
  CHECK (decode_launch (bh, 0x9300e000u, &m, &vd, &mode, &am, &addr));
  CHECK (m == 0 && vd == 0 && mode == 0 && am == 7 && addr == 0);
  CHECK (decode_launch (bh, 0x9370c000u, &m, &vd, &mode, &am, &addr));
  CHECK (m == 1 && vd == 3 && mode == 0 && am == 6 && addr == 0);
  CHECK (encode_launch (bh, 0, 0, 0, 7, 0, &w)
	 && (w & (0xfu << 16)) == 0 && ((w >> 13) & 7u) == 7u);

  /* Boundary refusals: odd address (aliases VD-high bit), 11-bit
     address, oversized fields.  */
  CHECK (!encode_launch (bh, 0, 0, 0, 7, 65, &w));
  CHECK (!encode_launch (bh, 0, 0, 0, 7, 0x400, &w));
  CHECK (!encode_launch (bh, 0, 0, 0, 8, 0, &w));	/* 3-bit field  */
  CHECK (!encode_launch (bh, 0, 0, 16, 7, 0, &w));
  CHECK (!encode_launch (bh, 4, 0, 0, 7, 0, &w));
  CHECK (!encode_launch (bh, 0, 4, 0, 7, 0, &w));

  /* Non-canonical stray bits refuse to decode.  */
  CHECK (!decode_launch (bh, 0x9300e000u | (1u << 10), &m, &vd, &mode,
			 &am, &addr));
  /* Wrong opcode refuses.  */
  CHECK (!decode_launch (bh, 0x7000e000u, &m, &vd, &mode, &am, &addr));
}

static void
test_launch_wh (const caps *wh)
{
  uint32_t w;

  /* WH shift 14, 2-bit addr-mode field, no-inc 3, auto-inc 2
     (rtl-rvtt-loadmacro.cc:1001, 1024-1025; sfpu-ops-wh.h:256).  */
  CHECK (encode_launch (wh, 0, 0, 0, 3, 0, &w));
  CHECK_EQ_HEX (w, 0x9300c000u);
  CHECK (encode_launch (wh, 0, 1, 0, 3, 0, &w));
  CHECK_EQ_HEX (w, 0x9310c000u);
  CHECK (encode_launch (wh, 1, 3, 0, 2, 128, &w));
  CHECK_EQ_HEX (w, 0x93708080u);

  /* Select launches (rtl-rvtt-loadmacro.cc:1608-1614):
     0x93000000 | macro << 22 | mode << 16 | 0 << 14 | addr; macro << 22
     is (macro << 2 | 0) << 20, i.e. VD = 0 for all three launches.  */
  CHECK (encode_launch (wh, 0, 0, 2, 0, 0x40, &w));
  CHECK_EQ_HEX (w, 0x93020040u);
  CHECK (encode_launch (wh, 1, 0, 6, 0, 0x40, &w));
  CHECK_EQ_HEX (w, 0x93460040u);
  CHECK (encode_launch (wh, 2, 0, 6, 0, 0x40, &w));
  CHECK_EQ_HEX (w, 0x93860040u);

  /* WH 2-bit addr-mode boundary.  */
  CHECK (!encode_launch (wh, 0, 0, 0, 4, 0, &w));

  unsigned m, vd, mode, am, addr;
  CHECK (decode_launch (wh, 0x93860040u, &m, &vd, &mode, &am, &addr));
  CHECK (m == 2 && vd == 0 && mode == 6 && am == 0 && addr == 0x40);
}

/* -------------------- Dst address predicate -------------------- */

static void
test_dst_address (const caps *c)
{
  CHECK (dst_address_encodable (c, 0));
  CHECK (dst_address_encodable (c, 64));
  CHECK (dst_address_encodable (c, 1022));
  CHECK (!dst_address_encodable (c, 1));	/* odd		 */
  CHECK (!dst_address_encodable (c, 65));	/* odd		 */
  CHECK (!dst_address_encodable (c, 1023));	/* odd		 */
  CHECK (!dst_address_encodable (c, 1024));	/* > 10 bits	 */
}

/* -------------------- SETC16 -------------------- */

static void
test_setc16 (const caps *bh, const caps *wh)
{
  uint32_t w;

  /* BH slot-6 words asserted by loadmacro-periodic-minmax-bh.C
     (.ttinsn 2987524096/2988572674/2989817856).  */
  CHECK (encode_setc16 (bh, 18, 0, &w));
  CHECK_EQ_HEX (w, 0xb2120000u);
  CHECK (encode_setc16 (bh, 34, 2, &w));
  CHECK_EQ_HEX (w, 0xb2220002u);
  CHECK (encode_setc16 (bh, 53, 0, &w));
  CHECK_EQ_HEX (w, 0xb2350000u);

  /* WH slot-2 + slot-6 words asserted by loadmacro-periodic-minmax-wh.C.  */
  CHECK (encode_setc16 (wh, 11, 0, &w));
  CHECK_EQ_HEX (w, 0xb20b0000u);
  CHECK (encode_setc16 (wh, 25, 2, &w));
  CHECK_EQ_HEX (w, 0xb2190002u);
  CHECK (encode_setc16 (wh, 50, 0, &w));
  CHECK_EQ_HEX (w, 0xb2320000u);
  CHECK (encode_setc16 (wh, 19, 0, &w));
  CHECK_EQ_HEX (w, 0xb2130000u);
  CHECK (encode_setc16 (wh, 29, 2, &w));
  CHECK_EQ_HEX (w, 0xb21d0002u);
  CHECK (encode_setc16 (wh, 54, 0, &w));
  CHECK_EQ_HEX (w, 0xb2360000u);

  /* Round trip + boundaries.  */
  unsigned reg, val;
  CHECK (decode_setc16 (bh, 0xb2220002u, &reg, &val));
  CHECK (reg == 34 && val == 2);
  CHECK (!encode_setc16 (bh, 0x100, 0, &w));
  CHECK (!encode_setc16 (bh, 0, 0x10000, &w));
  CHECK (!decode_setc16 (bh, 0x91000000u, &reg, &val));
}

/* -------------------- address-modifier programs -------------------- */

static void
test_addr_mod (const caps *bh, const caps *wh)
{
  setc16_program prog[6];
  unsigned n;
  bool base;

  /* BH: physical slot 6 only; regs (18,0)(34,2)(53,0)
     (rtl-rvtt-loadmacro.cc:826-831).  */
  CHECK (addr_mod_program (bh, 2, prog, &n, &base));
  CHECK (n == 3 && !base);
  CHECK (prog[0].config_reg == 18 && prog[0].value == 0);
  CHECK (prog[1].config_reg == 34 && prog[1].value == 2);
  CHECK (prog[2].config_reg == 53 && prog[2].value == 0);

  /* WH: exactly ONE physical slot -- scratch modifier 2 under the
     pinned ADDR_MOD_SET_Base=1 = physical slot 6, regs (19,0)(29,2)
     (54,0).  The base-0 bank (11/25/50 = LLK's live ADDR_MOD_2) must
     never appear (sfpi-gcc 2a0ba1e6602 adjudication;
     laneAJ-evidence-20260817).  */
  CHECK (addr_mod_program (wh, 2, prog, &n, &base));
  CHECK (n == 3 && base);
  CHECK (prog[0].config_reg == 19 && prog[0].value == 0);
  CHECK (prog[1].config_reg == 29 && prog[1].value == 2);
  CHECK (prog[2].config_reg == 54 && prog[2].value == 0);
  for (unsigned i = 0; i < n; ++i)
    CHECK (prog[i].config_reg != 11 && prog[i].config_reg != 25
	   && prog[i].config_reg != 50);

  /* Only Dst += 2 is proven; every other delta refuses.  */
  CHECK (!addr_mod_program (bh, 0, prog, &n, &base));
  CHECK (!addr_mod_program (bh, 4, prog, &n, &base));
  CHECK (!addr_mod_program (bh, -2, prog, &n, &base));
  CHECK (!addr_mod_program (wh, 8, prog, &n, &base));

  /* Address-mode values.  */
  CHECK (bh->no_increment_addr_mode == 7
	 && bh->auto_increment_dst2_addr_mode == 6);
  CHECK (wh->no_increment_addr_mode == 3
	 && wh->auto_increment_dst2_addr_mode == 2);
}

/* -------------------- template words -------------------- */

static void
test_templates (const caps *c)
{
  uint32_t w;

  /* Min/Max template 0: SFPSWAP (0x92), VC = physical L2, dest =
     routing selector 0xC, mod1 1 (min in VD) / 9 (max in VD)
     (rtl-rvtt-loadmacro.cc:873-887, 781-786).  */
  template_spec swap_min = { 0x92, 0, 2, 0xc, 1 };
  CHECK (encode_template (c, swap_min, &w));
  CHECK_EQ_HEX (w, 0x920002c1u);
  template_spec swap_max = { 0x92, 0, 2, 0xc, 9 };
  CHECK (encode_template (c, swap_max, &w));
  CHECK_EQ_HEX (w, 0x920002c9u);

  /* Min/Max template 1: SFPSHFT2 (0x94) copy, dest selector 0xD,
     mod1 6.  */
  template_spec shft2_copy = { 0x94, 0, 0, 0xd, 6 };
  CHECK (encode_template (c, shft2_copy, &w));
  CHECK_EQ_HEX (w, 0x940000d6u);

  /* Signbit template 0: SFPSHFT2 with imm12 = 0xfe1 (-31), selector
     0xC, mod1 6 (:852).  */
  template_spec shft_31 = { 0x94, 0xfe1, 0, 0xc, 6 };
  CHECK (encode_template (c, shft_31, &w));
  CHECK_EQ_HEX (w, 0x94fe10c6u);

  /* Signbit template 1 / cast-round template 0: SFPCAST (0x90).  */
  template_spec cast_d = { 0x90, 0, 0, 0xd, 0 };
  CHECK (encode_template (c, cast_d, &w));
  CHECK_EQ_HEX (w, 0x900000d0u);
  template_spec cast_c = { 0x90, 0, 0, 0xc, 0 };
  CHECK (encode_template (c, cast_c, &w));
  CHECK_EQ_HEX (w, 0x900000c0u);

  /* Cast-round template 1: SFP_STOCH_RND (0x8e), all upper fields
     zero, mod1 1 (:866).  */
  template_spec rnd = { 0x8e, 0, 0, 0xd, 1 };
  CHECK (encode_template (c, rnd, &w));
  CHECK_EQ_HEX (w, 0x8e0000d1u);

  /* Select templates: SFPSETCC (0x7b) EQ0 and SFPENCC (0x8a)
     (:1568-1569).  */
  template_spec setcc = { 0x7b, 0, 0, 0xc, 6 };
  CHECK (encode_template (c, setcc, &w));
  CHECK_EQ_HEX (w, 0x7b0000c6u);
  template_spec encc = { 0x8a, 0, 0, 0xd, 0 };
  CHECK (encode_template (c, encc, &w));
  CHECK_EQ_HEX (w, 0x8a0000d0u);

  /* Boundaries: unknown routing selector, unproven selector range,
     STOCH_RND with nonzero imm12 (field layout differs above bit 12),
     imm12 overflow.  */
  template_spec bad_sel = { 0x92, 0, 2, 0xe, 1 };
  CHECK (!encode_template (c, bad_sel, &w));
  template_spec bad_sel2 = { 0x92, 0, 2, 8, 1 };
  CHECK (!encode_template (c, bad_sel2, &w));
  template_spec bad_rnd = { 0x8e, 1, 0, 0xd, 1 };
  CHECK (!encode_template (c, bad_rnd, &w));
  template_spec bad_imm = { 0x94, 0x1000, 0, 0xc, 6 };
  CHECK (!encode_template (c, bad_imm, &w));

  /* Decode round trip.  */
  template_spec out;
  CHECK (decode_template (0x94fe10c6u, &out));
  CHECK (out.opcode == 0x94 && out.imm12 == 0xfe1 && out.src_c == 0
	 && out.dest_sel == 0xc && out.mod1 == 6);

  /* Hidden physical-LREG writes: the SFPSWAP template writes L2
     (RTL clobber of hard reg 82 = SFPU_REG_FIRST + 2) for either mod.  */
  CHECK (template_hidden_lreg_writes (c, 0x920002c1u) == (1u << 2));
  CHECK (template_hidden_lreg_writes (c, 0x920002c9u) == (1u << 2));
  CHECK (template_hidden_lreg_writes (c, 0x940000d6u) == 0);
  CHECK (template_hidden_lreg_writes (c, 0x900000c0u) == 0);
}

/* -------------------- sequence words -------------------- */

static void
test_sequences (const caps *c)
{
  uint32_t w;

  /* Min/Max macro 0: one templated SFPSWAP event, no store.  */
  seq_event m0[] = { { SU_SIMPLE, 0, DELAY_UNKNOWN, false } };
  CHECK (encode_sequence (c, 0, m0, 1, &w));
  CHECK_EQ_HEX (w, 0x00dd008cu);

  /* Min/Max macro 1: template-1 copy plus delayed store.  */
  seq_event m1[] = { { SU_SIMPLE, 1, DELAY_UNKNOWN, false },
		     { SU_STORE, -1, DELAY_UNKNOWN, true } };
  CHECK (encode_sequence (c, 1, m1, 2, &w));
  CHECK_EQ_HEX (w, 0x53000000u);

  /* Cast/round: Simple d0, Round d1, Store d2 (documented delays must
     match).  */
  seq_event cr[] = { { SU_SIMPLE, 0, 0, false },
		     { SU_ROUND, 1, 1, false },
		     { SU_STORE, -1, 2, true } };
  CHECK (encode_sequence (c, 0, cr, 3, &w));
  CHECK_EQ_HEX (w, 0x534d0004u);
  seq_event cr_bad[] = { { SU_SIMPLE, 0, 0, false },
			 { SU_ROUND, 1, 2, false },	/* wrong delay */
			 { SU_STORE, -1, 2, true } };
  CHECK (!encode_sequence (c, 0, cr_bad, 3, &w));

  /* Signbit: shift, cast, store (store delay 3 documented).  */
  seq_event sb[] = { { SU_SIMPLE, 0, DELAY_UNKNOWN, false },
		     { SU_SIMPLE, 1, DELAY_UNKNOWN, false },
		     { SU_STORE, -1, 3, true } };
  CHECK (encode_sequence (c, 0, sb, 3, &w));
  CHECK_EQ_HEX (w, 0x5384004du);
  seq_event sb_bad[] = { { SU_SIMPLE, 0, DELAY_UNKNOWN, false },
			 { SU_SIMPLE, 1, DELAY_UNKNOWN, false },
			 { SU_STORE, -1, 2, true } };	/* wrong delay */
  CHECK (!encode_sequence (c, 0, sb_bad, 3, &w));

  /* Select macros 0/1/2.  */
  seq_event sel0[] = { { SU_SIMPLE, 0, 0, false },
		       { SU_STORE, -1, 2, true } };
  CHECK (encode_sequence (c, 0, sel0, 2, &w));
  CHECK_EQ_HEX (w, 0x13000004u);
  CHECK (encode_sequence (c, 1, nullptr, 0, &w));	/* idle	       */
  CHECK_EQ_HEX (w, 0x00000000u);
  seq_event sel2[] = { { SU_SIMPLE, 1, 0, false } };
  CHECK (encode_sequence (c, 2, sel2, 1, &w));
  CHECK_EQ_HEX (w, 0x00000005u);

  /* Near misses refuse: wrong unit, wrong macro index, extra event,
     wrong template index.  */
  seq_event bad_unit[] = { { SU_MAD, 0, DELAY_UNKNOWN, false } };
  CHECK (!encode_sequence (c, 0, bad_unit, 1, &w));
  CHECK (!encode_sequence (c, 3, m0, 1, &w));
  seq_event extra[] = { { SU_SIMPLE, 0, DELAY_UNKNOWN, false },
			{ SU_SIMPLE, 1, DELAY_UNKNOWN, false } };
  CHECK (!encode_sequence (c, 0, extra, 2, &w));
  seq_event bad_t[] = { { SU_SIMPLE, 1, DELAY_UNKNOWN, false } };
  CHECK (!encode_sequence (c, 0, bad_t, 1, &w));
}

/* -------------------- misc words -------------------- */

static void
test_misc (const caps *c)
{
  uint32_t w;
  CHECK (encode_misc_select (c, 6, &w));
  CHECK_EQ_HEX (w, 0x00000706u);	/* proven U16 select instance  */
  CHECK (encode_misc_select (c, 0, &w));
  CHECK_EQ_HEX (w, 0x00000700u);
  CHECK (!encode_misc_select (c, 16, &w));

  /* Proven whole-word misc entries present.  */
  bool saw_binary = false, saw_signbit = false, saw_castround = false;
  bool saw_launch_mod0 = false;
  for (unsigned i = 0; i < c->n_misc_words; ++i)
    {
      const misc_word_entry &m = c->misc_words[i];
      if (strcmp (m.name, "minmax-binary") == 0)
	{
	  saw_binary = true;
	  CHECK_EQ_HEX (m.word, 0x00000330u);
	}
      else if (strcmp (m.name, "signbit") == 0)
	{
	  saw_signbit = true;
	  CHECK_EQ_HEX (m.word, 0x00000110u);
	}
      else if (strcmp (m.name, "cast-round") == 0)
	{
	  saw_castround = true;
	  CHECK_EQ_HEX (m.word, 0x00000100u);
	}
      else if (strcmp (m.name, "select-launch-mod0") == 0)
	{
	  /* WP10 compact select: the shipped handwritten Where
	     protocol's whole misc word (UsesLoadMod0ForStore +
	     WaitForElapsedInstructions for all macros).  */
	  saw_launch_mod0 = true;
	  CHECK_EQ_HEX (m.word, 0x00000770u);
	  CHECK (!m.store_mod0_in_bits_3_0);
	}
    }
  CHECK (saw_binary && saw_signbit && saw_castround && saw_launch_mod0);
}

/* -------------------- fixed architectural words -------------------- */

static void
test_fixed_words ()
{
  /* SFPENCC (3, 10) all-lanes enable.  */
  CHECK_EQ_HEX (sfpencc_all_lanes_word (), 0x8a00300au);
  /* The one SFPENCC word derivation: the all-lanes constant is
     encode (3, 10); no other proven (imm12, mod1) pair may alias it;
     out-of-range fields refuse.  */
  {
    uint32_t w = 0;
    CHECK (sfpencc_encode (3, 10, &w));
    CHECK_EQ_HEX (w, sfpencc_all_lanes_word ());
    CHECK (sfpencc_encode (0, 10, &w));		/* lanes off (EI, imm 0) */
    CHECK (w != sfpencc_all_lanes_word ());
    CHECK (sfpencc_encode (1, 10, &w));		/* enable only, no result */
    CHECK (w != sfpencc_all_lanes_word ());
    CHECK (sfpencc_encode (10, 3, &w));		/* swapped operand roles  */
    CHECK (w != sfpencc_all_lanes_word ());
    CHECK (sfpencc_encode (3, 0, &w));		/* EU_R1: enable untouched */
    CHECK (w != sfpencc_all_lanes_word ());
    CHECK (!sfpencc_encode (0x1003, 10, &w));	/* imm12 out of range      */
    CHECK (!sfpencc_encode (3, 0x1a, &w));	/* mod1 out of range       */
  }
  /* SETRWC (0, 4, 8, 0, 0, 4): CR-mode Dst += 8; two per face advance.
     Equals the frozen pass's deleted magic word (loadmacro.cc:161).  */
  CHECK_EQ_HEX (dst_step8_setrwc_word (), 0x37120004u);
  CHECK (dst_face_advance_step_count () == 2);
  /* TTINCRWC (0, 2, 0, 0), the absorbed explicit increment.  */
  CHECK_EQ_HEX (absorbed_dst_increment_word (), 0x38008000u);
  {
    /* setrwc_decode round-trips the derived Dst-step word (the field
       decode and the encoding share one layout, pinned here).  */
    setrwc_fields f;
    CHECK (setrwc_decode (dst_step8_setrwc_word (), &f));
    CHECK (f.clear_ab_vld == 0 && f.rwc_cr == 4 && f.rwc_d == 8
	   && f.rwc_b == 0 && f.rwc_a == 0 && f.bit_mask == 4);
    /* Field placement: each field lands in its own slot.  */
    CHECK (setrwc_decode (0x37u << 24 | 1u << 22 | 5u << 14 | 3u << 10
			  | 2u << 6 | 0x9u, &f));
    CHECK (f.clear_ab_vld == 1 && f.rwc_cr == 0 && f.rwc_d == 5
	   && f.rwc_b == 3 && f.rwc_a == 2 && f.bit_mask == 0x9);
    /* Any other opcode byte refuses (SFPCONFIG-class word).  */
    CHECK (!setrwc_decode (0x91u << 24 | 0x4u, &f));
    CHECK (!setrwc_decode (absorbed_dst_increment_word (), &f));
  }
}

/* -------------------- scalar capability data -------------------- */

static void
test_scalars (const caps *bh, const caps *wh)
{
  CHECK (bh->n_templates == 4 && bh->n_sequence_slots == 4
	 && bh->delay_bits == 3);
  CHECK (wh->n_templates == 4 && wh->n_sequence_slots == 4
	 && wh->delay_bits == 3);
  CHECK (bh->owned_config_dests == 0x0173);	/* {0,1,4,5,6,8}       */
  CHECK (wh->owned_config_dests == 0x0173);
  CHECK (bh->proven_drain_slots == 3 && wh->proven_drain_slots == 3);
  /* Frozen-pass break-evens: reference data for the WP7 cost-model
     regression, never a planner input.  */
  CHECK (bh->reference_breakeven_rows == 7);
  CHECK (wh->reference_breakeven_rows == 8);
  CHECK (bh->addr_mode_shift == 13 && bh->addr_mode_bits == 3);
  CHECK (wh->addr_mode_shift == 14 && wh->addr_mode_bits == 2);
  CHECK (!bh->needs_bank_base_ownership && wh->needs_bank_base_ownership);
}

/* -------------------- WH/BH common-data identity ----------------- */

/* The WH DejaGnu positive asserts the identical descriptor-state words
   as BH; the two .def files must never diverge on the shared data.  */

static void
test_wh_bh_identity (const caps *bh, const caps *wh)
{
  CHECK (bh->n_ref_descriptors == wh->n_ref_descriptors);
  for (unsigned i = 0; i < bh->n_ref_descriptors
	 && i < wh->n_ref_descriptors; ++i)
    {
      CHECK (strcmp (bh->ref_descriptors[i].shape,
		     wh->ref_descriptors[i].shape) == 0);
      CHECK (bh->ref_descriptors[i].dest == wh->ref_descriptors[i].dest);
      CHECK_EQ_HEX (bh->ref_descriptors[i].word,
		    wh->ref_descriptors[i].word);
    }

  CHECK (bh->n_seq_programs == wh->n_seq_programs);
  for (unsigned i = 0; i < bh->n_seq_programs && i < wh->n_seq_programs;
       ++i)
    {
      CHECK (strcmp (bh->seq_programs[i].name,
		     wh->seq_programs[i].name) == 0);
      CHECK_EQ_HEX (bh->seq_programs[i].word, wh->seq_programs[i].word);
      CHECK (bh->seq_programs[i].macro_index
	     == wh->seq_programs[i].macro_index);
      CHECK (bh->seq_programs[i].n_events == wh->seq_programs[i].n_events);
    }

  CHECK (bh->n_misc_words == wh->n_misc_words);
  for (unsigned i = 0; i < bh->n_misc_words && i < wh->n_misc_words; ++i)
    CHECK_EQ_HEX (bh->misc_words[i].word, wh->misc_words[i].word);

  CHECK (bh->n_hidden_writes == wh->n_hidden_writes);
}

/* -------------------- reference descriptor sets ------------------ */

static uint32_t
ref_word (const caps *c, const char *shape, unsigned dest, bool *found)
{
  for (unsigned i = 0; i < c->n_ref_descriptors; ++i)
    if (strcmp (c->ref_descriptors[i].shape, shape) == 0
	&& c->ref_descriptors[i].dest == dest)
      {
	*found = true;
	return c->ref_descriptors[i].word;
      }
  *found = false;
  return 0;
}

static void
check_ref (const caps *c, const char *shape, unsigned dest,
	   uint32_t expect)
{
  bool found = false;
  uint32_t w = ref_word (c, shape, dest, &found);
  CHECK (found);
  if (found)
    CHECK_EQ_HEX (w, expect);
}

static void
test_ref_descriptors (const caps *c)
{
  /* Min/Max (rtl-rvtt-loadmacro.cc:873-887).  */
  check_ref (c, "minmax-binary-min", 0, 0x920002c1u);
  check_ref (c, "minmax-binary-max", 0, 0x920002c9u);
  check_ref (c, "minmax-binary-min", 1, 0x940000d6u);
  check_ref (c, "minmax-binary-min", 4, 0x00dd008cu);
  check_ref (c, "minmax-binary-min", 5, 0x53000000u);
  check_ref (c, "minmax-binary-min", 8, 0x00000330u);
  /* Signbit (:847-856).  */
  check_ref (c, "signbit", 0, 0x94fe10c6u);
  check_ref (c, "signbit", 1, 0x900000d0u);
  check_ref (c, "signbit", 4, 0x5384004du);
  check_ref (c, "signbit", 8, 0x00000110u);
  /* Cast/round (:858-871).  */
  check_ref (c, "cast-round", 0, 0x900000c0u);
  check_ref (c, "cast-round", 1, 0x8e0000d1u);
  check_ref (c, "cast-round", 4, 0x534d0004u);
  check_ref (c, "cast-round", 8, 0x00000100u);
  /* Select (:1568-1572, 1596-1606).  */
  check_ref (c, "select-u16", 0, 0x7b0000c6u);
  check_ref (c, "select-u16", 1, 0x8a0000d0u);
  check_ref (c, "select-u16", 4, 0x13000004u);
  check_ref (c, "select-u16", 5, 0x00000000u);
  check_ref (c, "select-u16", 6, 0x00000005u);
  check_ref (c, "select-u16", 8, 0x00000706u);

  /* Every reference template word must round-trip through the generic
     encoder (dest-0/1 entries), proving the packer covers the proven
     shapes.  */
  for (unsigned i = 0; i < c->n_ref_descriptors; ++i)
    {
      const ref_descriptor_word &r = c->ref_descriptors[i];
      if (r.dest > 1 || r.word == 0)
	continue;
      template_spec t;
      uint32_t w;
      CHECK (decode_template (r.word, &t));
      CHECK (encode_template (c, t, &w));
      CHECK_EQ_HEX (w, r.word);
    }
}

/* -------------------- reference SETC16 program ------------------- */

static void
test_ref_setc16_words (const caps *c, const uint32_t *expect, unsigned n)
{
  CHECK (c->n_ref_addr_mod_setc16 == n);
  for (unsigned i = 0; i < n && i < c->n_ref_addr_mod_setc16; ++i)
    {
      uint32_t w;
      CHECK (encode_setc16 (c, c->ref_addr_mod_setc16[i].config_reg,
			    c->ref_addr_mod_setc16[i].value, &w));
      CHECK_EQ_HEX (w, expect[i]);
    }
}

/* WP9 CC-template facts: the deferred-CC visibility lag, the
   live-at-execution store lane mask (silicon adjudication 2026-08-17;
   craq-sim 9f324140), the architecturally-defined SFPSETCC
   complement class, and the select restore program on macro one (the
   whole frozen "ENCC d0" word at the derived calendar's macro index;
   both CPUs carry it identically -- asserted by test_wh_bh_identity's
   whole-table sweep).  */

static void
test_cc_template_facts (const caps *bh, const caps *wh)
{
  CHECK (cc_visibility_lag () == 1);
  CHECK (store_lane_mask_live_at_execution ());

  unsigned out = 0;
  CHECK (sfpsetcc_complement_mod1 (0, &out) && out == 4);
  CHECK (sfpsetcc_complement_mod1 (2, &out) && out == 6);
  CHECK (sfpsetcc_complement_mod1 (4, &out) && out == 0);
  CHECK (sfpsetcc_complement_mod1 (6, &out) && out == 2);
  /* Immediate and force-false forms have no defined complement.  */
  CHECK (!sfpsetcc_complement_mod1 (1, &out));
  CHECK (!sfpsetcc_complement_mod1 (3, &out));
  CHECK (!sfpsetcc_complement_mod1 (5, &out));
  CHECK (!sfpsetcc_complement_mod1 (7, &out));
  CHECK (!sfpsetcc_complement_mod1 (8, &out));
  CHECK (!sfpsetcc_complement_mod1 (9, &out));

  const caps *cpus[] = { bh, wh };
  for (const caps *c : cpus)
    {
      uint32_t m1_encc = 0, m2_encc = 0;
      bool have_m1 = false, have_m2 = false;
      for (unsigned i = 0; i != c->n_seq_programs; ++i)
	{
	  if (!strcmp (c->seq_programs[i].name, "select-m1-encc"))
	    {
	      have_m1 = true;
	      m1_encc = c->seq_programs[i].word;
	      CHECK (c->seq_programs[i].macro_index == 1);
	      CHECK (c->seq_programs[i].n_events == 1);
	      CHECK (c->seq_programs[i].events[0].unit == SU_SIMPLE);
	      CHECK (c->seq_programs[i].events[0].template_index == 1);
	      CHECK (c->seq_programs[i].events[0].delay == 0);
	    }
	  if (!strcmp (c->seq_programs[i].name, "select-m2"))
	    {
	      have_m2 = true;
	      m2_encc = c->seq_programs[i].word;
	    }
	}
      CHECK (have_m1 && have_m2);
      /* The whole word is the frozen "ENCC d0" program; the macro index
	 only selects the SFPCONFIG destination.  */
      CHECK_EQ_HEX (m1_encc, m2_encc);
    }
}

int
main ()
{
  const caps *bh = rvtt_macro_caps_for_cpu (CPU_BH);
  const caps *wh = rvtt_macro_caps_for_cpu (CPU_WH);
  CHECK (bh != nullptr && wh != nullptr);
  if (!bh || !wh)
    {
      fprintf (stderr, "capability tables missing; aborting\n");
      return 1;
    }

  test_qsr_refusal ();
  test_launch_bh (bh);
  test_launch_wh (wh);
  test_dst_address (bh);
  test_dst_address (wh);
  test_setc16 (bh, wh);
  test_addr_mod (bh, wh);
  test_templates (bh);
  test_templates (wh);
  test_sequences (bh);
  test_sequences (wh);
  test_misc (bh);
  test_misc (wh);
  test_fixed_words ();
  test_cc_template_facts (bh, wh);
  test_scalars (bh, wh);
  test_wh_bh_identity (bh, wh);
  test_ref_descriptors (bh);
  test_ref_descriptors (wh);

  static const uint32_t bh_setc16[] =
    { 0xb2120000u, 0xb2220002u, 0xb2350000u };
  /* WH: the single Base=1 slot (physical slot 6, regs 19/29/54);
     0xb2 << 24 | reg << 16 | value.  The base-0 bank words
     0xb20b0000/0xb2190002/0xb2320000 (regs 11/25/50 = LLK's live
     ADDR_MOD_2) must never be emitted (sfpi-gcc 2a0ba1e6602).  */
  static const uint32_t wh_setc16[] =
    { 0xb2130000u, 0xb21d0002u, 0xb2360000u };
  test_ref_setc16_words (bh, bh_setc16, 3);
  test_ref_setc16_words (wh, wh_setc16, 3);

  printf ("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
