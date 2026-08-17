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
#include "rvtt-macro-derive-core.h"

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
const char *macro_desc_refusal_cc_restore_store_race
  = "cc-restore-store-race";

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
  /* WP10 compact select program: matches only a schedule that absorbed
     the row stride into the trailing explicit load (the 3-slot
     handwritten protocol shape), and sources the delayed store's data
     mode from its carrying launch's mod0 (misc UsesLoadMod0ForStore)
     -- which obliges the definition carrier's load mode to equal the
     store mode (checked in derive_cc_model).  */
  bool require_absorbed_stride;
  bool misc_launch_mod0;
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
    false, false,
  },
  /* Predicated select, COMPACT calendar (WP10; the production
     handwritten Where protocol's 3-slot row, ckernel_sfpu_where.h
     "3 cycles per input row"): the definition carrier hosts the
     predicate template and the delayed store; the SECOND launch hosts
     the all-lanes restore; the trailing payload load stays explicit
     and absorbs the row stride through its own auto-increment address
     mode, so the typed separator is deleted rather than kept.  The
     store's data mode rides the launch (proven whole misc word
     "select-launch-mod0", UsesLoadMod0ForStore), which obliges the
     definition carrier's load mode to equal the store mode -- rows
     with a differently-typed condition keep the established 4-slot
     program above.  Matched only when the schedule absorbed the stride
     into the explicit load (require_absorbed_stride).  */
  {
    "predicated-select-compact (ckernel_sfpu_where.h 3-slot; WP10)",
    2,
    { { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSETCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSETCC (0, 0, 0, 0)) } }, true },
      { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPENCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPENCC (0, 0, 0, 0)) } }, false } },
    2,
    { { TR_FIELDS_FROM_SOURCE_CC_SENSE, 0, 0, 0, 0, 0,
	1 /* setcc mod1 operand */, -1, 0, -1, 0, 0 },
      { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPENCC (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPENCC (0, 0, 0, 0)), 0, 0, -1, -1, -1, 0,
	-1, 0, 0 } },
    { "select-m0", "select-m1-encc" },
    "select-launch-mod0",
    0, 0, false,
    true, false, false,
    true, true,
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

/* A program's template rules must be able to reach their source
   operands: an admitted instruction VARIANT with a different operand
   LAYOUT (e.g. the constant-register SFPSWAP forms, whose recog
   operand list is shorter than the binary-periodic swap_int layout's)
   is not the proven program and must not match it -- it falls through
   to the derived-calendar path or refuses, instead of failing inside
   the matched program's packer.  An operand that EXISTS but is not the
   encodable constant stays this program's shape and keeps the
   established encodability refusal (the WP8 dynamic-shift direction).  */

static bool
operand_exists (rtx_insn *insn, int pos)
{
  extract_insn (insn);
  return pos >= 0 && pos < recog_data.n_operands;
}

static bool
program_operands_reachable (const desc_program &p,
			    const derived_structure &derived)
{
  for (unsigned t = 0; t != p.n_templates; ++t)
    {
      const desc_template_rule &rule = p.templates[t];
      if (rule.source_event < 0
	  || (unsigned) rule.source_event >= derived.n_value_insns)
	continue;
      rtx_insn *src = derived.value_insns[rule.source_event];
      if (rule.mod1_op >= 0 && !operand_exists (src, rule.mod1_op))
	return false;
      if (rule.imm12_op >= 0 && !operand_exists (src, rule.imm12_op))
	return false;
      if (rule.pin_op >= 0 && !operand_exists (src, rule.pin_op))
	return false;
    }
  return true;
}

static const desc_program *
find_program (const derived_structure &derived, bool absorbed_into_explicit)
{
  bool is_wh = TARGET_XTT_TENSIX_WH;
  for (const desc_program &p : desc_programs)
    {
      if (p.n_macros != derived.n_macros)
	continue;
      /* The compact select program exists only for the schedule that
	 absorbed the stride into the trailing explicit load; every
	 other program only for schedules that did not.  */
      if (p.require_absorbed_stride != absorbed_into_explicit)
	continue;
      bool match = true;
      for (unsigned m = 0; m != p.n_macros && match; ++m)
	match = macro_key_matches (p.macros[m], derived.macros[m], is_wh);
      if (match && program_operands_reachable (p, derived))
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
   cc-template-unproved, or the specific name left in *REFUSAL when
   one obligation owns a sharper spelling; refusal paths never
   mutate).							      */
/* ------------------------------------------------------------------ */

static bool
derive_cc_model (const macro_region &region, const macro_schedule &schedule,
		 const rvtt_macro::caps *c, bool launch_mod0,
		 macro_cc_model *out, HOST_WIDE_INT *store_mode,
		 const char **refusal = nullptr)
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
      else if (e.cc_write && e.lreg_read && !e.lreg_write)
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

  /* The delayed store must execute after both payload loads have
     written the shared VD.  */
  if (store_exec <= last_slot)
    return false;

  /* The restore must not become visible before the predicated payload
     issues, and must be visible by the next row's first slot (the kept
     explicit separator provides that slot).  */
  if (restore_visible <= last_slot)
    return false;
  if (restore_visible > schedule.ii)
    return false;

  /* ARCHITECTURAL CONSTRAINT (silicon adjudication 2026-08-17 ->
     craq-sim 9f324140 -> this check): the store's lane predicate is
     the LIVE CC state at its execution cycle
     (store_lane_mask_live_at_execution) -- the launch never latches
     it -- and a CC write retiring in the store's own cycle is not yet
     visible to it.  The all-lanes restore must therefore retire
     STRICTLY BEFORE the store executes: restore_exec < store_exec,
     i.e. restore_visible (= restore_exec + lag, lag 1) <= store_exec.
     The 4-slot separator-kept select calendar violates this
     (restore_exec == store_exec == 3: the store retires under the
     SFPSETCC complement mask and leaves the true-branch lanes
     unwritten -- the deterministic BH silicon failure); the compact
     3-slot calendar satisfies it (restore_exec 2 < store_exec 3) and
     is silicon-correct.  Symmetrically the store must retire before
     the NEXT row's predicate definition executes (def_exec + ii with
     identical rows), or it would execute under that row's mask.  */
  if (!rvtt_macro::store_lane_mask_live_at_execution ())
    return false;
  if (restore_exec >= store_exec
      || store_exec >= def_exec + schedule.ii)
    {
      if (refusal)
	*refusal = macro_desc_refusal_cc_restore_store_race;
      return false;
    }

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
  /* Launch-sourced store mod0 (WP10 compact program,
     UsesLoadMod0ForStore): the delayed store's data mode is the
     carrying launch's own mod0, i.e. the definition carrier's load
     mode -- which must therefore equal the store mode.  The
     established program keeps the definition carrier's mode free (the
     misc word carries the store mode).  */
  if (launch_mod0)
    {
      xtt_effect_set def_src_e = rvtt_insn_effects (row.insns[def_src_ix]);
      if (!rvtt_dst_access_operands (row.insns[def_src_ix], def_src_e,
				     &address, &mode, &addr_mode)
	  || !CONST_INT_P (mode)
	  || INTVAL (mode) != st_mode)
	return false;
    }
  (void) c;

  out->active = true;
  out->def_visible_slot = def_visible;
  out->pre_load_slot = first_slot;
  out->post_load_slot = last_slot;
  out->store_exec_slot = store_exec;
  out->restore_visible_slot = restore_visible;
  out->row_interval = schedule.ii;
  *store_mode = st_mode;
  return true;
}

/* ------------------------------------------------------------------ */
/* Timing-calendar derivation (Layer 4b): when no proven whole-word
   program matches, derive the sequence words, delays, and misc fields
   from the schedule and the established architectural facts
   (docs/TIMING_CALENDAR_DERIVATION.md §4).  The admitted template
   class grows one CRAQ-validated increment at a time; today it is the
   constant-register SFPSWAP family (the unary max/min shape).  Rows
   outside the admitted class keep the established
   descriptor-program-unproven refusal byte-identically.	      */
/* ------------------------------------------------------------------ */

/* Architectural index of a hardware constant register operand, or -1.
   Constant registers appear as (unspec [(const_int L)] SFPCSTLREG) --
   the L index is printed as L%d by the assembler -- or, defensively,
   as a hard SFPU register in the constant range L8..L15.  */

static int
cstlreg_index (rtx x)
{
  if (GET_CODE (x) == UNSPEC && XINT (x, 1) == UNSPEC_SFPCSTLREG
      && CONST_INT_P (XVECEXP (x, 0, 0)))
    {
      HOST_WIDE_INT idx = INTVAL (XVECEXP (x, 0, 0));
      return idx >= SFPU_CREG_IDX_LWM && idx <= 15 ? (int) idx : -1;
    }
  if (REG_P (x) && REGNO (x) >= SFPU_REG_FIRST + SFPU_CREG_IDX_LWM
      && REGNO (x) <= SFPU_REG_FIRST + 15)
    return (int) (REGNO (x) - SFPU_REG_FIRST);
  return -1;
}

/* Analyze a constant-register SFPSWAP variant: the admitted derived
   template class.  The single-result patterns carry a variant marker
   as the unspec's last element (1 = constant in the VC position,
   2 = constant in the VD position); the template realization always
   places the launch value in the VD position and the constant in VC,
   so the VD-position variant takes the architecturally complementary
   result-routing mod (bit 8 -- the same routing bit the frozen minmax
   selection used, LM:781-786).  The proven envelope is the
   full-vector min/max class: post-mapping mod1 in {1, 9}; sub-vector
   modes and the dual-constant variant refuse.  */

static bool
swap_cst_template_fields (rtx_insn *insn, uint8_t *src_c, uint8_t *mod1)
{
  if (insn_unspecv (insn) != UNSPECV_SFPSWAP)
    return false;
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) != SET)
    return false;		/* dual-result / dual-constant forms   */
  rtx un = SET_SRC (pat);
  if (GET_CODE (un) != UNSPEC_VOLATILE || XVECLEN (un, 0) != 4)
    return false;
  rtx a = XVECEXP (un, 0, 0);
  rtx b = XVECEXP (un, 0, 1);
  rtx mod = XVECEXP (un, 0, 2);
  rtx marker = XVECEXP (un, 0, 3);
  if (!CONST_INT_P (mod) || !CONST_INT_P (marker))
    return false;
  HOST_WIDE_INT m = INTVAL (mod);
  int cst;
  switch (INTVAL (marker))
    {
    case 1:			/* live VD position, constant VC       */
      cst = cstlreg_index (b);
      break;
    case 2:			/* constant VD position, live VC:
				   surviving result is the VC-position
				   one; the template keeps the live
				   value in VD, so the result routing
				   flips.  */
      cst = cstlreg_index (a);
      m ^= 8;
      break;
    default:
      return false;
    }
  if (cst < 0 || (m != 1 && m != 9))
    return false;
  *src_c = (uint8_t) cst;
  *mod1 = (uint8_t) m;
  return true;
}

/* Shared derived synthesis state: the row_spec fed to the derivation
   core, the derived calendar, and the packed template fields.  */

struct derived_synthesis
{
  rvtt_macro_derive::row_spec row;
  rvtt_macro_derive::derived_calendar cal;
  uint8_t template_opcode[4];
  uint8_t template_src_c[4];
  uint8_t template_mod1[4];
  unsigned n_templates;
  int store_macro;
  unsigned store_mode;
  bool store_uses_carrier_mode;
  unsigned store_only_vd;
  const char *refusal;
};

/* Build the derivation-core row description from the region's explicit
   facts and the schedule, gated on the admitted template class.
   Returns false with DS->refusal null when the row is simply outside
   the admitted class (the caller keeps the established refusal).  */

static bool
derive_row (const macro_region &region, const macro_schedule &schedule,
	    const rvtt_macro::caps *c, derived_synthesis *ds)
{
  using namespace rvtt_macro_derive;
  memset (ds, 0, sizeof (*ds));
  ds->store_macro = -1;
  ds->row.store_producer = -1;
  ds->row.store_input_last_slot = -1;
  ds->row.store_vd_next_write = -1;
  ds->row.store_event = -1;

  const macro_row &row = region.rows[0];

  /* Owned configuration destinations bound the derived resources.  */
  unsigned max_templates = 0, max_macros = 0;
  for (unsigned d = 0; d < 4; ++d)
    if ((c->owned_config_dests >> d) & 1)
      ++max_templates;
  for (unsigned d = 4; d < 8; ++d)
    if ((c->owned_config_dests >> d) & 1)
      ++max_macros;
  ds->row.max_templates = max_templates;
  ds->row.max_macros = max_macros;

  /* Per-register last writer while walking the row in program order:
     -1 none, -2 a load (issued), else the value-event index.  */
  int last_writer[16];
  int last_writer_slot[16];
  for (unsigned r = 0; r < 16; ++r)
    {
      last_writer[r] = -1;
      last_writer_slot[r] = -1;
    }
  /* Carrier-load LREG mask per macro.  */
  uint32_t carrier_load_regs[4] = {};
  int n_macros = 0;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if ((e.dst_mem_read || e.dst_mem_write) && ev.is_carrier
	  && ev.macro_index < 4)
	{
	  if (e.dst_mem_read)
	    carrier_load_regs[ev.macro_index] |= e.lreg_write;
	  ds->row.macro_slot[ev.macro_index] = ev.slot;
	  if ((int) ev.macro_index + 1 > n_macros)
	    n_macros = ev.macro_index + 1;
	}
      if ((e.dst_mem_write || (ev.realization
			       == macro_event::LAUNCHED_TEMPLATE_SLOT))
	  && ev.macro_index < 4
	  && (int) ev.macro_index + 1 > n_macros)
	n_macros = ev.macro_index + 1;
    }
  ds->row.n_macros = (unsigned) n_macros;

  unsigned n_ev = 0;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      bool is_load = e.dst_mem_read;
      bool is_store = e.dst_mem_write;
      bool launched_value = ev.realization
	== macro_event::LAUNCHED_TEMPLATE_SLOT && !ev.is_store;
      bool launched_store = is_store
	&& ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT;

      if (launched_value)
	{
	  /* Admitted derived template class: constant-register SFPSWAP.  */
	  uint8_t src_c = 0, mod1 = 0;
	  if (!swap_cst_template_fields (row.insns[ix], &src_c, &mod1))
	    return false;	/* outside the admitted class	       */
	  if (n_ev == MAX_EVENTS || ds->n_templates == 4)
	    return false;
	  event_spec &spec = ds->row.events[n_ev];
	  spec.opcode = source_opcode_byte (row.insns[ix]);
	  spec.is_store = false;
	  spec.macro_index = ev.macro_index;
	  spec.carrier_slot = ds->row.macro_slot[ev.macro_index];
	  spec.dep_mask = 0;
	  spec.latest_issued_input_slot = -1;
	  spec.reads_carrier_vd_reg
	    = (e.lreg_read & carrier_load_regs[ev.macro_index]) != 0;
	  spec.planned_src_c = src_c;
	  for (unsigned r = 0; r < 16; ++r)
	    if ((e.lreg_read >> r) & 1)
	      {
		if (last_writer[r] >= 0)
		  spec.dep_mask |= 1u << last_writer[r];
		else if (last_writer[r] == -2
			 && last_writer_slot[r]
			    > spec.latest_issued_input_slot)
		  spec.latest_issued_input_slot = last_writer_slot[r];
	      }
	  /* Constant-register sources are architectural constants, not
	     dataflow inputs.  */
	  ds->template_opcode[ds->n_templates] = spec.opcode;
	  ds->template_src_c[ds->n_templates] = src_c;
	  ds->template_mod1[ds->n_templates] = mod1;
	  ++ds->n_templates;
	  for (unsigned r = 0; r < 16; ++r)
	    if ((e.lreg_write >> r) & 1)
	      last_writer[r] = (int) n_ev;
	  ++n_ev;
	}
      else if (launched_store)
	{
	  if (n_ev == MAX_EVENTS || ds->row.store_event >= 0)
	    return false;
	  event_spec &spec = ds->row.events[n_ev];
	  spec.opcode = 0;
	  spec.is_store = true;
	  spec.macro_index = ev.macro_index;
	  spec.carrier_slot = ds->row.macro_slot[ev.macro_index];
	  spec.dep_mask = 0;
	  spec.latest_issued_input_slot = -1;
	  spec.reads_carrier_vd_reg = false;
	  spec.planned_src_c = 0;
	  for (unsigned r = 0; r < 16; ++r)
	    if (((e.lreg_read >> r) & 1) && last_writer[r] >= 0)
	      {
		spec.dep_mask |= 1u << last_writer[r];
		ds->row.store_producer = last_writer[r];
	      }
	  ds->row.store_event = (int) n_ev;
	  ds->store_macro = ev.macro_index;
	  rtx address, mode, addr_mode;
	  if (!rvtt_dst_access_operands (row.insns[ix], e, &address, &mode,
					 &addr_mode)
	      || !CONST_INT_P (mode) || UINTVAL (mode) > 0xf)
	    return false;
	  ds->store_mode = (unsigned) UINTVAL (mode);
	  ++n_ev;
	}
      else if (is_load)
	{
	  /* Carrier or explicit load: an issued write.  */
	  for (unsigned r = 0; r < 16; ++r)
	    if ((e.lreg_write >> r) & 1)
	      {
		last_writer[r] = -2;
		last_writer_slot[r] = ev.slot;
	      }
	  if (!ev.is_carrier && ev.realization == macro_event::EXPLICIT_INSN
	      && ds->row.n_explicits < 8)
	    {
	      ds->row.explicits[ds->row.n_explicits].slot = ev.slot;
	      ds->row.explicits[ds->row.n_explicits].unit_mask = 0;
	      ++ds->row.n_explicits;
	    }
	}
      else if (ev.realization == macro_event::EXPLICIT_INSN)
	{
	  /* An explicit compute issue occupies its architectural
	     sub-unit in its issue cycle (the ISA discard rule).  */
	  unsigned mask = 0;
	  switch (e.subunit)
	    {
	    case XTT_SU_SIMPLE:
	      mask = 1u << rvtt_macro::SEQ_UNIT_SIMPLE;
	      break;
	    case XTT_SU_MAD:
	      mask = 1u << rvtt_macro::SEQ_UNIT_MAD;
	      break;
	    case XTT_SU_ROUND:
	      mask = 1u << rvtt_macro::SEQ_UNIT_ROUND;
	      break;
	    case XTT_SU_STORE:
	      mask = 1u << rvtt_macro::SEQ_UNIT_STORE;
	      break;
	    default:
	      mask = 0;
	      break;
	    }
	  if (ds->row.n_explicits < 8)
	    {
	      ds->row.explicits[ds->row.n_explicits].slot = ev.slot;
	      ds->row.explicits[ds->row.n_explicits].unit_mask = mask;
	      ++ds->row.n_explicits;
	    }
	  for (unsigned r = 0; r < 16; ++r)
	    if ((e.lreg_write >> r) & 1)
	      {
		last_writer[r] = -2;
		last_writer_slot[r] = ev.slot;
	      }
	}
      else
	return false;		/* coalesced/CC rows are the proven
				   select program's territory	       */
    }
  ds->row.n_events = n_ev;
  if (ds->row.store_event < 0 || ds->row.store_producer < 0
      || ds->n_templates == 0)
    return false;

  ds->row.ii = schedule.ii;
  ds->row.last_issue_slot = schedule.ii - 1;
  ds->row.vd_alternates = schedule.alternating_vd;
  /* An unabsorbed separator occupies the last issue slot and is not an
     SFPU-class instruction.  */
  bool kept_separator = row.separator && !schedule.absorbed_stride;
  ds->row.window_all_sfpu = !kept_separator;
  if (kept_separator && ds->row.n_explicits < 8)
    {
      ds->row.explicits[ds->row.n_explicits].slot = schedule.ii - 1;
      ds->row.explicits[ds->row.n_explicits].unit_mask = 0;
      ++ds->row.n_explicits;
    }

  /* Store mod0 source: the store-carrying launch encodes the mode of
     the access it carries, so a store-only carrier (or a merged
     carrier whose load shares the store's mode) takes the launch's
     Mod0; otherwise the misc StoreMod0 nibble carries it.  */
  ds->store_uses_carrier_mode = true;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.dst_mem_read && ev.is_carrier
	  && ev.macro_index == (unsigned) ds->store_macro)
	{
	  rtx address, mode, addr_mode;
	  if (rvtt_dst_access_operands (row.insns[ix], e, &address, &mode,
					&addr_mode)
	      && CONST_INT_P (mode))
	    ds->store_uses_carrier_mode
	      = UINTVAL (mode) == ds->store_mode;
	}
    }

  /* The store-only carrier's sacrificial VD: the lowest physical LREG
     outside the alternating pair and the row's internal registers.  */
  {
    uint32_t used = region.internal_lregs | 0x3u;
    unsigned vd = 2;
    while (vd < 8 && ((used >> vd) & 1))
      ++vd;
    if (vd == 8)
      return false;
    ds->store_only_vd = vd;
  }

  if (!rvtt_macro_derive::derive_calendar (c, ds->row, &ds->cal))
    {
      ds->refusal = ds->cal.refusal;
      return false;
    }
  /* Stride absorption in a DERIVED calendar is a per-CPU proven
     envelope (rvtt-macro-tables.cc derived_stride_absorption_proven):
     the WH launch auto-increment/dual-slot machinery is the open WH
     Dst-advance frontier and refuses by name.  */
  if (schedule.absorbed_stride
      && !rvtt_macro::derived_stride_absorption_proven (c))
    {
      ds->refusal = "derived-stride-absorption-unproven";
      return false;
    }
  if (ds->cal.has_staging_copy)
    {
      rvtt_macro::staging_copy_facts copy;
      if (!rvtt_macro::staging_copy_realization (c, &copy)
	  || ds->cal.staging_template_index != (int) ds->n_templates)
	{
	  ds->refusal = rvtt_macro_derive::refusal_store_source ();
	  return false;
	}
      ds->template_opcode[ds->n_templates] = copy.opcode;
      ds->template_src_c[ds->n_templates] = 0;
      ds->template_mod1[ds->n_templates] = copy.mod1;
      ++ds->n_templates;
    }
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

  /* A schedule that named its own blocker is not a synthesis input:
     the candidate is unproven and the search advances.  TWO documented
     carve-outs (union of WP10 and the timing-calendar derivation):
     event-delay-unproven (NOTES 9(g), docs/MACRO_PLANNER.md Sec. 6 --
     an unproven per-event delay does not block a whole-word program
     proven end to end) and sequence-encoding-unproven (the missing
     proven sequence program is exactly what Layer 4b derives,
     docs/TIMING_CALENDAR_DERIVATION.md).  Every other refusal --
     including the compact candidate's mandatory-absorption failure
     (WP10), whose partially-compact structure must never fall through
     to a program keyed for a different calendar, and the physical
     latency/port violations no descriptor can repair -- stands.  */
  if (schedule.refusal
      && schedule.refusal != macro_sched_refusal_event_delay_unproven
      && schedule.refusal != macro_sched_refusal_sequence_encoding_unproven)
    return false;

  derived_structure derived;
  const desc_program *program = nullptr;
  if (derive_structure (region, schedule, &derived))
    program = find_program (derived, schedule.absorb_into_explicit);
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
      /* Layer 4b: no proven whole-word program -- derive the calendar
	 from the schedule and the established architectural facts.
	 Rows outside the admitted derived template class (or failing
	 any derivation obligation) refuse by name.  */
      derived_synthesis ds;
      if (derive_row (region, schedule, c, &ds))
	{
	  /* Templates: the admitted source events' packed fields plus
	     the staging copy; positional dest selectors.  */
	  out->n_templates = ds.n_templates;
	  for (unsigned t = 0; t != ds.n_templates; ++t)
	    {
	      template_spec spec;
	      spec.opcode = ds.template_opcode[t];
	      spec.imm12 = 0;
	      spec.src_c = ds.template_src_c[t];
	      spec.dest_sel = 0xc + t;
	      spec.mod1 = ds.template_mod1[t];
	      if (!encode_template (c, spec, &out->templ[t]))
		{
		  out->refusal = macro_desc_refusal_encoding_failed;
		  if (dump)
		    fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
			     out->refusal);
		  return true;
		}
	    }

	  /* Derived sequence words and misc fields.  */
	  out->n_seq = ds.row.n_macros;
	  for (unsigned m = 0; m != ds.row.n_macros; ++m)
	    out->seq[m] = ds.cal.seq_words[m];
	  out->misc = rvtt_macro::encode_misc_fields
	    (ds.store_uses_carrier_mode ? 0 : ds.store_mode,
	     ds.store_uses_carrier_mode ? 1u << ds.store_macro : 0,
	     ds.cal.delay_kind_mask);
	  out->has_misc = true;

	  if (schedule.absorbed_stride)
	    {
	      bool needs_bank_base = false;
	      if (!addr_mod_program (c, schedule.absorbed_stride,
				     out->setc16, &out->n_setc16,
				     &needs_bank_base))
		{
		  out->refusal = macro_desc_refusal_encoding_failed;
		  if (dump)
		    fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
			     out->refusal);
		  return true;
		}
	    }

	  /* Launch tuples: value carriers alternate the {0,1} VD pair;
	     store-only carriers take the derived sacrificial VD.  */
	  const macro_row &drow = region.rows[0];
	  for (unsigned m = 0; m != ds.row.n_macros; ++m)
	    {
	      macro_launch_spec launch;
	      memset (&launch, 0, sizeof (launch));
	      launch.macro_index = m;
	      bool have = false, is_store_only = true;
	      for (unsigned ix = 0; ix != drow.insns.length (); ++ix)
		{
		  const macro_event &ev = schedule.events[ix];
		  xtt_effect_set e = rvtt_insn_effects (drow.insns[ix]);
		  bool carried_load = e.dst_mem_read && ev.is_carrier
		    && ev.macro_index == m;
		  bool carried_store = e.dst_mem_write && ev.macro_index == m
		    && ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT;
		  if (!carried_load && !carried_store)
		    continue;
		  rtx address, mode, addr_mode;
		  if (!rvtt_dst_access_operands (drow.insns[ix], e, &address,
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
		&& m == ds.row.n_macros - 1;
	      launch.addr_mode = absorbs ? c->auto_increment_dst2_addr_mode
		: c->no_increment_addr_mode;
	      if (is_store_only)
		{
		  launch.vd = ds.store_only_vd;
		  launch.vd_alternates = false;
		}
	      else
		{
		  launch.vd = 0;
		  launch.vd_alternates = true;
		}
	      if (!encode_launch (c, m, launch.vd, launch.mode,
				  launch.addr_mode, launch.address,
				  &launch.word)
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

	  out->drain_slots = ds.cal.drain;
	  out->needs_all_lanes_prefix = region.net.cc_read;
	  out->keep_separator = false;

	  out->planned_lregs = region.internal_lregs | 0x3u
	    | (1u << ds.store_only_vd);
	  for (unsigned t = 0; t != out->n_templates; ++t)
	    out->planned_lregs |= template_hidden_lreg_writes (c,
							       out->templ[t]);

	  if (dump)
	    {
	      fprintf (dump,
		       "Macro-planner descriptor: derived-calendar"
		       " events=%u staging=%s drain=%d kind-mask=0x%x\n",
		       ds.row.n_events,
		       ds.cal.has_staging_copy ? "copy" : "none",
		       ds.cal.drain, ds.cal.delay_kind_mask);
	      fprintf (dump,
		       "Macro-planner descriptor: templates=%u seq=%u"
		       " misc=0x%08x setc16=%u launches=%u drain=%d"
		       " planned-lregs=0x%x prefix=%s\n",
		       out->n_templates, out->n_seq, out->misc,
		       out->n_setc16, out->launches.length (),
		       out->drain_slots, out->planned_lregs,
		       out->needs_all_lanes_prefix ? "all-lanes" : "none");
	      for (unsigned t = 0; t != out->n_templates; ++t)
		fprintf (dump,
			 "Macro-planner descriptor-word dest=%u: 0x%08x\n",
			 t, out->templ[t]);
	      for (unsigned m = 0; m != out->n_seq; ++m)
		fprintf (dump,
			 "Macro-planner descriptor-word dest=%u: 0x%08x\n",
			 4 + m, out->seq[m]);
	      fprintf (dump, "Macro-planner descriptor-word dest=8: 0x%08x\n",
		       out->misc);
	      for (unsigned s = 0; s != out->n_setc16; ++s)
		{
		  uint32_t word;
		  if (rvtt_macro::encode_setc16 (c, out->setc16[s].config_reg,
						 out->setc16[s].value, &word))
		    fprintf (dump, "Macro-planner descriptor-setc16:"
			     " 0x%08x\n", word);
		}
	      for (macro_launch_spec &launch : out->launches)
		{
		  fprintf (dump, "Macro-planner descriptor-launch: macro=%u"
			   " vd=%u word=0x%08x", launch.macro_index,
			   launch.vd, launch.word);
		  if (launch.vd_alternates)
		    fprintf (dump, " alt-vd=%u alt-word=0x%08x",
			     launch.vd ^ 1, launch.word_alt);
		  fprintf (dump, "\n");
		}
	    }
	  return true;
	}
      out->refusal = ds.refusal ? ds.refusal
	: macro_desc_refusal_program_unproven;
      if (dump)
	fprintf (dump, "Macro-planner descriptor-refusal: %s\n",
		 out->refusal);
      return true;
    }

  /* WP9: a CC-template program must prove the full CC model -- the
     definition/merge/restore dataflow, the deferred-visibility slots,
     the live-mask restore-before-store-execution constraint, and the
     payload/store mode envelope -- before any word is packed.  */
  macro_cc_model cc_model;
  memset (&cc_model, 0, sizeof (cc_model));
  HOST_WIDE_INT cc_store_mode = 0;
  const char *cc_refusal = nullptr;
  if (program->cc_select
      && !derive_cc_model (region, schedule, c, program->misc_launch_mod0,
			   &cc_model, &cc_store_mode, &cc_refusal))
    {
      out->refusal = cc_refusal ? cc_refusal
	: macro_desc_refusal_cc_template_unproved;
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
	  && !schedule.absorb_into_explicit && m == program->n_macros - 1;
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
		 " pre-load=%d post-load=%d store-exec=%d"
		 " restore-visible=%d interval=%d separator=%s\n",
		 out->cc.complement ? "complement" : "direct",
		 out->cc.def_visible_slot, out->cc.pre_load_slot,
		 out->cc.post_load_slot, out->cc.store_exec_slot,
		 out->cc.restore_visible_slot, out->cc.row_interval,
		 out->keep_separator ? "kept" : "absorbed");
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
  const desc_program *program = find_program (derived, schedule.absorb_into_explicit);
  if (!program)
    {
      /* Derived-calendar expectations (Layer 4b): re-run the shared
	 derivation from the region's explicit facts.  The same-table
	 limitation stated above applies; the independent cross-check
	 for the derivation itself is the standalone reproduction suite
	 rvtt-macro-derive-test.cc, which pins the algorithm against
	 independently recorded frozen words.  */
      derived_synthesis ds;
      if (!derive_row (region, schedule, c, &ds))
	return false;
      out->n_templates = ds.n_templates;
      for (unsigned t = 0; t != ds.n_templates; ++t)
	{
	  rvtt_macro_verify::expect_template &e = out->templates[t];
	  e.whole_word = false;
	  e.opcode = ds.template_opcode[t];
	  e.imm12 = 0;
	  e.dest_sel = 0xc + t;
	  e.mod1 = ds.template_mod1[t];
	}
      out->n_seq = ds.row.n_macros;
      for (unsigned m = 0; m != ds.row.n_macros; ++m)
	out->seq_words[m] = ds.cal.seq_words[m];
      out->misc = rvtt_macro::encode_misc_fields
	(ds.store_uses_carrier_mode ? 0 : ds.store_mode,
	 ds.store_uses_carrier_mode ? 1u << ds.store_macro : 0,
	 ds.cal.delay_kind_mask);
      out->check_misc = true;
      out->stride = schedule.absorbed_stride;

      const macro_row &row = region.rows[0];
      for (unsigned m = 0; m != ds.row.n_macros; ++m)
	{
	  rvtt_macro_verify::expect_access &a
	    = out->accesses[out->n_accesses];
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
	      if (!rvtt_dst_access_operands (row.insns[ix], e, &address,
					     &mode, &addr_mode)
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
	  bool absorbs = schedule.absorbed_stride
	    && m == ds.row.n_macros - 1;
	  a.addr_mode = absorbs ? c->auto_increment_dst2_addr_mode
	    : c->no_increment_addr_mode;
	  a.vd = is_store_only ? ds.store_only_vd : 0;
	  ++out->n_accesses;
	}

      out->planned_lregs = region.internal_lregs | 0x3u
	| (1u << ds.store_only_vd);
      /* Hidden template writes are covered through the synthesized
	 words in verify(); mirror the ownership expectation here.  */
      for (unsigned t = 0; t != ds.n_templates; ++t)
	{
	  template_spec spec;
	  spec.opcode = ds.template_opcode[t];
	  spec.imm12 = 0;
	  spec.src_c = ds.template_src_c[t];
	  spec.dest_sel = 0xc + t;
	  spec.mod1 = ds.template_mod1[t];
	  uint32_t word = 0;
	  if (encode_template (c, spec, &word))
	    out->planned_lregs |= template_hidden_lreg_writes (c, word);
	}
      return true;
    }

  /* CC-template expectations (WP9), re-derived from the region's
     explicit facts.  */
  macro_cc_model cc_model;
  memset (&cc_model, 0, sizeof (cc_model));
  HOST_WIDE_INT cc_store_mode = 0;
  if (program->cc_select)
    {
      if (!derive_cc_model (region, schedule, c, program->misc_launch_mod0,
			    &cc_model, &cc_store_mode))
	return false;
      out->cc.active = true;
      out->cc.complement = cc_model.complement;
      out->cc.def_visible_slot = cc_model.def_visible_slot;
      out->cc.pre_load_slot = cc_model.pre_load_slot;
      out->cc.post_load_slot = cc_model.post_load_slot;
      out->cc.store_exec_slot = cc_model.store_exec_slot;
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
      bool absorbs = schedule.absorbed_stride
	&& !schedule.absorb_into_explicit && m == program->n_macros - 1;
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
