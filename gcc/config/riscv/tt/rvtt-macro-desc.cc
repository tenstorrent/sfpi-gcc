/* Macro-planner descriptor synthesis (Layer 4).
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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "tm_p.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"

/* Selection is keyed by the DERIVED event structure -- per-macro subunit
   lists, store placement, and the admitted source instructions' encoding
   identity (unspec -> architectural opcode byte through the retained
   TT_OP encoding tables).  Shape names, operation names, and source
   structure never participate.  Raw whole words below are PROVEN
   descriptor programs transcribed with provenance from the frozen pass
   (see NOTES-wp6-prep.md); template fields are packed from admitted
   operands wherever the field derivation is established, and stay
   whole-word otherwise (9(d)/9(e) partial semantics).  */

const char *macro_desc_refusal_program_unproven
  = "descriptor-program-unproven";
const char *macro_desc_refusal_encoding_failed
  = "descriptor-encoding-failed";
const char *macro_desc_refusal_verification_failed
  = "descriptor-verification-failed";
const char *macro_desc_refusal_cc_template_unproved
  = "cc-template-unproved";

namespace {

using namespace rvtt_macro;

/* The architectural opcode byte of an admitted value instruction, from
   the retained TT_OP encoding tables, keyed by the insn's unspec (the
   design-sanctioned encodability key).  Returns 0 when no entry is on
   record (synthesis then refuses by program-unproven).  */

static int
insn_unspecv (rtx_insn *insn)
{
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == PARALLEL)
    pat = XVECEXP (pat, 0, 0);
  if (GET_CODE (pat) == SET)
    pat = SET_SRC (pat);
  if (GET_CODE (pat) == UNSPEC_VOLATILE || GET_CODE (pat) == UNSPEC)
    return XINT (pat, 1);
  return -1;
}

static uint8_t
source_opcode_byte (rtx_insn *insn)
{
  switch (insn_unspecv (insn))
    {
    case UNSPECV_SFPSWAP:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPSWAP (0, 0, 0, 0)
	      : TT_OP_BH_SFPSWAP (0, 0, 0, 0)) >> 24;
    case UNSPECV_SFPSHFT:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPSHFT (0, 0, 0, 0)
	      : TT_OP_BH_SFPSHFT (0, 0, 0, 0)) >> 24;
    case UNSPECV_SFPCAST:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPCAST (0, 0, 0)
	      : TT_OP_BH_SFPCAST (0, 0, 0)) >> 24;
    case UNSPECV_SFPSTOCHRND:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)
	      : TT_OP_BH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)) >> 24;
    case UNSPECV_SFPSETCC:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPSETCC (0, 0, 0, 0)
	      : TT_OP_BH_SFPSETCC (0, 0, 0, 0)) >> 24;
    case UNSPECV_SFPENCC:
      return (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPENCC (0, 0, 0, 0)
	      : TT_OP_BH_SFPENCC (0, 0, 0, 0)) >> 24;
    default:
      return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Proven descriptor programs.  KEYS are derived structure + encoding
   identity; PAYLOADS are proven words / established field rules.       */
/* ------------------------------------------------------------------ */

enum templ_rule_kind
{
  /* Pack opcode/imm12/src/mod1 from the admitted source instruction;
     dest_sel is positional (0xC + template index, the regularity of
     every proven shape).  */
  TR_FIELDS_FROM_SOURCE,
  /* As above, plus the dataflow-selected result-routing bit: when the
     source's SECOND result set reaches the store, mod1 gains the
     routing bit (frozen provenance rtl-rvtt-loadmacro.cc:781-786).  */
  TR_FIELDS_FROM_SOURCE_ROUTING_MOD,
  /* Pack the template entirely from table data (WP8: replaces the
     former proven-whole-word payloads, so no raw template words live
     outside the capability/encoding tables): the per-CPU opcode byte
     from the retained TT_OP encoding tables, a fixed proven mod1 field,
     a fixed src field, the positional dest selector, and -- when
     imm12_op >= 0 -- the imm12 field packed from the source's typed
     immediate operand (established: the field carries exactly the
     explicit shift amount; frozen :469, :852).  Covers templates whose
     event is internal to the macro program (no derivable source
     instruction) or whose opcode differs from the source instruction's
     (the SHFT2 immediate variant realizing an explicit SFPSHFT,
     NOTES-wp6-prep.md 9(e)).  */
  TR_TABLE_FIELDS,
  /* WP9: pack opcode from the source predicate definition and derive
     the template's mod1 from the source's predicate-sense operand
     through the CC model: when the post-visibility payload load
     carries the merge's LIVE operand, the template takes the
     architecturally-defined complement of the source sense
     (sfpsetcc_complement_mod1); the sense must lie in the
     register-test class either way (the proven envelope; immediate and
     force-false predicates refuse).  */
  TR_FIELDS_FROM_SOURCE_CC_SENSE,
};

struct desc_event_key
{
  uint8_t subunit;		/* rvtt_macro::subunit_t	       */
  uint8_t opcode_wh, opcode_bh;	/* source opcode byte per CPU	       */
};

struct desc_macro_key
{
  unsigned n_events;		/* derived hosted value events	       */
  desc_event_key ev[2];
  bool store;
};

struct desc_template_rule
{
  templ_rule_kind kind;
  uint8_t opcode_wh, opcode_bh;	/* TR_TABLE_FIELDS: TT_OP opcode byte  */
  uint8_t fixed_mod1;		/* TR_TABLE_FIELDS: proven mod1 field  */
  uint8_t src_c_plan;		/* planned physical src field	       */
  int source_event;		/* flat value-event index; -1 internal */
  int mod1_op;			/* source operand carrying mod1	       */
  int imm12_op;			/* source operand carrying imm12; -1   */
  uint8_t routing_mod_bit;	/* TR_.._ROUTING_MOD only	       */
  /* Proven-envelope operand pin (WP8): the source operand PIN_OP must
     equal the per-CPU pinned value or synthesis refuses
     program-unproven.  Used where the explicit operand has no
     established template field (it must be the proven constant) or
     where the explicit-mode -> template-word mapping is a single proven
     pair (NOTES-wp6-prep.md 9(e)/9(f)).  -1 = no pin.  */
  int pin_op;
  int pin_wh, pin_bh;
};

struct desc_program
{
  const char *provenance;
  unsigned n_macros;
  desc_macro_key macros[2];
  unsigned n_templates;
  desc_template_rule templates[2];
  /* Provenance labels of the proven per-macro sequence programs and the
     proven misc word, resolved against the capability tables (the raw
     words' only home).  Selection is purely structural (the macro keys
     above); these labels act as opaque indices into proven table rows,
     never as recognizers.  */
  const char *seq_names[2];
  const char *misc_name;
  int fixed_vd;			/* -1: alternating pair {0,1}	       */
  unsigned store_only_vd;	/* VD of a store-only carrier	       */
  /* CRAQ-validated envelope: the program was proven only with one
     uniform data mode across every Dst access of the row.  */
  bool uniform_mode_required;
  /* WP9 CC-template program: the row carries a predicate definition, a
     coalesced lane-merge, and the all-lanes restore; synthesis derives
     and proves the macro_cc_model, the misc word is field-derived from
     the store's data mode (encode_misc_select) instead of a fixed
     named word, and the row's explicit typed separator stays in place
     (its issue slot is the restore's visibility slot).  */
  bool cc_select;
  bool misc_from_store_mode;
  bool keep_separator;
};

#define OPB_WH(M) ((uint8_t) ((M) >> 24))

static const desc_program desc_programs[] = {
  /* Binary periodic select-store (frozen minmax calendar, LM:873-887).
     Macro 0 hosts one Simple source event; macro 1 carries the delayed
     store; the transient copy inside macro 1 is part of the proven
     whole-word program, not a derived event.  */
  {
    "binary-periodic (LM:781-786,873-887,1022-1054)",
    2,
    { { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSWAP (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSWAP (0, 0, 0, 0)) } }, false },
      { 0, {}, true } },
    2,
    /* Template 1 is the macro-internal SHFT2 copy into the transient
       LReg16 slot: opcode from the TT_OP tables, proven mod1 6.  */
    { { TR_FIELDS_FROM_SOURCE_ROUTING_MOD, 0, 0, 0, 2 /* planned RHS L2 */,
	0, 4 /* swap mod1 operand */, -1, 8, -1, 0, 0 },
      { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPSHFT2 (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPSHFT2 (0, 0, 0, 0)), 6, 0, -1, -1, -1, 0,
	-1, 0, 0 } },
    { "minmax-binary-m0", "minmax-binary-m1" },
    "minmax-binary",
    -1, 3, true,
  },
  /* Unary shift/cast (frozen signbit calendar, LM:847-856).  The SHFT2
     immediate template aliases its source selector to L1, forcing the
     macro VD (LM:357-359) -- fixed_vd data, not a derived plan.  */
  {
    "unary-shift-cast (LM:469,847-856,357-359)",
    1,
    { { 2, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSHFT (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSHFT (0, 0, 0, 0)) },
	     { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPCAST (0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPCAST (0, 0, 0)) } }, true },
      { 0, {}, false } },
    2,
    /* The explicit-shift-mode -> template-word mapping is a single
       proven pair (WH mode 1 / BH mode 5 -> template mod1 6, NOTES
       9(e)); other shift modes are not the proven program.  */
    { { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPSHFT2 (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPSHFT2 (0, 0, 0, 0)), 6, 0, 0, -1,
	4 /* shift imm operand */, 0, 7 /* shift mode operand */, 1, 5 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 1, 3 /* cast mod1 operand */,
	-1, 0, -1, 0, 0 } },
    { "signbit-m0", nullptr },
    "signbit",
    1, 0, true,
  },
  /* Unary cast/round (frozen U16->BF16 calendar, LM:858-871).  */
  {
    "unary-cast-round (LM:858-871)",
    1,
    { { 2, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPCAST (0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPCAST (0, 0, 0)) },
	     { SU_ROUND, OPB_WH (TT_OP_WH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)) } }, true },
      { 0, {}, false } },
    2,
    /* The round template's low nibble is the explicit instruction's
       architectural instr_mod1 = operand 7 (the raw 0x8e word places
       instr_mod1 at bits 3:0 and the assemblable explicit form carries
       it in operand 7; corrected at WP8 from the untestable operand-8
       reading).  Operand 8 has no field in the 0x8e template layout and
       is pinned to the proven zero.  The imm8 operand routes through
       the imm12 packer, whose 0x8e nonzero refusal keeps unproven
       immediate forms out.  */
    { { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 0, 3 /* cast mod1 */, -1, 0,
	-1, 0, 0 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 1, 7 /* stochrnd instr_mod1 */,
	4 /* imm8 */, 0, 8 /* no template field */, 0, 0 } },
    { "cast-round-m0", nullptr },
    "cast-round",
    -1, 0, false,
  },
  /* Predicated select (the TTNN Where shape class; frozen select
     calendar LM:1568-1606, re-derived generically at WP9).  The
     definition carrier hosts the predicate template and the delayed
     store; the demoted middle payload issues as a plain load (the
     production handwritten Where protocol's own shape); the last
     carrier hosts the all-lanes restore.  The lane-merge itself is
     coalesced into the shared launch VD by the deferred-CC dataflow --
     no template realizes it.  Misc is field-derived (0x700 | store
     mode, encode_misc_select); the proven envelope requires one
     payload/store data mode (the definition carrier's mode is free)
     and keeps the row's explicit separator as the restore-visibility
     slot.  VD is fixed at 0: every payload targets the shared launch
     VD (LM select emission).  */
  {
    "predicated-select (LM:1568-1606; WP9 generic derivation)",
    2,
    { { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSETCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSETCC (0, 0, 0, 0)) } }, true },
      { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPENCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPENCC (0, 0, 0, 0)) } }, false } },
    2,
    /* Template 0: the predicate definition, sense-mapped through the
       CC model (source rvtt_sfpsetcc_v operand 1 is the mod1).
       Template 1: the all-lanes restore, entirely table data (opcode
       from the TT_OP tables, proven mod1 0, imm12 0 -- the frozen
       0x8a0000d0 "Restore all lanes" word re-derived field-wise).  */
    { { TR_FIELDS_FROM_SOURCE_CC_SENSE, 0, 0, 0, 0, 0,
	1 /* setcc mod1 operand */, -1, 0, -1, 0, 0 },
      { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPENCC (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPENCC (0, 0, 0, 0)), 0, 0, -1, -1, -1, 0,
	-1, 0, 0 } },
    { "select-m0", "select-m1-encc" },
    nullptr,
    0, 0, false,
    true, true, true,
  },
};

/* Derived structure of the canonical row: per-carrier value events (in
   program order) and store placement, plus the flat value-event insns.  */

struct derived_structure
{
  unsigned n_macros;
  desc_macro_key macros[2];
  rtx_insn *value_insns[4];
  unsigned n_value_insns;
  rtx_insn *store_insn;
  rtx_insn *load_insns[2];
  unsigned n_load_insns;
};

static bool
derive_structure (const macro_region &region, const macro_schedule &schedule,
		  derived_structure *out)
{
  memset (out, 0, sizeof (*out));
  const macro_row &row = region.rows[0];
  int max_macro = -1;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.dst_mem_read && out->n_load_insns < 2)
	out->load_insns[out->n_load_insns++] = row.insns[ix];
      if (e.dst_mem_write)
	out->store_insn = row.insns[ix];
      if (ev.realization != macro_event::LAUNCHED_TEMPLATE_SLOT)
	continue;
      if ((int) ev.macro_index > max_macro)
	max_macro = ev.macro_index;
      if (ev.macro_index >= 2)
	return false;
      desc_macro_key &mk = out->macros[ev.macro_index];
      if (ev.is_store)
	mk.store = true;
      else
	{
	  if (mk.n_events == 2 || out->n_value_insns == 4)
	    return false;
	  uint8_t opb = source_opcode_byte (row.insns[ix]);
	  if (!opb)
	    return false;
	  mk.ev[mk.n_events].subunit = (uint8_t) e.subunit;
	  mk.ev[mk.n_events].opcode_wh = opb;
	  mk.ev[mk.n_events].opcode_bh = opb;
	  ++mk.n_events;
	  out->value_insns[out->n_value_insns++] = row.insns[ix];
	}
    }
  out->n_macros = max_macro + 1;
  return out->n_macros >= 1 && out->store_insn;
}

static bool
macro_key_matches (const desc_macro_key &key, const desc_macro_key &derived,
		   bool is_wh)
{
  if (key.n_events != derived.n_events || key.store != derived.store)
    return false;
  for (unsigned e = 0; e != key.n_events; ++e)
    {
      uint8_t want = is_wh ? key.ev[e].opcode_wh : key.ev[e].opcode_bh;
      /* The derived key stores the current target's byte in both.  */
      if (key.ev[e].subunit != derived.ev[e].subunit
	  || want != derived.ev[e].opcode_bh)
	return false;
    }
  return true;
}

/* Resolve a proven sequence program / misc word by its provenance
   label in the capability tables -- the raw words' only home.  */

static bool
find_seq_word (const caps *c, const char *name, uint32_t *word)
{
  for (unsigned i = 0; i != c->n_seq_programs; ++i)
    if (!strcmp (c->seq_programs[i].name, name))
      {
	*word = c->seq_programs[i].word;
	return true;
      }
  return false;
}

static bool
find_misc_word (const caps *c, const char *name, uint32_t *word)
{
  for (unsigned i = 0; i != c->n_misc_words; ++i)
    if (!strcmp (c->misc_words[i].name, name))
      {
	*word = c->misc_words[i].word;
	return true;
      }
  return false;
}

static const desc_program *
find_program (const derived_structure &derived)
{
  bool is_wh = TARGET_XTT_TENSIX_WH;
  for (const desc_program &p : desc_programs)
    {
      if (p.n_macros != derived.n_macros)
	continue;
      bool match = true;
      for (unsigned m = 0; m != p.n_macros && match; ++m)
	match = macro_key_matches (p.macros[m], derived.macros[m], is_wh);
      if (match)
	return &p;
    }
  return nullptr;
}

/* Dataflow-selected result routing: does the row's store consume the
   source instruction's SECOND result set?  (Post-admission operand
   access; the frozen provenance is the mod-1/9 selection.)  */

static bool
second_set_reaches_store_p (rtx_insn *value_insn, rtx_insn *store_insn)
{
  rtx pat = PATTERN (value_insn);
  if (GET_CODE (pat) != PARALLEL || XVECLEN (pat, 0) < 2)
    return false;
  rtx set1 = XVECEXP (pat, 0, 1);
  if (GET_CODE (set1) != SET || !REG_P (SET_DEST (set1)))
    return false;
  xtt_effect_set store_effects = rvtt_insn_effects (store_insn);
  unsigned regno = REGNO (SET_DEST (set1));
  if (regno < SFPU_REG_FIRST || regno - SFPU_REG_FIRST > 16)
    return false;
  return (store_effects.lreg_read >> (regno - SFPU_REG_FIRST)) & 1;
}

static bool
const_operand (rtx_insn *insn, int pos, HOST_WIDE_INT *value)
{
  extract_insn (insn);
  if (pos < 0 || pos >= recog_data.n_operands
      || !CONST_INT_P (recog_data.operand[pos]))
    return false;
  *value = INTVAL (recog_data.operand[pos]);
  return true;
}

/* ------------------------------------------------------------------ */
/* WP9: derive and prove the CC-template model of a predicated-select
   row from the schedule's slots, the matched program's proven delays,
   and the architectural CC facts in the capability tables.  Fills OUT
   and *STORE_MODE (the shared payload/store data mode) on success;
   returns false when any obligation fails (the caller refuses
   cc-template-unproved; refusal paths never mutate).		      */
/* ------------------------------------------------------------------ */

static bool
derive_cc_model (const macro_region &region, const macro_schedule &schedule,
		 const rvtt_macro::caps *c, macro_cc_model *out,
		 HOST_WIDE_INT *store_mode)
{
  memset (out, 0, sizeof (*out));
  const macro_row &row = region.rows[0];

  /* Locate the definition, restore, merge, store, and load events.  */
  int def_ix = -1, restore_ix = -1, merge_ix = -1, store_ix = -1;
  int load_ix[4];
  unsigned n_loads = 0;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.dst_mem_read)
	{
	  if (n_loads == 4)
	    return false;
	  load_ix[n_loads++] = ix;
	}
      if (ev.realization == macro_event::CC_COALESCED)
	{
	  if (merge_ix >= 0)
	    return false;
	  merge_ix = ix;
	}
      if (ev.realization != macro_event::LAUNCHED_TEMPLATE_SLOT)
	continue;
      if (ev.is_store)
	{
	  if (store_ix >= 0)
	    return false;
	  store_ix = ix;
	}
      else if (e.cc_write && e.lreg_read)
	{
	  if (def_ix >= 0)
	    return false;	/* two conflicting predicates */
	  def_ix = ix;
	}
      else if (e.cc_write)
	{
	  if (restore_ix >= 0 || !e.cc_write_all_lanes)
	    return false;
	  restore_ix = ix;
	}
    }
  if (def_ix < 0 || restore_ix < 0 || merge_ix < 0 || store_ix < 0
      || n_loads != 3)
    return false;
  /* The row's lane predication rides the ambient all-lanes enable; the
     formation-level proof (needs_all_lanes_prefix) must be armed.  */
  if (!region.net.cc_read)
    return false;

  const macro_event &def_ev = schedule.events[def_ix];
  const macro_event &restore_ev = schedule.events[restore_ix];
  const macro_event &store_ev = schedule.events[store_ix];
  xtt_effect_set def_e = rvtt_insn_effects (row.insns[def_ix]);
  xtt_effect_set merge_e = rvtt_insn_effects (row.insns[merge_ix]);

  if (def_ev.programmed_delay < 0 || restore_ev.programmed_delay < 0
      || store_ev.programmed_delay < 0)
    return false;

  int lag = (int) rvtt_macro::cc_visibility_lag ();
  int def_exec = def_ev.slot + 1 + def_ev.programmed_delay;
  int def_visible = def_exec + lag;
  int restore_exec = restore_ev.slot + 1 + restore_ev.programmed_delay;
  int restore_visible = restore_exec + lag;
  int store_exec = store_ev.slot + 1 + store_ev.programmed_delay;

  /* Map the merge's operands to their producing loads: the live value
     is the merge's own destination, the selected value its one other
     input (the scheduler's coalescing already proved the shape).  */
  uint32_t dest = merge_e.lreg_write;
  uint32_t other = merge_e.lreg_read & ~dest;
  int live_ix = -1, sel_ix = -1, def_src_ix = -1;
  for (unsigned l = 0; l != n_loads; ++l)
    {
      xtt_effect_set le = rvtt_insn_effects (row.insns[load_ix[l]]);
      if (le.lreg_write & dest)
	live_ix = load_ix[l];
      if (le.lreg_write & other)
	sel_ix = load_ix[l];
      if (le.lreg_write & def_e.lreg_read)
	def_src_ix = load_ix[l];
    }
  if (live_ix < 0 || sel_ix < 0 || def_src_ix < 0
      || live_ix == sel_ix || def_src_ix == live_ix || def_src_ix == sel_ix)
    return false;

  /* The definition's source value must be its own carrier's load (the
     dispatch reads the launch VD), and no payload may overwrite the VD
     before the definition executes (events retire before same-slot
     issues).  */
  const macro_event &def_src_ev = schedule.events[def_src_ix];
  if (!def_src_ev.is_carrier || def_src_ev.macro_index != def_ev.macro_index)
    return false;
  int live_slot = schedule.events[live_ix].slot;
  int sel_slot = schedule.events[sel_ix].slot;
  if (live_slot < 0 || sel_slot < 0)
    return false;
  if (live_slot < def_exec || sel_slot < def_exec)
    return false;

  /* Deferred-CC dataflow: exactly one payload issues before the
     definition is visible (the ambient all-lanes load) and the other at
     or after it (the predicated overwrite).  The template's predicate
     sense is complemented exactly when the post-visibility payload is
     the merge's LIVE operand.  */
  int first_slot = live_slot < sel_slot ? live_slot : sel_slot;
  int last_slot = live_slot < sel_slot ? sel_slot : live_slot;
  if (first_slot == last_slot
      || first_slot >= def_visible || last_slot < def_visible)
    return false;
  out->complement = last_slot == live_slot;

  /* The store's lane mask is latched at its carrying launch
     (store_lane_mask_latched_at_launch): it must issue under the
     ambient all-lanes state, and the delayed store must execute after
     both payload loads have written the shared VD.  */
  if (!rvtt_macro::store_lane_mask_latched_at_launch ())
    return false;
  if (store_ev.slot >= def_visible)
    return false;
  if (store_exec <= last_slot)
    return false;

  /* The restore must not become visible before the predicated payload
     issues, and must be visible by the next row's first slot (the kept
     explicit separator provides that slot).  */
  if (restore_visible <= last_slot)
    return false;
  if (restore_visible > schedule.ii)
    return false;

  /* Proven envelope: one payload/store data mode (the definition
     carrier's own load mode is free).  */
  rtx address, mode, addr_mode;
  HOST_WIDE_INT live_mode, sel_mode, st_mode;
  xtt_effect_set live_e = rvtt_insn_effects (row.insns[live_ix]);
  xtt_effect_set sel_e = rvtt_insn_effects (row.insns[sel_ix]);
  xtt_effect_set st_e = rvtt_insn_effects (row.insns[store_ix]);
  if (!rvtt_dst_access_operands (row.insns[live_ix], live_e, &address,
				 &mode, &addr_mode)
      || !CONST_INT_P (mode))
    return false;
  live_mode = INTVAL (mode);
  if (!rvtt_dst_access_operands (row.insns[sel_ix], sel_e, &address,
				 &mode, &addr_mode)
      || !CONST_INT_P (mode))
    return false;
  sel_mode = INTVAL (mode);
  if (!rvtt_dst_access_operands (row.insns[store_ix], st_e, &address,
				 &mode, &addr_mode)
      || !CONST_INT_P (mode))
    return false;
  st_mode = INTVAL (mode);
  if (live_mode != sel_mode || sel_mode != st_mode)
    return false;
  (void) c;

  out->active = true;
  out->def_visible_slot = def_visible;
  out->pre_load_slot = first_slot;
  out->post_load_slot = last_slot;
  out->store_launch_slot = store_ev.slot;
  out->restore_visible_slot = restore_visible;
  out->row_interval = schedule.ii;
  *store_mode = st_mode;
  return true;
}

} // anonymous namespace

void
rvtt_macro_descriptor_release (macro_descriptor *desc)
{
  desc->launches.release ();
}

bool
rvtt_macro_synthesize (const macro_region &region,
		       const macro_schedule &schedule,
		       macro_descriptor *out, FILE *dump)
{
  memset (out, 0, sizeof (*out));
  out->launches = vNULL;
  out->drain_slots = -1;

  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? CPU_BH
    : TARGET_XTT_TENSIX_WH ? CPU_WH : CPU_QSR;
  const caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c)
    return false;		/* already refused at schedule time */

  derived_structure derived;
  const desc_program *program = nullptr;
  if (derive_structure (region, schedule, &derived))
    program = find_program (derived);
  if (program && program->uniform_mode_required)
    {
      /* The proven envelope covers only a uniform data mode across the
	 row's Dst accesses.  */
      rtx first_mode = nullptr;
      for (rtx_insn *insn : region.rows[0].insns)
	{
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (!e.dst_mem_read && !e.dst_mem_write)
	    continue;
	  rtx address, mode, addr_mode;
	  if (!rvtt_dst_access_operands (insn, e, &address, &mode,
					 &addr_mode))
	    continue;
	  if (!first_mode)
	    first_mode = mode;
	  else if (!rtx_equal_p (first_mode, mode))
	    {
	      program = nullptr;
	      break;
	    }
	}
    }
  if (!program)
    {
      out->refusal = macro_desc_refusal_program_unproven;
      if (dump)
	fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		 out->refusal);
      return true;
    }

  /* WP9: a CC-template program must prove the full CC model -- the
     definition/merge/restore dataflow, the deferred-visibility slots,
     the launch-latched store mask, and the payload/store mode envelope
     -- before any word is packed.  */
  macro_cc_model cc_model;
  memset (&cc_model, 0, sizeof (cc_model));
  HOST_WIDE_INT cc_store_mode = 0;
  if (program->cc_select
      && !derive_cc_model (region, schedule, c, &cc_model, &cc_store_mode))
    {
      out->refusal = macro_desc_refusal_cc_template_unproved;
      if (dump)
	fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		 out->refusal);
      return true;
    }

  /* Templates.  */
  out->n_templates = program->n_templates;
  for (unsigned t = 0; t != program->n_templates; ++t)
    {
      const desc_template_rule &rule = program->templates[t];
      uint32_t word = 0;
      if (rule.pin_op >= 0 && rule.source_event >= 0)
	{
	  HOST_WIDE_INT pin = 0;
	  if (!const_operand (derived.value_insns[rule.source_event],
			      rule.pin_op, &pin)
	      || pin != (TARGET_XTT_TENSIX_WH ? rule.pin_wh : rule.pin_bh))
	    {
	      out->refusal = macro_desc_refusal_program_unproven;
	      if (dump)
		fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
			 out->refusal);
	      return true;
	    }
	}
      switch (rule.kind)
	{
	case TR_FIELDS_FROM_SOURCE:
	case TR_FIELDS_FROM_SOURCE_ROUTING_MOD:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT mod1 = 0, imm12 = 0;
	    if (!const_operand (src, rule.mod1_op, &mod1)
		|| (rule.imm12_op >= 0
		    && !const_operand (src, rule.imm12_op, &imm12)))
	      {
		out->refusal = macro_desc_refusal_encoding_failed;
		break;
	      }
	    if (rule.kind == TR_FIELDS_FROM_SOURCE_ROUTING_MOD
		&& second_set_reaches_store_p (src, derived.store_insn))
	      mod1 |= rule.routing_mod_bit;
	    template_spec spec;
	    spec.opcode = source_opcode_byte (src);
	    spec.imm12 = (uint16_t) (imm12 & 0xfff);
	    spec.src_c = rule.src_c_plan;
	    spec.dest_sel = 0xc + t;	/* positional routing selector */
	    spec.mod1 = (uint8_t) mod1;
	    if (!encode_template (c, spec, &word))
	      out->refusal = macro_desc_refusal_encoding_failed;
	  }
	  break;
	case TR_TABLE_FIELDS:
	  {
	    HOST_WIDE_INT imm12 = 0;
	    /* Encodability, both directions: any typed immediate the
	       12-bit field represents packs; anything else refuses --
	       never silently masks.  */
	    if (rule.imm12_op >= 0
		&& (!const_operand (derived.value_insns[rule.source_event],
				    rule.imm12_op, &imm12)
		    || imm12 < -2048 || imm12 > 2047))
	      {
		out->refusal = macro_desc_refusal_encoding_failed;
		break;
	      }
	    template_spec spec;
	    spec.opcode = TARGET_XTT_TENSIX_WH ? rule.opcode_wh
	      : rule.opcode_bh;
	    spec.imm12 = (uint16_t) (imm12 & 0xfff);
	    spec.src_c = rule.src_c_plan;
	    spec.dest_sel = 0xc + t;	/* positional routing selector */
	    spec.mod1 = rule.fixed_mod1;
	    if (!encode_template (c, spec, &word))
	      out->refusal = macro_desc_refusal_encoding_failed;
	  }
	  break;
	case TR_FIELDS_FROM_SOURCE_CC_SENSE:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT mod1 = 0;
	    if (!const_operand (src, rule.mod1_op, &mod1) || mod1 < 0)
	      {
		out->refusal = macro_desc_refusal_encoding_failed;
		break;
	      }
	    /* Proven envelope: the sense must be in the register-test
	       class whether or not it is complemented; the complement
	       mapping itself is capability-table data.  */
	    unsigned complemented = 0;
	    if (!rvtt_macro::sfpsetcc_complement_mod1 ((uint64_t) mod1,
						       &complemented))
	      {
		out->refusal = macro_desc_refusal_cc_template_unproved;
		break;
	      }
	    template_spec spec;
	    spec.opcode = source_opcode_byte (src);
	    spec.imm12 = 0;
	    spec.src_c = rule.src_c_plan;
	    spec.dest_sel = 0xc + t;	/* positional routing selector */
	    spec.mod1 = (uint8_t) (cc_model.complement ? complemented
				   : (unsigned) mod1);
	    if (!encode_template (c, spec, &word))
	      out->refusal = macro_desc_refusal_encoding_failed;
	  }
	  break;
	}
      if (out->refusal)
	{
	  if (dump)
	    fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		     out->refusal);
	  return true;
	}
      out->templ[t] = word;
    }

  /* Sequences and misc: proven whole words resolved from the matched
     program's provenance labels in the capability tables.  */
  out->n_seq = program->n_macros;
  for (unsigned m = 0; m != program->n_macros; ++m)
    if (!program->seq_names[m]
	|| !find_seq_word (c, program->seq_names[m], &out->seq[m]))
      {
	out->refusal = macro_desc_refusal_program_unproven;
	if (dump)
	  fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		   out->refusal);
	return true;
      }
  if (program->misc_from_store_mode)
    {
      /* Field-derived misc: the proven select rule 0x700 | StoreMod0,
	 packed from the row's shared payload/store data mode through
	 the capability tables.  */
      if (!rvtt_macro::encode_misc_select (c, (unsigned) cc_store_mode,
					   &out->misc))
	{
	  out->refusal = macro_desc_refusal_encoding_failed;
	  if (dump)
	    fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		     out->refusal);
	  return true;
	}
    }
  else if (!find_misc_word (c, program->misc_name, &out->misc))
    {
      out->refusal = macro_desc_refusal_program_unproven;
      if (dump)
	fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		 out->refusal);
      return true;
    }
  out->has_misc = true;
  out->keep_separator = program->keep_separator;
  out->cc = cc_model;

  /* Address-modifier SETC16 programs for the absorbed stride.  */
  if (schedule.absorbed_stride)
    {
      bool needs_bank_base = false;
      if (!addr_mod_program (c, schedule.absorbed_stride, out->setc16,
			     &out->n_setc16, &needs_bank_base))
	{
	  out->refusal = macro_desc_refusal_encoding_failed;
	  if (dump)
	    fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		     out->refusal);
	  return true;
	}
    }

  /* Launch tuples: one per macro carrier, addresses and data modes from
     the typed Dst operands, VD from the program plan.  */
  {
    /* Recover per-macro carried accesses from the schedule events.  */
    const macro_row &row = region.rows[0];
    for (unsigned m = 0; m != program->n_macros; ++m)
      {
	macro_launch_spec launch;
	memset (&launch, 0, sizeof (launch));
	launch.macro_index = m;
	bool have = false, is_store_only = true;
	for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	  {
	    const macro_event &ev = schedule.events[ix];
	    xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	    bool carried_load = e.dst_mem_read && ev.is_carrier
	      && ev.macro_index == m;
	    bool carried_store = e.dst_mem_write && ev.macro_index == m
	      && ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT;
	    if (!carried_load && !carried_store)
	      continue;
	    rtx address, mode, addr_mode;
	    if (!rvtt_dst_access_operands (row.insns[ix], e, &address,
					   &mode, &addr_mode)
		|| !CONST_INT_P (address) || !CONST_INT_P (mode))
	      continue;
	    if (carried_load)
	      {
		is_store_only = false;
		launch.address = UINTVAL (address);
		launch.mode = UINTVAL (mode);
		have = true;
	      }
	    else if (!have)
	      {
		launch.address = UINTVAL (address);
		launch.mode = UINTVAL (mode);
		have = true;
	      }
	  }
	if (!have)
	  continue;
	bool absorbs = schedule.absorbed_stride
	  && m == program->n_macros - 1;
	launch.addr_mode = absorbs ? c->auto_increment_dst2_addr_mode
	  : c->no_increment_addr_mode;
	if (program->fixed_vd >= 0)
	  {
	    launch.vd = program->fixed_vd;
	    launch.vd_alternates = false;
	  }
	else if (is_store_only)
	  {
	    launch.vd = program->store_only_vd;
	    launch.vd_alternates = false;
	  }
	else
	  {
	    launch.vd = 0;	/* alternating pair {0, 1}, lowest-free */
	    launch.vd_alternates = true;
	  }
	if (!encode_launch (c, m, launch.vd, launch.mode, launch.addr_mode,
			    launch.address, &launch.word)
	    || (launch.vd_alternates
		&& !encode_launch (c, m, launch.vd ^ 1, launch.mode,
				   launch.addr_mode, launch.address,
				   &launch.word_alt)))
	  {
	    out->refusal = macro_desc_refusal_encoding_failed;
	    if (dump)
	      fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		       out->refusal);
	    return true;
	  }
	out->launches.safe_push (launch);
      }
  }

  /* Drain: the matched whole-word program is proven end to end, so the
     proven-calendar drain applies (the generic greatest-remaining-delay
     rule evaluates to it for every proven program).  */
  out->drain_slots = c->proven_drain_slots;
  out->needs_all_lanes_prefix = region.net.cc_read;

  /* Planner-owned physical LREGs: the planned VDs, staging and source
     fields, plus every hidden write the proven templates carry.  */
  out->planned_lregs = region.internal_lregs;
  for (macro_launch_spec &launch : out->launches)
    {
      out->planned_lregs |= 1u << launch.vd;
      if (launch.vd_alternates)
	out->planned_lregs |= 1u << (launch.vd ^ 1);
    }
  for (unsigned t = 0; t != out->n_templates; ++t)
    out->planned_lregs |= template_hidden_lreg_writes (c, out->templ[t]);
  for (unsigned t = 0; t != program->n_templates; ++t)
    if (program->templates[t].src_c_plan)
      out->planned_lregs |= 1u << program->templates[t].src_c_plan;

  if (dump)
    {
      fprintf (dump,
	       "Macro-planner descriptor: templates=%u seq=%u misc=0x%08x"
	       " setc16=%u launches=%u drain=%d planned-lregs=0x%x"
	       " prefix=%s\n",
	       out->n_templates, out->n_seq, out->misc, out->n_setc16,
	       out->launches.length (), out->drain_slots,
	       out->planned_lregs,
	       out->needs_all_lanes_prefix ? "all-lanes" : "none");
      if (out->cc.active)
	fprintf (dump,
		 "Macro-planner descriptor-cc: sense=%s def-visible=%d"
		 " pre-load=%d post-load=%d store-latch=%d"
		 " restore-visible=%d interval=%d separator=kept\n",
		 out->cc.complement ? "complement" : "direct",
		 out->cc.def_visible_slot, out->cc.pre_load_slot,
		 out->cc.post_load_slot, out->cc.store_launch_slot,
		 out->cc.restore_visible_slot, out->cc.row_interval);
      for (unsigned t = 0; t != out->n_templates; ++t)
	fprintf (dump, "Macro-planner descriptor-word dest=%u: 0x%08x\n",
		 t, out->templ[t]);
      for (unsigned m = 0; m != out->n_seq; ++m)
	fprintf (dump, "Macro-planner descriptor-word dest=%u: 0x%08x\n",
		 4 + m, out->seq[m]);
      fprintf (dump, "Macro-planner descriptor-word dest=8: 0x%08x\n",
	       out->misc);
      for (unsigned s = 0; s != out->n_setc16; ++s)
	{
	  uint32_t word;
	  if (rvtt_macro::encode_setc16 (c, out->setc16[s].config_reg,
					 out->setc16[s].value, &word))
	    fprintf (dump, "Macro-planner descriptor-setc16: 0x%08x\n",
		     word);
	}
      for (macro_launch_spec &launch : out->launches)
	{
	  fprintf (dump, "Macro-planner descriptor-launch: macro=%u vd=%u"
		   " word=0x%08x", launch.macro_index, launch.vd,
		   launch.word);
	  if (launch.vd_alternates)
	    fprintf (dump, " alt-vd=%u alt-word=0x%08x", launch.vd ^ 1,
		     launch.word_alt);
	  fprintf (dump, "\n");
	}
    }
  return true;
}

/* ------------------------------------------------------------------ */
/* Verifier expectations: a second pass over the region's explicit
   facts, so the verifier compares the synthesized words against an
   independently assembled expectation set.

   LIMITATION (WP8 revisit): the template, sequence, and misc
   expectations are assembled from the SAME desc_program table that
   synthesis reads (derive_structure + find_program), so the verifier
   catches packing/encoding divergence but cannot catch a wrong table
   entry -- both sides would agree on the wrong words.  The cross-check
   for table wrongness is the frozen-oracle byte-parity suite (the
   periodic-minmax parities in manifest-loadmacro-family /
   manifest-inplace-minmax), which compares the planner's emission
   against independently recorded quarantined-pass words.  Launch words
   and SETC16 slot programs, by contrast, are re-derived here
   independently of synthesis's choices.  */
/* ------------------------------------------------------------------ */

#include "rvtt-macro-verify-core.h"

bool
rvtt_macro_build_expectations (const macro_region &region,
			       const macro_schedule &schedule,
			       rvtt_macro_verify::expectations *out)
{
  memset (out, 0, sizeof (*out));

  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? CPU_BH
    : TARGET_XTT_TENSIX_WH ? CPU_WH : CPU_QSR;
  const caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c)
    return false;

  derived_structure derived;
  if (!derive_structure (region, schedule, &derived))
    return false;
  const desc_program *program = find_program (derived);
  if (!program)
    return false;

  /* CC-template expectations (WP9), re-derived from the region's
     explicit facts.  */
  macro_cc_model cc_model;
  memset (&cc_model, 0, sizeof (cc_model));
  HOST_WIDE_INT cc_store_mode = 0;
  if (program->cc_select)
    {
      if (!derive_cc_model (region, schedule, c, &cc_model, &cc_store_mode))
	return false;
      out->cc.active = true;
      out->cc.complement = cc_model.complement;
      out->cc.def_visible_slot = cc_model.def_visible_slot;
      out->cc.pre_load_slot = cc_model.pre_load_slot;
      out->cc.post_load_slot = cc_model.post_load_slot;
      out->cc.store_launch_slot = cc_model.store_launch_slot;
      out->cc.restore_visible_slot = cc_model.restore_visible_slot;
      out->cc.row_interval = cc_model.row_interval;
    }

  /* Template expectations.  */
  out->n_templates = program->n_templates;
  for (unsigned t = 0; t != program->n_templates; ++t)
    {
      const desc_template_rule &rule = program->templates[t];
      rvtt_macro_verify::expect_template &e = out->templates[t];
      if (rule.pin_op >= 0 && rule.source_event >= 0)
	{
	  HOST_WIDE_INT pin = 0;
	  if (!const_operand (derived.value_insns[rule.source_event],
			      rule.pin_op, &pin)
	      || pin != (TARGET_XTT_TENSIX_WH ? rule.pin_wh : rule.pin_bh))
	    return false;
	}
      switch (rule.kind)
	{
	case TR_FIELDS_FROM_SOURCE:
	case TR_FIELDS_FROM_SOURCE_ROUTING_MOD:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT mod1 = 0, imm12 = 0;
	    if (!const_operand (src, rule.mod1_op, &mod1)
		|| (rule.imm12_op >= 0
		    && !const_operand (src, rule.imm12_op, &imm12)))
	      return false;
	    if (rule.kind == TR_FIELDS_FROM_SOURCE_ROUTING_MOD
		&& second_set_reaches_store_p (src, derived.store_insn))
	      mod1 |= rule.routing_mod_bit;
	    e.whole_word = false;
	    e.opcode = source_opcode_byte (src);
	    e.imm12 = (uint16_t) (imm12 & 0xfff);
	    e.dest_sel = 0xc + t;
	    e.mod1 = (uint8_t) mod1;
	  }
	  break;
	case TR_TABLE_FIELDS:
	  {
	    HOST_WIDE_INT imm12 = 0;
	    if (rule.imm12_op >= 0
		&& (!const_operand (derived.value_insns[rule.source_event],
				    rule.imm12_op, &imm12)
		    || imm12 < -2048 || imm12 > 2047))
	      return false;
	    e.whole_word = false;
	    e.opcode = TARGET_XTT_TENSIX_WH ? rule.opcode_wh
	      : rule.opcode_bh;
	    e.imm12 = (uint16_t) (imm12 & 0xfff);
	    e.dest_sel = 0xc + t;
	    e.mod1 = rule.fixed_mod1;
	  }
	  break;
	case TR_FIELDS_FROM_SOURCE_CC_SENSE:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT mod1 = 0;
	    if (!const_operand (src, rule.mod1_op, &mod1) || mod1 < 0)
	      return false;
	    unsigned complemented = 0;
	    if (!rvtt_macro::sfpsetcc_complement_mod1 ((uint64_t) mod1,
						       &complemented))
	      return false;
	    e.whole_word = false;
	    e.opcode = source_opcode_byte (src);
	    e.imm12 = 0;
	    e.dest_sel = 0xc + t;
	    e.mod1 = (uint8_t) (cc_model.complement ? complemented
				: (unsigned) mod1);
	  }
	  break;
	}
    }

  /* Sequence/misc expectations resolved from the capability tables.  */
  out->n_seq = program->n_macros;
  for (unsigned m = 0; m != program->n_macros; ++m)
    if (!program->seq_names[m]
	|| !find_seq_word (c, program->seq_names[m], &out->seq_words[m]))
      return false;
  if (program->misc_from_store_mode)
    {
      if (!rvtt_macro::encode_misc_select (c, (unsigned) cc_store_mode,
					   &out->misc))
	return false;
    }
  else if (!find_misc_word (c, program->misc_name, &out->misc))
    return false;
  out->check_misc = true;
  out->stride = schedule.absorbed_stride;

  /* Access expectations, macro order.  */
  const macro_row &row = region.rows[0];
  for (unsigned m = 0; m != program->n_macros; ++m)
    {
      rvtt_macro_verify::expect_access &a = out->accesses[out->n_accesses];
      a.macro_index = m;
      bool have = false, is_store_only = true;
      for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	{
	  const macro_event &ev = schedule.events[ix];
	  xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	  bool carried_load = e.dst_mem_read && ev.is_carrier
	    && ev.macro_index == m;
	  bool carried_store = e.dst_mem_write && ev.macro_index == m
	    && ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT;
	  if (!carried_load && !carried_store)
	    continue;
	  rtx address, mode, addr_mode;
	  if (!rvtt_dst_access_operands (row.insns[ix], e, &address, &mode,
					 &addr_mode)
	      || !CONST_INT_P (address) || !CONST_INT_P (mode))
	    continue;
	  if (carried_load)
	    {
	      is_store_only = false;
	      a.address = UINTVAL (address);
	      a.mode = UINTVAL (mode);
	      have = true;
	    }
	  else if (!have)
	    {
	      a.address = UINTVAL (address);
	      a.mode = UINTVAL (mode);
	      have = true;
	    }
	}
      if (!have)
	continue;
      bool absorbs = schedule.absorbed_stride && m == program->n_macros - 1;
      a.addr_mode = absorbs ? c->auto_increment_dst2_addr_mode
	: c->no_increment_addr_mode;
      a.vd = program->fixed_vd >= 0 ? (unsigned) program->fixed_vd
	: is_store_only ? program->store_only_vd : 0;
      ++out->n_accesses;
    }

  /* Planned registers, as synthesis plans them.  */
  out->planned_lregs = region.internal_lregs;
  for (unsigned ix = 0; ix != out->n_accesses; ++ix)
    {
      out->planned_lregs |= 1u << out->accesses[ix].vd;
      if (program->fixed_vd < 0 && out->accesses[ix].vd == 0)
	out->planned_lregs |= 2;	/* alternating pair */
    }
  for (unsigned t = 0; t != program->n_templates; ++t)
    if (program->templates[t].src_c_plan)
      out->planned_lregs |= 1u << program->templates[t].src_c_plan;
  return true;
}

/* Assembler-output helper for the field-operand rvtt_owned_setc16
   pattern (design 4.3): the pass hands the insn architectural fields;
   the word is packed here through the capability tables, never in pass
   code.  */

const char *
rvtt_output_owned_setc16 (rtx *operands)
{
  static char buffer[32];
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? CPU_BH : CPU_WH;
  const caps *c = rvtt_macro_caps_for_cpu (cpu);
  uint32_t word = 0;
  if (!c
      || !rvtt_macro::encode_setc16 (c, UINTVAL (operands[0]),
				     UINTVAL (operands[1]), &word))
    fatal_insn ("owned SETC16 fields not encodable", operands[0]);
  snprintf (buffer, sizeof (buffer), ".ttinsn\t%u", word);
  return buffer;
}
