/* Macro-planner descriptor synthesis.
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

/* Descriptor synthesis for the macro planner (see
   rtl-rvtt-macro-planner.cc for the other parts): turn a region into the
   SFPLOADMACRO descriptor program (instruction templates, sequence words,
   misc word, launches) that the hardware will execute.

   A region's canonical row is reduced to its event structure: the value
   events each launch hosts, with their sub-units and opcode bytes, and
   where the store goes.  This structure is matched against a small set of
   verified whole-word programs (desc_programs).  If none matches, the
   calendar is derived from the schedule instead (derive_row, using
   rvtt-macro-derive-core.h) for the instruction classes that
   derived_value_template_fields supports.  Anything else is refused by
   name.  Operation names and coefficient values are never looked at, so
   unrelated kernels with the same row structure get the same
   descriptor.  */

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
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-derive-core.h"
#include "dominance.h"
#include "rvtt-macro-epoch.h"

/* Refusal names used by descriptor synthesis.  Raw whole words come from
   the capability tables; template fields are packed from the source
   operands where the field layout is known, and kept as whole words where
   only the whole word has been verified.  */

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

static bool swap_cst_template_fields (rtx_insn *, uint8_t *, uint8_t *);

/* Return the unspec number of INSN's pattern, or -1.  */

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

/* Return the opcode byte of INSN's instruction on the current CPU, from
   the TT_OP encoding tables, or 0 if it is not one of the instructions
   the programs below use.  */

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
/* Verified descriptor programs, keyed by event structure and
   encoding.  */
/* ------------------------------------------------------------------ */

enum templ_rule_kind
{
  /* Pack opcode/imm12/src/mod1 from the source instruction; dest_sel is
     0xC + template index.  */
  TR_FIELDS_FROM_SOURCE,
  /* As above, plus the result-routing bit: when the source's second
     result reaches the store, mod1 gains the routing bit (SFPSWAP mod1
     1 -> 9).  */
  TR_FIELDS_FROM_SOURCE_ROUTING_MOD,
  /* Pack the template from table data: the opcode byte from the TT_OP
     encoding tables, a fixed mod1 and src field, the positional dest
     selector and, when imm12_op >= 0, the imm12 field from the source's
     immediate operand (the explicit shift amount).  Used for templates
     whose event is internal to the program (no source instruction) or
     whose opcode differs from the source's (the SHFT2 immediate template
     that realizes an explicit SFPSHFT).  */
  TR_TABLE_FIELDS,
  /* CC-template packing: the opcode comes from the source predicate
     definition, and mod1 from its predicate-sense operand.  When the
     payload load issued after the predicate becomes visible carries the
     merge's live operand, the template uses the complement of the source
     sense (sfpsetcc_complement_mod1).  The sense must be a register test
     either way; immediate and force-false predicates are refused.  */
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
  uint8_t fixed_mod1;		/* TR_TABLE_FIELDS: fixed mod1 field   */
  uint8_t src_c_plan;		/* planned physical src field	       */
  int source_event;		/* flat value-event index; -1 internal */
  int mod1_op;			/* source operand carrying mod1	       */
  int imm12_op;			/* source operand carrying imm12; -1   */
  uint8_t routing_mod_bit;	/* TR_.._ROUTING_MOD only	       */
  /* Source operand PIN_OP must equal the per-CPU value pin_wh/pin_bh,
     otherwise synthesis refuses with program-unproven.  Used where the
     operand has no template field (so only the verified constant is
     allowed) or where only one explicit-mode -> template-word pair is
     verified.  -1 = no pin.  */
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
  /* Labels of the per-macro sequence programs and the misc word in the
     capability tables, which hold the raw words.  Matching uses only the
     macro keys above; the labels are just indices into the tables.  */
  const char *seq_names[2];
  const char *misc_name;
  int fixed_vd;			/* -1: alternating pair {0,1}	       */
  unsigned store_only_vd;	/* VD of a store-only carrier	       */
  /* The program has only been verified with one data mode across every
     Dst access of the row.  */
  bool uniform_mode_required;
  /* CC-template program: the row has a predicate definition, a coalesced
     lane merge and the all-lanes restore.  Synthesis derives and checks
     the macro_cc_model, packs the misc word from the store's data mode
     (encode_misc_select) instead of using a fixed word, and keeps the
     row's explicit separator, whose issue slot is where the restore
     becomes visible.  */
  bool cc_select;
  bool misc_from_store_mode;
  bool keep_separator;
  /* Compact select program: matches only a schedule that absorbed the
     row stride into the trailing explicit load (the 3-slot shape used by
     the library), and takes the delayed store's data mode from its
     launch's mod0 (misc UsesLoadMod0ForStore), which requires the
     definition carrier's load mode to equal the store mode (checked in
     derive_cc_model).  */
  bool require_absorbed_stride;
  bool misc_launch_mod0;
};

#define OPB_WH(M) ((uint8_t) ((M) >> 24))

static const desc_program desc_programs[] = {
  /* Binary periodic select-store (the Min/Max program).  Macro 0 hosts
     one Simple source event; macro 1 carries the delayed store; the
     transient copy inside macro 1 is part of the whole-word program, not a
     derived event.  */
  {
    "binary-periodic (LM:781-786,873-887,1022-1054)",
    2,
    { { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSWAP (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSWAP (0, 0, 0, 0)) } }, false },
      { 0, {}, true } },
    2,
    /* Template 1 is the macro-internal SHFT2 copy into LReg16: opcode
       from the TT_OP tables, mod1 6.  */
    { { TR_FIELDS_FROM_SOURCE_ROUTING_MOD, 0, 0, 0, 2 /* planned RHS L2 */,
	0, 4 /* swap mod1 operand */, -1, 8, -1, 0, 0 },
      { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPSHFT2 (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPSHFT2 (0, 0, 0, 0)), 6, 0, -1, -1, -1, 0,
	-1, 0, 0 } },
    { "minmax-binary-m0", "minmax-binary-m1" },
    "minmax-binary",
    -1, 3, true,
    false, false, false,
    false, false,
  },
  /* Unary shift/cast (the signbit program).  The SHFT2 immediate template
     reads its source through selector L1, which forces the macro VD, hence
     fixed_vd.  */
  {
    "unary-shift-cast (LM:469,847-856,357-359)",
    1,
    { { 2, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSHFT (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSHFT (0, 0, 0, 0)) },
	     { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPCAST (0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPCAST (0, 0, 0)) } }, true },
      { 0, {}, false } },
    2,
    /* Only one explicit shift mode maps to the template (WH mode 1 / BH
       mode 5 -> template mod1 6); other modes do not match.  */
    { { TR_TABLE_FIELDS, OPB_WH (TT_OP_WH_SFPSHFT2 (0, 0, 0, 0)),
	OPB_WH (TT_OP_BH_SFPSHFT2 (0, 0, 0, 0)), 6, 0, 0, -1,
	4 /* shift imm operand */, 0, 7 /* shift mode operand */, 1, 5 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 1, 3 /* cast mod1 operand */,
	-1, 0, -1, 0, 0 } },
    { "signbit-m0", nullptr },
    "signbit",
    1, 0, true,
    false, false, false,
    false, false,
  },
  /* Unary cast/round (the U16->BF16 cast/round program).  */
  {
    "unary-cast-round (LM:858-871)",
    1,
    { { 2, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPCAST (0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPCAST (0, 0, 0)) },
	     { SU_ROUND, OPB_WH (TT_OP_WH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFP_STOCH_RND (0, 0, 0, 0, 0, 0)) } }, true },
      { 0, {}, false } },
    2,
    /* The plain round form supplies instr_mod1 at operand 3.  Its
       immediate descale field is fixed to zero; operand 4 is the rounding
       mode and must be the verified deterministic mode.  */
    { { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 0, 3 /* cast mod1 */, -1, 0,
	-1, 0, 0 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 0, 1, 3 /* stochrnd instr_mod1 */,
	-1 /* fixed imm0 */, 0, 4 /* rounding mode */, 0, 0 } },
    { "cast-round-m0", nullptr },
    "cast-round",
    -1, 0, false,
    false, false, false,
    false, false,
  },
  /* Predicated select (the TTNN Where shape).  The definition carrier
     hosts the predicate template and the delayed store; the middle payload
     issues as a plain load (as in the library's Where implementation); the
     last carrier hosts the all-lanes restore.  The lane merge itself is
     coalesced into the shared launch VD by the deferred-CC dataflow, so no
     template realizes it.  Misc is 0x700 | store mode
     (encode_misc_select); payloads and store must share one data mode
     (the definition carrier's mode is free), and the row's explicit
     separator is kept as the restore-visibility slot.  VD is fixed at 0:
     every payload targets the shared launch VD.  */
  {
    "predicated-select (loadmacro CC-template generic derivation)",
    2,
    { { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPSETCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPSETCC (0, 0, 0, 0)) } }, true },
      { 1, { { SU_SIMPLE, OPB_WH (TT_OP_WH_SFPENCC (0, 0, 0, 0)),
	       OPB_WH (TT_OP_BH_SFPENCC (0, 0, 0, 0)) } }, false } },
    2,
    /* Template 0: the predicate definition, sense-mapped through the CC
       model (source rvtt_sfpsetcc_v operand 1 is the mod1).
       Template 1: the all-lanes restore, entirely table data (opcode from
       the TT_OP tables, mod1 0, imm12 0, i.e. the 0x8a0000d0 "Restore all
       lanes" word).  */
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
  /* Predicated select, compact calendar (the 3-slot row of the library's
     ckernel_sfpu_where.h): the definition carrier hosts the predicate
     template and the delayed store; the second launch hosts the all-lanes
     restore; the trailing payload load stays explicit and absorbs the row
     stride through its own auto-increment address mode, so the separator
     is deleted.  The store's data mode comes from the launch (misc word
     "select-launch-mod0", UsesLoadMod0ForStore), so the definition
     carrier's load mode must equal the store mode; other rows use the
     4-slot program above.  Matched only when the schedule absorbed the
     stride into the explicit load (require_absorbed_stride).  */
  {
    "predicated-select-compact (ckernel_sfpu_where.h 3-slot)",
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

/* Derive the canonical row's structure from REGION's first row under
   SCHEDULE into *OUT: per-macro hosted value events in program order
   (sub-unit plus source opcode byte), store placement, and the flat
   value/load/store insns.  Returns false when the row exceeds the
   descriptor shape limits (macros beyond two, more than two events per
   macro or four value insns), hosts an instruction with no recorded
   opcode byte, or lacks a store.  */

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
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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

/* Return true unless REGION contains a PRNG-advancing instruction that
   the formed program would not issue exactly once per row.  Such a row
   can be formed only when a matched whole-word PROGRAM hosts that
   instruction in exactly one template, at the same position in every
   row; the region scanner treats opaque consumers as boundaries, so the
   order of PRNG advances is unchanged.  Derived calendars are not
   supported for these rows.  */
static bool
prng_program_preserves_issue_p (const macro_region &region,
				const macro_schedule &schedule,
				const derived_structure &derived,
				const desc_program *program)
{
  int source_ix = -1;
  for (unsigned ix = 0; ix != region.rows[0].insns.length (); ++ix)
    if (rvtt_macro_insn_effects (region.rows[0].insns[ix]).prng_advance)
      {
	if (source_ix >= 0)
	  return false;
	source_ix = ix;
      }
  if (source_ix < 0)
    return true;
  if (!program || schedule.events[source_ix].realization
		  != macro_event::LAUNCHED_TEMPLATE_SLOT)
    return false;
  for (const macro_row &row : region.rows)
    for (unsigned ix = 0; ix != row.insns.length (); ++ix)
      if (rvtt_macro_insn_effects (row.insns[ix]).prng_advance
		  != (ix == (unsigned) source_ix))
	return false;

  int value_ix = -1;
  for (unsigned ix = 0; ix != derived.n_value_insns; ++ix)
    if (derived.value_insns[ix] == region.rows[0].insns[source_ix])
      value_ix = ix;
  if (value_ix < 0)
    return false;
  unsigned copies = 0;
  for (unsigned t = 0; t != program->n_templates; ++t)
    copies += program->templates[t].source_event == value_ix;
  return copies == 1;
}

/* Return whether the row's derived macro key matches program key KEY on
   the current target (IS_WH selects the per-CPU opcode byte): the same
   hosted-event count, store placement, and per-event sub-unit and
   source opcode byte.  */

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

/* Look up the sequence program word labelled NAME in capability table
   C into *WORD; false when there is none.  */

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

/* Look up the capability table's miscellaneous instruction word
   registered under NAME for target C into *WORD; false when no such
   entry exists.  Companion of find_seq_word above for words outside
   the sequence-program table.  */

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
   operands.  An instruction variant with a different operand layout (e.g.
   the constant-register SFPSWAP forms, whose operand list is shorter
   than the swap program expects) must not match the program; it goes to
   the derived-calendar path or is refused, instead of failing inside the
   program's packer.  An operand that exists but is not an encodable
   constant still matches and gets the encodability refusal.  */

static bool
operand_exists (rtx_insn *insn, int pos)
{
  extract_insn (insn);
  return pos >= 0 && pos < recog_data.n_operands;
}

/* Return whether every template rule of P that reads a source operand
   (mod1, imm12, or a pinned operand) finds it inside its source insn's
   recog operand list.  */

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
      /* The constant-register SFPSWAP variants have enough RTL operands
         for an index-only test to succeed, but their operand roles are not
         the swap program's layout.  They belong to the derived-calendar
         class below.  */
      uint8_t src_c, mod1;
      if (swap_cst_template_fields (src, &src_c, &mod1))
	return false;
      if (rule.mod1_op >= 0 && !operand_exists (src, rule.mod1_op))
	return false;
      if (rule.imm12_op >= 0 && !operand_exists (src, rule.imm12_op))
	return false;
      if (rule.pin_op >= 0 && !operand_exists (src, rule.pin_op))
	return false;
    }
  return true;
}

/* Match DERIVED against the verified descriptor programs.
   ABSORBED_INTO_EXPLICIT selects the compact select program (which
   applies only when the schedule absorbed the row stride into the
   trailing explicit load) and excludes every other program.  A program
   matches only when every macro key agrees for the current target and
   its template rules can reach their source operands.  Return null when
   no program applies (the caller then tries the derived calendar).  */

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

/* Return whether the row's store consumes the second result of the
   source instruction, which selects SFPSWAP mod1 9 over mod1 1.  */

static bool
second_set_reaches_store_p (rtx_insn *value_insn, rtx_insn *store_insn)
{
  rtx pat = PATTERN (value_insn);
  if (GET_CODE (pat) != PARALLEL || XVECLEN (pat, 0) < 2)
    return false;
  rtx set1 = XVECEXP (pat, 0, 1);
  if (GET_CODE (set1) != SET || !REG_P (SET_DEST (set1)))
    return false;
  xtt_effect_set store_effects = rvtt_macro_insn_effects (store_insn);
  unsigned regno = REGNO (SET_DEST (set1));
  if (regno < SFPU_REG_FIRST || regno - SFPU_REG_FIRST > 16)
    return false;
  return (store_effects.lreg_read >> (regno - SFPU_REG_FIRST)) & 1;
}

/* Fetch INSN's recog operand POS as a constant integer into *VALUE;
   false when POS is out of range or the operand is not a CONST_INT.  */

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
/* Derive and check the CC-template model of a predicated-select row
   from the schedule's slots, the matched program's delays and the CC
   facts in the capability tables.  Fill OUT and *STORE_MODE (the shared
   payload/store data mode) on success.  Return false when any check
   fails; *REFUSAL is then null (the caller uses cc-template-unproved)
   or a more specific name.  Nothing is modified on failure.	      */
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
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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
  /* The row relies on the all-lanes enable before the region, so the
     formation-level check (needs_all_lanes_prefix) must be enabled.  */
  if (!region.net.cc_read)
    return false;

  const macro_event &def_ev = schedule.events[def_ix];
  const macro_event &restore_ev = schedule.events[restore_ix];
  const macro_event &store_ev = schedule.events[store_ix];
  xtt_effect_set def_e = rvtt_macro_insn_effects (row.insns[def_ix]);
  xtt_effect_set merge_e = rvtt_macro_insn_effects (row.insns[merge_ix]);

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
     is the merge's own destination, the selected value its other input
     (the scheduler's coalescing already checked the shape).  */
  uint32_t dest = merge_e.lreg_write;
  uint32_t other = merge_e.lreg_read & ~dest;
  int live_ix = -1, sel_ix = -1, def_src_ix = -1;
  for (unsigned l = 0; l != n_loads; ++l)
    {
      xtt_effect_set le = rvtt_macro_insn_effects (row.insns[load_ix[l]]);
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
     definition is visible (the all-lanes load) and the other at or after
     it (the predicated overwrite).  The template's predicate sense is
     complemented exactly when the later payload is the merge's live
     operand.  */
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

  /* The store's lane predicate is the CC state when the store executes
     (store_lane_mask_live_at_execution); the launch does not latch it, and
     a CC write retiring in the store's own cycle is not yet visible to it.
     The all-lanes restore must therefore retire strictly before the store
     executes: restore_exec < store_exec, i.e. restore_visible
     (= restore_exec + lag, lag 1) <= store_exec.  The 4-slot select
     calendar with the kept separator violates this (restore_exec ==
     store_exec == 3: the store runs under the SFPSETCC complement mask and
     leaves the true-branch lanes unwritten on Blackhole); the compact
     3-slot calendar satisfies it (restore_exec 2 < store_exec 3).
     Likewise the store must retire before the next row's predicate
     definition executes (def_exec + ii with identical rows), or it would
     run under that row's mask.  */
  if (!rvtt_macro::store_lane_mask_live_at_execution ())
    return false;
  if (restore_exec >= store_exec
      || store_exec >= def_exec + schedule.ii)
    {
      if (refusal)
	*refusal = macro_desc_refusal_cc_restore_store_race;
      return false;
    }

  /* Payloads and store must share one data mode (the definition
     carrier's own load mode is free).  */
  rtx address, mode, addr_mode;
  HOST_WIDE_INT live_mode, sel_mode, st_mode;
  xtt_effect_set live_e = rvtt_macro_insn_effects (row.insns[live_ix]);
  xtt_effect_set sel_e = rvtt_macro_insn_effects (row.insns[sel_ix]);
  xtt_effect_set st_e = rvtt_macro_insn_effects (row.insns[store_ix]);
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
  /* Compact program (UsesLoadMod0ForStore): the delayed store's data
     mode is its launch's mod0, i.e. the definition carrier's load mode,
     which must therefore equal the store mode.  The 4-slot program leaves
     the definition carrier's mode free (the misc word carries the store
     mode).  */
  if (launch_mod0)
    {
      xtt_effect_set def_src_e = rvtt_macro_insn_effects (row.insns[def_src_ix]);
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
/* Calendar derivation: when no whole-word program matches, derive the
   sequence words, delays and misc fields from the schedule and the
   architectural facts (see rvtt-macro-derive-core.h).  The supported
   template classes are the constant-register SFPSWAP family (unary
   max/min) and the classes of derived_value_template_fields; other rows
   are refused with descriptor-program-unproven.  */
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

/* Analyze a constant-register SFPSWAP variant.  The single-result
   patterns carry a variant marker as the unspec's last element
   (1 = constant in the VC position, 2 = constant in the VD position).
   The template always places the launch value in VD and the constant in
   VC, so the VD-position variant uses the complementary result-routing
   mod (bit 8, the same routing bit the Min/Max program uses).  Only the
   full-vector min/max modes (mod1 1 or 9 after mapping) are supported;
   sub-vector modes and the dual-constant variant are refused.  */

static bool
swap_cst_template_fields (rtx_insn *insn, uint8_t *src_c, uint8_t *mod1)
{
  if (insn_unspecv (insn) != UNSPECV_SFPSWAP)
    return false;
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) != SET)
    return false;		/* dual-result / dual-constant forms   */
  rtx un = SET_SRC (pat);
  if (GET_CODE (un) != UNSPEC_VOLATILE || XVECLEN (un, 0) != 5)
    return false;
  rtx a = XVECEXP (un, 0, 0);
  rtx b = XVECEXP (un, 0, 1);
  rtx mod = XVECEXP (un, 0, 2);
  rtx marker = XVECEXP (un, 0, 4);
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

/* Architectural index of any LREG operand -- a hard physical register
   L0..L7, or a hardware constant register (cstlreg unspec / hard reg
   L8..L15).  -1 for anything else.  */

static int
lreg_index (rtx x)
{
  if (REG_P (x) && REGNO (x) >= SFPU_REG_FIRST
      && REGNO (x) <= SFPU_REG_FIRST + 15)
    return (int) (REGNO (x) - SFPU_REG_FIRST);
  return cstlreg_index (x);
}

/* Template classes supported by the derived calendar besides the
   constant-register SFPSWAP family.  Each maps one launched value insn to
   template fields whose SFPLOADMACRO execution has exactly the explicit
   instruction's semantics under the operand overrides of the
   SFPLOADMACRO.md functional model (VD := launch VD or LReg16; route bit
   0x80 replaces VB with the launch VD, otherwise VC is replaced; the
   other source fields come from the template word):

   - SFPCAST (mods 0, and 3 on BH, as accepted by the pattern): reads VC
     only (SFPCAST.md).  A source equal to the launch-VD register packs
     src_c 0 and uses the VC:=VD route; any other source stays in VC
     under the VB route (SFPCAST does not read VB).

   - SFPIADD register form (mods 0..10, ARG_IMM clear): computes VB + VC
     with VB = VD implicit (SFPIADD.md).  The tied accumulator must be the
     launch-VD register so the VB:=VD route is the identity; the VC
     addend stays in src_c.

   - SFPIADD immediate form (mods 1, 5, 9: ARG_IMM set, 2SCOMP clear,
     mod1 <= 10, as accepted by rvtt_sfpiadd_i_lv_int; the instruction
     reads only LReg[VC] and SignExtend(Imm12), never VD): the immediate
     goes in the template's imm12 field.  The in-place form (source ==
     destination == launch-VD register) packs src_c 0 and uses the VC:=VD
     route; another source stays in VC under route 1.  Only a constant
     immediate is supported; a register immediate needs a run-time
     instruction push and is refused.

   - SFPSHFT immediate in-place form: realized as the SFPSHFT2 immediate
     template on the Round sub-unit, as in the signbit program (WH mode 1
     / BH mode 5 -> SHFT2 template mod1 6).  The shifted value must be
     the launch-VD register (SHFT2 reads VB:=VD, a special case in
     SFPLOADMACRO.md), and the immediate goes in imm12.

   - SFPSHFT register-amount form (mod 0 logical on WH/BH, mod 2
     arithmetic on BH): in place on the launch VD; the signed amount
     register stays in VC (route 1); imm12 packs 0.

   - The Simple unary family: SFPLZ (mods 0/2/4), SFPMOV (mods 0/1/2;
     not mod 8, PRNG), SFPABS (0/1), SFPEXEXP (0/1/2/10), SFPEXMAN (0/1),
     each of which reads only LReg[VC] in these mods.  LZ/MOV/ABS accept
     the route override like CAST; EXEXP/EXMAN have no override and
     always name their source (naming the launch-VD register forces fixed
     VDs).  CC-writing mods are included.

   - The logic family SFPAND/SFPOR/SFPXOR: the tied in-place form (VD op=
     VC; XOR everywhere, AND/OR on WH) and the BH three-operand USE_VB
     form (VD write-only; one source named through the VB subfield, where
     L0 is a legal name, the other in VC or as the launch VD through the
     VC:=VD route).

   - SFPMUL24, BH only (mods 0 LOWER / 1 UPPER; indirect-VA/VD mods are
     refused): computes the low/high 23 bits of VA*VB with the VC addend
     fixed to the zero register L9 (SFPMUL24.md requires VC == 9).  One
     factor must be the launch-VD register (VB:=VD route); the other
     factor's register goes in the template's VA field (inside the imm12
     region in the TT_OP layout), and src_c holds L9.  Both modes are
     symmetric in VA/VB (sfpmul24_result), so a product the register
     allocator tied to the VA-side factor is handled by commuting; the
     VB-side order is tried first.

   Everything else is unsupported.  LAUNCH_VD < 0 (unknown carrier
   register) supports nothing.  An encoded VC of L0 cannot be told apart
   from the "source unused" convention (planned_src_c 0 routes the launch
   VD to VC) and is refused.  */

/* insn_unspecv through a PARALLEL's leading SET (the shift patterns
   carry a scratch clobber).  */

static int
value_insn_unspecv (rtx_insn *insn)
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

struct derived_template_fields
{
  uint8_t opcode;
  uint8_t mod1;
  uint8_t src_c;
  uint16_t imm12;
  /* Physical LREGs (L0..L7) that the template names in its source
     fields, as opposed to reaching them through the launch-VD route.  If
     such a read names a value carrier's loaded register, that carrier must
     keep its own physical destination (fixed VD).  */
  uint32_t name_reads;
};

/* Map launched value insn INSN to the template fields its macro
   realization would encode, for the classes documented above.
   LAUNCH_VD is the physical index of the hosting carrier's launch VD
   (-1 unknown, -2 store-only carrier; see below).  Fill *OUT, including
   the physical registers named in source fields, and return true only
   when INSN is in a supported class with supported operands.  */

static bool
derived_value_template_fields (rtx_insn *insn, int launch_vd,
			       derived_template_fields *out)
{
  /* LAUNCH_VD -2 marks a store-only carrier: its VD holds an unread
     load, so no class may treat a register as the launch VD there
     (comparisons with -2 are always false; the cast/mov class still names
     its source, the accumulate/factor classes refuse).  -1 supports
     nothing.  */
  if (launch_vd == -1)
    return false;
  rtx pat = PATTERN (insn);
  rtx set = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
  if (GET_CODE (set) != SET || !REG_P (SET_DEST (set)))
    return false;
  int dest = lreg_index (SET_DEST (set));
  if (dest < 0 || dest > 7)
    return false;
  rtx un = SET_SRC (set);
  if (GET_CODE (un) != UNSPEC_VOLATILE)
    return false;

  switch (value_insn_unspecv (insn))
    {
    case UNSPECV_SFPCAST:
      {
	if (XVECLEN (un, 0) != 3)
	  return false;
	rtx src = XVECEXP (un, 0, 1);
	rtx mod = XVECEXP (un, 0, 2);
	int s = lreg_index (src);
	if (s < 0 || !CONST_INT_P (mod))
	  return false;
	HOST_WIDE_INT m = INTVAL (mod);
	if (!(m == 0 || (TARGET_XTT_TENSIX_BH && m == 3)))
	  return false;		/* the modes the pattern accepts */
	out->opcode = source_opcode_byte (insn);
	out->mod1 = (uint8_t) m;
	out->imm12 = 0;
	out->name_reads = 0;
	if (s == launch_vd)
	  out->src_c = 0;	/* VC:=VD route, identity	       */
	else if (s != 0)
	  {
	    out->src_c = (uint8_t) s;
	    if (s < 8)
	      out->name_reads |= 1u << s;
	  }
	else
	  return false;		/* L0 collides with the unused code    */
	return true;
      }
    case UNSPECV_SFPIADD:
      {
	if (XVECLEN (un, 0) == 7)
	  {
	      /* Immediate form (rvtt_sfpiadd_i_lv_int): [0] mem-or-0,
	         [1] opcode const, [2] shifts const, [3] imm-or-insn,
	         [4] src (VC), [5] lv, [6] mod1 (ARG_IMM already OR'd).
	         Only the constant-immediate alternative is supported.  */
	    rtx mem = XVECEXP (un, 0, 0);
	    rtx imm = XVECEXP (un, 0, 3);
	    rtx src = XVECEXP (un, 0, 4);
	    rtx mod = XVECEXP (un, 0, 6);
	    int s = lreg_index (src);
	    if (mem != const0_rtx || !CONST_INT_P (imm) || s < 0
		|| !CONST_INT_P (mod))
	      return false;
	    HOST_WIDE_INT m = INTVAL (mod);
	    if (!IN_RANGE (m, 0, 10) || (m & 3) != 1)
	      return false;	/* ARG_IMM set, 2SCOMP clear	       */
	    HOST_WIDE_INT iv = INTVAL (imm);
	    if (iv < -2048 || iv > 2047)
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPIADD (0, 0, 0, 0)
			   : TT_OP_BH_SFPIADD (0, 0, 0, 0)) >> 24;
	    out->mod1 = (uint8_t) m;
	    out->imm12 = (uint16_t) (iv & 0xfff);
	    out->name_reads = 0;
	    if (s == dest && s == launch_vd)
	      out->src_c = 0;	/* in-place: VC:=VD route	       */
	    else if (s != 0)
	      {
		out->src_c = (uint8_t) s;	/* named source, route 1 */
		if (s < 8)
		  out->name_reads |= 1u << s;
	      }
	    else
	      return false;	/* L0 collides with the unused code    */
	    return true;
	  }
	if (XVECLEN (un, 0) != 4)
	  return false;
	rtx acc = XVECEXP (un, 0, 1);
	rtx src = XVECEXP (un, 0, 2);
	rtx mod = XVECEXP (un, 0, 3);
	int a = lreg_index (acc);
	int s = lreg_index (src);
	if (a < 0 || s <= 0 || !CONST_INT_P (mod))
	  return false;
	HOST_WIDE_INT m = INTVAL (mod);
	if (!IN_RANGE (m, 0, 10) || (m & 1) != 0)
	  return false;		/* unsupported register-form mode    */
	if (a != dest || a != launch_vd)
	  return false;		/* accumulator must be the launch VD   */
	out->opcode = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPIADD (0, 0, 0, 0)
		       : TT_OP_BH_SFPIADD (0, 0, 0, 0)) >> 24;
	out->mod1 = (uint8_t) m;
	out->src_c = (uint8_t) s;
	out->imm12 = 0;
	out->name_reads = s < 8 ? 1u << s : 0;
	return true;
      }
    case UNSPECV_SFPSHFT:
      {
	if (XVECLEN (un, 0) == 4)
	  {
	    /* Register-amount form (rvtt_sfpshft_v_lv_int): [0] lv,
	       [1] tied value, [2] amount, [3] mod1.  Supported modes: 0 (logical,
	       WH/BH) and BH mod 2 (arithmetic right for negative amounts).  The
	       shifted value is LReg[VD] (in place on the launch VD); the signed
	       amount is LReg[VC] and stays in VC under route 1; imm12 must pack 0
	       (the field is unused in this mode).  */
	    rtx val = XVECEXP (un, 0, 1);
	    rtx amt = XVECEXP (un, 0, 2);
	    rtx mod = XVECEXP (un, 0, 3);
	    int v = lreg_index (val);
	    int s = lreg_index (amt);
	    if (v < 0 || s < 0 || !CONST_INT_P (mod))
	      return false;
	    HOST_WIDE_INT m = INTVAL (mod);
	    if (!(m == 0 || (TARGET_XTT_TENSIX_BH && m == 2)))
	      return false;
	    if (v != dest || v != launch_vd)
	      return false;	/* in-place on the launch VD	       */
	    if (s == 0)
	      return false;	/* L0 collides with the unused code    */
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPSHFT (0, 0, 0, 0)
			   : TT_OP_BH_SFPSHFT (0, 0, 0, 0)) >> 24;
	    out->mod1 = (uint8_t) m;
	    out->src_c = (uint8_t) s;
	    out->imm12 = 0;
	    out->name_reads = s < 8 ? 1u << s : 0;
	    return true;
	  }
	if (XVECLEN (un, 0) != 7)
	  return false;
	rtx imm = XVECEXP (un, 0, 3);
	rtx src = XVECEXP (un, 0, 4);
	rtx mod = XVECEXP (un, 0, 6);
	int s = lreg_index (src);
	if (s < 0 || !CONST_INT_P (imm) || !CONST_INT_P (mod))
	  return false;
	if (INTVAL (mod) != (TARGET_XTT_TENSIX_WH ? 1 : 5))
	  return false;		/* only the signbit mode pair	       */
	HOST_WIDE_INT iv = INTVAL (imm);
	if (iv < -2048 || iv > 2047)
	  return false;
	if (s != dest || s != launch_vd)
	  return false;		/* in-place on the launch VD	       */
	out->opcode = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPSHFT2 (0, 0, 0, 0)
		       : TT_OP_BH_SFPSHFT2 (0, 0, 0, 0)) >> 24;
	out->mod1 = 6;
	out->src_c = 0;
	out->imm12 = (uint16_t) (iv & 0xfff);
	out->name_reads = 0;
	return true;
      }
    case UNSPECV_SFPAND:
    case UNSPECV_SFPOR:
    case UNSPECV_SFPXOR:
      {
	/* The logic family.  Two pattern shapes:

	   - The tied in-place form (rvtt_sfp<logical>_lv_2op: XOR on every
	     CPU, AND/OR on WH): VD op= VC.  The tied operand must be the launch
	     VD; the other source stays in VC.  XOR (0x8d) takes no operand
	     override; AND/OR mod 0 require imm12/VB zero, which is what the
	     template packs.

	   - The BH three-operand form (rvtt_sfp<logical>_lv_bh, mod1 =
	     SFPAND_MOD1_USE_VB): VD is write-only; one source is named through
	     the VB subfield (imm12 bits 3:0 = word bits 15:12; L0 is a legal VB
	     name, the L0 restriction applies to VC only), the other stays in VC
	     or, when it is the launch-VD register, goes through the VC:=VD
	     override with src_c 0.

	   See SFPAND.md, SFPOR.md and SFPXOR.md (BH).  */
	int uv = value_insn_unspecv (insn);
	uint8_t opb;
	if (uv == UNSPECV_SFPAND)
	  opb = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPAND (0, 0, 0, 0)
		 : TT_OP_BH_SFPAND (0, 0, 0, 0)) >> 24;
	else if (uv == UNSPECV_SFPOR)
	  opb = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPOR (0, 0, 0, 0)
		 : TT_OP_BH_SFPOR (0, 0, 0, 0)) >> 24;
	else
	  opb = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPXOR (0, 0, 0, 0)
		 : TT_OP_BH_SFPXOR (0, 0, 0, 0)) >> 24;
	if (XVECLEN (un, 0) == 3)
	  {
	    /* Tied in-place: [0] lv, [1] tied, [2] src.  */
	    rtx tied = XVECEXP (un, 0, 1);
	    rtx src = XVECEXP (un, 0, 2);
	    int v = lreg_index (tied);
	    int s = lreg_index (src);
	    if (v < 0 || s <= 0)
	      return false;	/* L0 collides with the unused code    */
	    if (v != dest || v != launch_vd)
	      return false;	/* in-place on the launch VD	       */
	    out->opcode = opb;
	    out->mod1 = 0;
	    out->src_c = (uint8_t) s;
	    out->imm12 = 0;
	    out->name_reads = s < 8 ? 1u << s : 0;
	    return true;
	  }
	if (XVECLEN (un, 0) == 4 && uv != UNSPECV_SFPXOR
	    && TARGET_XTT_TENSIX_BH)
	  {
	    /* BH USE_VB: [0] lv, [1] srcA, [2] srcB, [3] mod1.  */
	    rtx sa = XVECEXP (un, 0, 1);
	    rtx sb = XVECEXP (un, 0, 2);
	    rtx mod = XVECEXP (un, 0, 3);
	    int a = lreg_index (sa);
	    int b = lreg_index (sb);
	    if (a < 0 || b < 0 || !CONST_INT_P (mod)
		|| INTVAL (mod) != (HOST_WIDE_INT) SFPAND_MOD1_USE_VB)
	      return false;
	    out->opcode = opb;
	    out->mod1 = (uint8_t) SFPAND_MOD1_USE_VB;
	    out->imm12 = (uint16_t) (a & 0xf);	/* the VB subfield     */
	    out->name_reads = a < 8 ? 1u << a : 0;
	    if (b == launch_vd)
	      out->src_c = 0;	/* VC:=VD route		       */
	    else if (b != 0)
	      {
		out->src_c = (uint8_t) b;
		if (b < 8)
		  out->name_reads |= 1u << b;
	      }
	    else
	      return false;	/* L0 collides with the unused code    */
	    return true;
	  }
	return false;
      }
    case UNSPECV_SFPLZ:
    case UNSPECV_SFPMOV:
    case UNSPECV_SFPABS:
    case UNSPECV_SFPEXEXP:
    case UNSPECV_SFPEXMAN:
      {
	/* The Simple unary family (rvtt_sfp<unary>_lv): each reads only
	   LReg[VC] in the supported mods and writes VD, including the
	   CC-writing mods (LZ 2, EXEXP 2/10).  LZ, MOV and ABS accept the route
	   override (RC_VC): a source equal to the launch VD packs src_c 0 under
	   the VC:=VD route, any other source is named.  EXEXP and EXMAN take no
	   operand override (0x77/0x78 are in no route class), so their source is
	   always named, even when it is the launch-VD register (which then
	   forces fixed VDs).  SFPMOV mod 8 (PRNG state) is not supported.  */
	if (XVECLEN (un, 0) != 3)
	  return false;
	rtx src = XVECEXP (un, 0, 1);
	rtx mod = XVECEXP (un, 0, 2);
	int s = lreg_index (src);
	if (s < 0 || !CONST_INT_P (mod))
	  return false;
	HOST_WIDE_INT m = INTVAL (mod);
	bool routed = true;
	switch (value_insn_unspecv (insn))
	  {
	  case UNSPECV_SFPLZ:
	    if (!(m == 0 || m == 2 || m == 4))
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPLZ (0, 0, 0, 0)
			   : TT_OP_BH_SFPLZ (0, 0, 0, 0)) >> 24;
	    break;
	  case UNSPECV_SFPMOV:
	    if (!IN_RANGE (m, 0, 2))
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPMOV (0, 0, 0, 0)
			   : TT_OP_BH_SFPMOV (0, 0, 0, 0)) >> 24;
	    break;
	  case UNSPECV_SFPABS:
	    if (!IN_RANGE (m, 0, 1))
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPABS (0, 0, 0, 0)
			   : TT_OP_BH_SFPABS (0, 0, 0, 0)) >> 24;
	    break;
	  case UNSPECV_SFPEXEXP:
	    if (!(IN_RANGE (m, 0, 2) || m == 10))
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPEXEXP (0, 0, 0, 0)
			   : TT_OP_BH_SFPEXEXP (0, 0, 0, 0)) >> 24;
	    routed = false;
	    break;
	  default:		/* UNSPECV_SFPEXMAN */
	    if (!IN_RANGE (m, 0, 1))
	      return false;
	    out->opcode = (TARGET_XTT_TENSIX_WH
			   ? TT_OP_WH_SFPEXMAN (0, 0, 0, 0)
			   : TT_OP_BH_SFPEXMAN (0, 0, 0, 0)) >> 24;
	    routed = false;
	    break;
	  }
	out->mod1 = (uint8_t) m;
	out->imm12 = 0;
	out->name_reads = 0;
	if (routed && s == launch_vd)
	  out->src_c = 0;	/* VC:=VD route, identity	       */
	else if (s != 0)
	  {
	    out->src_c = (uint8_t) s;
	    if (s < 8)
	      out->name_reads |= 1u << s;
	  }
	else
	  return false;		/* L0 collides with the unused code    */
	return true;
      }
    case UNSPECV_SFPMUL24:
      {
	if (!TARGET_XTT_TENSIX_BH || XVECLEN (un, 0) != 4)
	  return false;
	rtx va = XVECEXP (un, 0, 1);
	rtx vb = XVECEXP (un, 0, 2);
	rtx mod = XVECEXP (un, 0, 3);
	int a = lreg_index (va);
	int b = lreg_index (vb);
	if (a < 0 || b < 0 || !CONST_INT_P (mod))
	  return false;
	HOST_WIDE_INT m = INTVAL (mod);
	if (m != 0 && m != 1)
	  return false;		/* plain LOWER/UPPER only	       */
	/* One factor must be the launch-VD register (supplied by the VB:=VD
	   route); the other factor's register is named in the template's VA
	   field.  Both modes are symmetric in VA/VB (LOWER: (a*b) low 23 bits;
	   UPPER: (a_23*b_23) >> 23, SFPMUL24.md), so when the allocator tied
	   the product to the VA-side factor instead, the commuted word naming
	   the VB-side factor computes the same value.  The VB-side order is
	   tried first.  */
	int named;
	if (b == dest && b == launch_vd)
	  named = a;		/* VA-side factor named	       */
	else if (a == dest && a == launch_vd)
	  named = b;		/* commuted: VB-side factor named      */
	else
	  return false;		/* no factor is the launch VD	       */
	/* The TT_OP layout places the named factor at word bits 19:16
	   = imm12 bits 7:4; the VB field (bits 15:12 = imm12 bits 3:0)
	   is overridden at execution and packs 0.  */
	out->opcode = (uint8_t) (TT_OP_BH_SFPMUL24 (0, 0, 0, 0, 0) >> 24);
	out->mod1 = (uint8_t) m;
	out->src_c = 9;		/* the mandated zero constant LReg[9]  */
	out->imm12 = (uint16_t) (named << 4);
	out->name_reads = named < 8 ? 1u << named : 0;
	return true;
      }
    default:
      return false;
    }
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
  uint16_t template_imm12[4];	/* generic classes; swap packs 0       */
  unsigned n_templates;
  int store_macro;
  unsigned store_mode;
  bool store_uses_carrier_mode;
  unsigned store_only_vd;
  /* Fixed launch VDs: when any explicit row member reads a value
     carrier's loaded register by name, every value carrier keeps its own
     physical load destination (vd_pin >= 0) instead of the alternating
     {0,1} pair, subject to the derivation core's launch-VD lifetime
     check.  -1 = alternating.  */
  int vd_pin[4];
  const char *refusal;
};

/* Build the derivation-core row description from the region's explicit
   facts and the schedule.  Return false with DS->refusal null when the
   row is outside the supported template classes (the caller then uses
   descriptor-program-unproven).  */

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
  for (unsigned m = 0; m < 4; ++m)
    ds->vd_pin[m] = -1;

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
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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
  unsigned ev_pos[rvtt_macro_derive::MAX_EVENTS] = {};	/* insn index  */
  uint32_t name_reads_all = 0;	/* fixed-VD escape analysis	       */
  uint32_t first_touch_write = 0, touched = 0;	/* sacrificial VD      */
  uint32_t name_reads_all_launched = 0;	/* launched surviving fields   */
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
      bool is_load = e.dst_mem_read;
      bool is_store = e.dst_mem_write;
      /* First-touch kind per register, program order (reads shadow the
	 same insn's writes -- an in-place op's first touch is a read).  */
      first_touch_write |= e.lreg_write & ~e.lreg_read & ~touched;
      touched |= e.lreg_read | e.lreg_write;
      bool launched_value = ev.realization
	== macro_event::LAUNCHED_TEMPLATE_SLOT && !ev.is_store;
      bool launched_store = is_store
	&& ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT;

      if (launched_value)
	{
	  /* Supported template classes: constant-register SFPSWAP and the
	     classes of derived_value_template_fields.  */
	  derived_template_fields tf;
	  uint8_t src_c = 0, mod1 = 0;
	  if (swap_cst_template_fields (row.insns[ix], &src_c, &mod1))
	    {
	      tf.opcode = source_opcode_byte (row.insns[ix]);
	      tf.mod1 = mod1;
	      tf.src_c = src_c;
	      tf.imm12 = 0;
	      tf.name_reads = 0;	/* constant-register source     */
	    }
	  else
	    {
	      /* The launch-VD register of this event's carrier: the
		 carried load's destination, or the sacrificial VD of a
		 store-only carrier.  */
	      int launch_vd = -1;
	      if (ev.macro_index < 4)
		{
		  uint32_t regs = carrier_load_regs[ev.macro_index];
		  if (regs && !(regs & (regs - 1)))
		    launch_vd = ctz_hwi (regs);
		  else if (!regs)
		    launch_vd = -2;	/* store-only carrier	       */
		}
	      if (!derived_value_template_fields (row.insns[ix], launch_vd,
						  &tf))
		return false;	/* outside the admitted class	       */
	    }
	  if (n_ev == MAX_EVENTS)
	    return false;
	  name_reads_all |= tf.name_reads;
	  name_reads_all_launched |= tf.name_reads;
	  /* Identical field tuples encode to identical template words and share
	     one InstructionTemplate destination.  */
	  int slot = -1;
	  for (unsigned t = 0; t != ds->n_templates && slot < 0; ++t)
	    if (ds->template_opcode[t] == tf.opcode
		&& ds->template_src_c[t] == tf.src_c
		&& ds->template_mod1[t] == tf.mod1
		&& ds->template_imm12[t] == tf.imm12)
	      slot = (int) t;
	  if (slot < 0)
	    {
	      if (ds->n_templates == 4)
		return false;
	      slot = (int) ds->n_templates;
	      ds->template_opcode[slot] = tf.opcode;
	      ds->template_src_c[slot] = tf.src_c;
	      ds->template_mod1[slot] = tf.mod1;
	      ds->template_imm12[slot] = tf.imm12;
	      ++ds->n_templates;
	    }
	  event_spec &spec = ds->row.events[n_ev];
	  spec.opcode = tf.opcode;
	  spec.is_store = false;
	  spec.macro_index = ev.macro_index;
	  spec.carrier_slot = ds->row.macro_slot[ev.macro_index];
	  spec.dep_mask = 0;
	  spec.latest_issued_input_slot = -1;
	  spec.reads_carrier_vd_reg
	    = (e.lreg_read & carrier_load_regs[ev.macro_index]) != 0;
	  spec.planned_src_c = tf.src_c;
	  spec.template_key = (uint8_t) (slot + 1);
	  spec.template_mod1 = tf.mod1;
	  spec.template_imm12 = tf.imm12;
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
	  for (unsigned r = 0; r < 16; ++r)
	    if ((e.lreg_write >> r) & 1)
	      last_writer[r] = (int) n_ev;
	  ev_pos[n_ev] = ix;
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
	  ev_pos[n_ev] = ix;
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
	  /* Explicit loads would be recorded with unit_mask 0, which the
	     derivation core's hazard rule ignores (it tests the mask first), so
	     they are not recorded; recording them would only use up slots needed
	     by issues that do constrain the calendar.  */
	}
      else if (ev.realization == macro_event::EXPLICIT_INSN)
	{
	  /* An explicit compute issue occupies its sub-unit in its issue cycle
	     (the ISA discard rule) and reads every register operand by name.  */
	  name_reads_all |= e.lreg_read;
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
	  /* Only explicit issues with a nonzero sub-unit mask are recorded; a
	     zero mask cannot affect the derivation core's result.  If there are
	     more than the core can hold, refuse rather than drop some, since that
	     would check the calendar against an incomplete hazard set.  */
	  if (mask != 0)
	    {
	      if (ds->row.n_explicits >= 16)
		{
		  ds->refusal = "descriptor-explicit-issue-cap-exceeded";
		  return false;
		}
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
	return false;		/* coalesced/CC rows use the select
				   programs			       */
    }
  ds->row.n_events = n_ev;
  if (ds->row.store_event < 0 || ds->row.store_producer < 0
      || ds->n_templates == 0)
    return false;

  ds->row.ii = schedule.ii;
  ds->row.last_issue_slot = schedule.ii - 1;
  ds->row.vd_alternates = schedule.alternating_vd;

  /* Hazards of hosted events against the row's explicit issues and
     earlier events (register WAR floors, later-consumer/overwrite
     deadlines, same-cycle anti-dependences).  Slot+1 encoding; 0 = no
     constraint.  */
  for (unsigned ei = 0; ei < n_ev; ++ei)
    {
      event_spec &spec = ds->row.events[ei];
      if (spec.is_store)
	continue;
      xtt_effect_set ee = rvtt_macro_insn_effects (row.insns[ev_pos[ei]]);
      uint32_t w = ee.lreg_write;
      if (!w)
	continue;
      for (unsigned jx = 0; jx != row.insns.length (); ++jx)
	{
	  if (jx == ev_pos[ei])
	    continue;
	  const macro_event &jev = schedule.events[jx];
	  if (jev.realization != macro_event::EXPLICIT_INSN)
	    continue;
	  xtt_effect_set je = rvtt_macro_insn_effects (row.insns[jx]);
	  int p1 = jev.slot + 1;
	  if (jx < ev_pos[ei])
	    {
	      if ((je.lreg_read & w) && p1 > spec.war_floor_slot_p1)
		spec.war_floor_slot_p1 = (int16_t) p1;
	    }
	  else
	    {
	      if ((je.lreg_read & w)
		  && (!spec.issue_consumer_slot_p1
		      || p1 < spec.issue_consumer_slot_p1))
		spec.issue_consumer_slot_p1 = (int16_t) p1;
	      if ((je.lreg_write & w)
		  && (!spec.issue_overwrite_slot_p1
		      || p1 < spec.issue_overwrite_slot_p1))
		spec.issue_overwrite_slot_p1 = (int16_t) p1;
	    }
	}
      /* Earlier events reading a register this event writes.  */
      for (unsigned d = 0; d < ei; ++d)
	{
	  xtt_effect_set de = rvtt_macro_insn_effects (row.insns[ev_pos[d]]);
	  if (de.lreg_read & w)
	    spec.anti_dep_mask |= (uint8_t) (1u << d);
	}
    }

  /* Fixed launch VDs: when any read by name (an explicit issue's register
     operand or a hosted template's source field) consumes a value
     carrier's loaded register, the alternating {0,1} VD pair would move
     the value away from the register its consumers name, so every value
     carrier keeps its own physical load destination, subject to the
     derivation core's launch-VD lifetime check (the next row instance's
     rewrite).  Rows with several value carriers are pinned too, since
     only one alternating pair is supported.  */
  {
    uint32_t all_carrier_regs = 0;
    unsigned value_carriers = 0;
    for (unsigned m = 0; m < ds->row.n_macros && m < 4; ++m)
      if (carrier_load_regs[m])
	{
	  all_carrier_regs |= carrier_load_regs[m];
	  ++value_carriers;
	}
    if ((name_reads_all & all_carrier_regs) || value_carriers > 1)
      {
	for (unsigned m = 0; m < ds->row.n_macros && m < 4; ++m)
	  {
	    uint32_t regs = carrier_load_regs[m];
	    if (!regs)
	      continue;
	    if ((regs & (regs - 1)) != 0 || (unsigned) ctz_hwi (regs) > 7)
	      return false;	/* not one physical L0..L7 register    */
	    ds->vd_pin[m] = ctz_hwi (regs);
	  }
	ds->row.vd_alternates = false;
      }
  }

  /* The store-only carrier's VD, which receives an unused load.  The
     launch's VDLo field encodes VD 0..3 (VDHi is shared with the address
     LSB and stays 0 for the even Dst addresses used here), so the register
     must be in L0..L3.  Prefer the lowest register outside the alternating
     pair and the region's internal set; if every low register is used in
     the row, take an internal temporary that can safely be clobbered: its
     first access in the row is a write (so the next row never reads the
     garbage), it is no value carrier's pinned VD, and no launched event's
     source field names it (explicit readers all issue before the store
     carrier's last slot).  Otherwise the row is refused.  */
  {
    unsigned vd = rvtt_macro_store_only_sacrificial_vd (region.internal_lregs);
    if (vd >= 4)
      {
	vd = 8;
	uint32_t carrier_regs = 0;
	for (unsigned m = 0; m < ds->row.n_macros && m < 4; ++m)
	  carrier_regs |= carrier_load_regs[m];
	/* Only pinned rows use this; alternating rows own {0,1}.  */
	if (!ds->row.vd_alternates)
	  for (unsigned r = 0; r < 4 && vd == 8; ++r)
	    if (((first_touch_write >> r) & 1)
		&& !((carrier_regs >> r) & 1)
		&& !((name_reads_all_launched >> r) & 1))
	      vd = r;
	if (vd == 8)
	  return false;
      }
    ds->store_only_vd = vd;
  }
  /* An unabsorbed separator occupies the last issue slot and is not an
     SFPU-class instruction.  */
  bool kept_separator = row.separator && !schedule.absorbed_stride;
  ds->row.window_all_sfpu = !kept_separator;
  /* The kept separator would be recorded with unit_mask 0, which the
     hazard rule ignores (see above), so it is not recorded.  */

  /* Store mod0 source: the store-carrying launch encodes the mode of
     the access it carries, so a store-only carrier (or a merged
     carrier whose load shares the store's mode) takes the launch's
     Mod0; otherwise the misc StoreMod0 nibble carries it.  */
  ds->store_uses_carrier_mode = true;
  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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

  /* Dump the row spec given to the derivation core (event tuples and
     explicit floors), so that a refusal names its participants.  */
  if (dump_file)
    {
      for (unsigned e = 0; e < ds->row.n_events; ++e)
	{
	  const rvtt_macro_derive::event_spec &sp = ds->row.events[e];
	  fprintf (dump_file,
		   "Macro-planner derive-event %u: opcode=0x%02x%s "
		   "macro=%d carrier-slot=%d dep=0x%02x anti=0x%02x "
		   "tmpl-key=%d imm12=0x%03x war-floor+1=%d "
		   "consumer-slot+1=%d overwrite-slot+1=%d "
		   "issued-input-slot=%d reads-carrier-vd=%d\n",
		   e, sp.opcode, sp.is_store ? " (store)" : "",
		   sp.macro_index, sp.carrier_slot, sp.dep_mask,
		   sp.anti_dep_mask, sp.template_key, sp.template_imm12,
		   sp.war_floor_slot_p1, sp.issue_consumer_slot_p1,
		   sp.issue_overwrite_slot_p1,
		   sp.latest_issued_input_slot, sp.reads_carrier_vd_reg);
	}
      for (unsigned x = 0; x < ds->row.n_explicits; ++x)
	fprintf (dump_file,
		 "Macro-planner derive-explicit %u: slot=%d "
		 "unit-mask=0x%x\n",
		 x, ds->row.explicits[x].slot,
		 ds->row.explicits[x].unit_mask);
    }
  if (!rvtt_macro_derive::derive_calendar (c, ds->row, &ds->cal))
    {
      ds->refusal = ds->cal.refusal;
      return false;
    }
  /* Stride absorption in a derived calendar depends on the CPU
     (derived_stride_absorption_proven in rvtt-macro-tables.cc): BH and WH
     support it (WH through the single-slot Base=1 program); other CPUs
     are refused by name.  */
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

} /* anonymous namespace */

/* The store-only carrier's VD for a region whose internal registers are
   INTERNAL_LREGS: the lowest physical LREG outside the alternating pair
   and those registers, or 8 when none is free.  Used by both descriptor
   synthesis and the scheduler so that they agree.  */

unsigned
rvtt_macro_store_only_sacrificial_vd (uint32_t internal_lregs)
{
  uint32_t used = internal_lregs | 0x3u;
  unsigned vd = 2;
  while (vd < 8 && ((used >> vd) & 1))
    ++vd;
  return vd;
}

/* For the scheduler: the template fields (*OPCODE, *MOD1, *SRC_C,
   *IMM12) launched value INSN would use with launch VD LAUNCH_VD, or
   false when INSN is not in a supported class.  The scheduler counts
   distinct field tuples against the InstructionTemplate budget;
   descriptor synthesis recomputes everything.  */

bool
rvtt_macro_derived_template_probe (rtx_insn *insn, int launch_vd,
				   uint8_t *opcode, uint8_t *mod1,
				   uint8_t *src_c, uint16_t *imm12)
{
  uint8_t sc = 0, m = 0;
  if (swap_cst_template_fields (insn, &sc, &m))
    {
      *opcode = source_opcode_byte (insn);
      *mod1 = m;
      *src_c = sc;
      *imm12 = 0;
      return true;
    }
  derived_template_fields tf;
  if (!derived_value_template_fields (insn, launch_vd, &tf))
    return false;
  *opcode = tf.opcode;
  *mod1 = tf.mod1;
  *src_c = tf.src_c;
  *imm12 = tf.imm12;
  return true;
}

/* Return the sub-unit (an xtt_subunit_t value) a launched realization
   of INSN would occupy.  The in-place immediate shift runs on Round as an
   SHFT2 template; everything else uses its own sub-unit.  */

int
rvtt_macro_hosted_subunit (rtx_insn *insn)
{
  if (value_insn_unspecv (insn) == UNSPECV_SFPSHFT)
    {
      rtx pat = PATTERN (insn);
      rtx set = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
      if (GET_CODE (set) == SET)
	{
	  rtx un = SET_SRC (set);
	  if (GET_CODE (un) == UNSPEC_VOLATILE && XVECLEN (un, 0) == 7
	      && CONST_INT_P (XVECEXP (un, 0, 3))
	      && CONST_INT_P (XVECEXP (un, 0, 6))
	      && INTVAL (XVECEXP (un, 0, 6))
		 == (TARGET_XTT_TENSIX_WH ? 1 : 5))
	    return XTT_SU_ROUND;
	}
    }
  return (int) rvtt_macro_insn_effects (insn).subunit;
}

/* Free the heap storage owned by DESC (the launch vector).  */

void
rvtt_macro_descriptor_release (macro_descriptor *desc)
{
  desc->launches.release ();
}

/* Synthesize the macro descriptor for REGION under SCHEDULE into *OUT:
   match a verified whole-word descriptor program, or derive the calendar
   from the schedule and the capability tables.  Return false when
   synthesis cannot start (no capability table, or the schedule carries a
   refusal other than the exceptions below); otherwise return true with
   *OUT complete or with a refusal name in OUT->refusal.  Descriptor
   words and refusals go to DUMP when non-null.  The function is never
   modified.  */

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

  /* A schedule that carries a refusal is not synthesized, with three
     exceptions: event-delay-unproven (a whole-word program does not need
     per-event delays), sequence-encoding-unproven (the missing sequence
     program is what derivation produces, rvtt-macro-derive-core.h), and
     template-capacity-exceeded (derive_row rechecks capacity after
     template sharing).  Every other refusal stands, including a compact
     candidate that failed the mandatory stride absorption (its structure
     must not match a program meant for a different calendar) and
     latency/port violations that no descriptor can fix.  */
  if (schedule.refusal
      && schedule.refusal != macro_sched_refusal_event_delay_unproven
      && schedule.refusal != macro_sched_refusal_sequence_encoding_unproven
      && schedule.refusal != macro_sched_refusal_template_capacity_exceeded)
    return false;

  derived_structure derived;
  const desc_program *program = nullptr;
  if (derive_structure (region, schedule, &derived))
    program = find_program (derived, schedule.absorb_into_explicit);
  if (!prng_program_preserves_issue_p (region, schedule, derived, program))
    {
      out->refusal = "prng-issue-order-unproven";
      rvtt_refuse_by_name (out->refusal, dump,
			   "Macro-planner descriptor-refusal: %s\n",
			   out->refusal);
      return true;
    }
  if (program && program->uniform_mode_required)
    {
      /* The programs have only been verified with one data mode across
         the row's Dst accesses.  */
      rtx first_mode = nullptr;
      for (rtx_insn *insn : region.rows[0].insns)
	{
	  xtt_effect_set e = rvtt_macro_insn_effects (insn);
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
      /* No whole-word program matches: derive the calendar from the
         schedule.  Rows outside the supported template classes, or failing
         any derivation check, are refused by name.  */
      derived_synthesis ds;
      if (derive_row (region, schedule, c, &ds))
	{
	  /* Templates: the packed fields of the source events plus the staging
	     copy; positional dest selectors.  */
	  out->n_templates = ds.n_templates;
	  for (unsigned t = 0; t != ds.n_templates; ++t)
	    {
	      template_spec spec;
	      spec.opcode = ds.template_opcode[t];
	      spec.imm12 = ds.template_imm12[t];
	      spec.src_c = ds.template_src_c[t];
	      spec.dest_sel = 0xc + t;
	      spec.mod1 = ds.template_mod1[t];
	      if (!encode_template (c, spec, &out->templ[t]))
		{
		  out->refusal = macro_desc_refusal_encoding_failed;
		  rvtt_refuse_by_name (out->refusal, dump,
				       "Macro-planner descriptor-refusal: %s\n",
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
		  rvtt_refuse_by_name (out->refusal, dump,
				       "Macro-planner descriptor-refusal: %s\n",
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
		  xtt_effect_set e = rvtt_macro_insn_effects (drow.insns[ix]);
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
	      launch.is_store_only = is_store_only;
	      if (is_store_only)
		{
		  launch.vd = ds.store_only_vd;
		  launch.vd_alternates = false;
		}
	      else if (ds.vd_pin[m] >= 0)
		{
		  /* Fixed launch VD: the carrier keeps its own physical load
		     destination (checked by the derivation core).  */
		  launch.vd = (unsigned) ds.vd_pin[m];
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
		  rvtt_refuse_by_name (out->refusal, dump,
				       "Macro-planner descriptor-refusal: %s\n",
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
      rvtt_refuse_by_name (out->refusal, dump,
			   "Macro-planner descriptor-refusal: %s\n",
			   out->refusal);
      return true;
    }

  /* A CC-template program must pass all of derive_cc_model -- the
     definition/merge/restore dataflow, the deferred-visibility slots, the
     restore-before-store constraint, and the single data mode -- before
     any word is packed.  */
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
      rvtt_refuse_by_name (out->refusal, dump,
			   "Macro-planner descriptor-refusal: %s\n",
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
	      rvtt_refuse_by_name (out->refusal, dump,
				   "Macro-planner descriptor-refusal: %s\n",
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
	    /* Any immediate that the 12-bit field can represent is packed;
	       anything else is refused rather than masked.  */
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
	    /* The sense must be a register test whether or not it is
	       complemented; the complement mapping comes from the capability
	       tables.  */
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
	  rvtt_refuse_by_name (out->refusal, dump,
			       "Macro-planner descriptor-refusal: %s\n",
			       out->refusal);
	  return true;
	}
      out->templ[t] = word;
    }

  /* Sequences and misc: whole words looked up by the matched program's
     labels in the capability tables.  */
  out->n_seq = program->n_macros;
  for (unsigned m = 0; m != program->n_macros; ++m)
    if (!program->seq_names[m]
	|| !find_seq_word (c, program->seq_names[m], &out->seq[m]))
      {
	out->refusal = macro_desc_refusal_program_unproven;
	rvtt_refuse_by_name (out->refusal, dump,
			     "Macro-planner descriptor-refusal: %s\n",
			     out->refusal);
	return true;
      }
  if (program->misc_from_store_mode)
    {
      /* Select misc word 0x700 | StoreMod0, from the row's shared
         payload/store data mode.  */
      if (!rvtt_macro::encode_misc_select (c, (unsigned) cc_store_mode,
					   &out->misc))
	{
	  out->refusal = macro_desc_refusal_encoding_failed;
	  rvtt_refuse_by_name (out->refusal, dump,
			       "Macro-planner descriptor-refusal: %s\n",
			       out->refusal);
	  return true;
	}
    }
  else if (!find_misc_word (c, program->misc_name, &out->misc))
    {
      out->refusal = macro_desc_refusal_program_unproven;
      rvtt_refuse_by_name (out->refusal, dump,
			   "Macro-planner descriptor-refusal: %s\n",
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
	  rvtt_refuse_by_name (out->refusal, dump,
			       "Macro-planner descriptor-refusal: %s\n",
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
	    xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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
	launch.is_store_only = is_store_only;
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
	    rvtt_refuse_by_name (out->refusal, dump,
				 "Macro-planner descriptor-refusal: %s\n",
				 out->refusal);
	    return true;
	  }
	out->launches.safe_push (launch);
      }
  }

  /* Drain: the table's drain for whole-word programs (the general
     greatest-remaining-delay rule gives the same value for each of
     them).  */
  out->drain_slots = c->proven_drain_slots;
  out->needs_all_lanes_prefix = region.net.cc_read;

  /* Physical LREGs used by the planner: the planned VDs, staging and
     source fields, plus every hidden write the templates perform.  */
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

   The template, sequence and misc expectations come from the same
   desc_programs table that synthesis reads, so the verifier catches
   packing and encoding errors but not a wrong table entry; tests that
   check the emitted descriptor words catch those.  Launch words and
   SETC16 slot programs are re-derived here independently of
   synthesis.  */
/* ------------------------------------------------------------------ */

#include "rvtt-macro-verify-core.h"

/* Assemble the verifier's expectation set for REGION under SCHEDULE
   into *OUT, matching the program again (or re-running the derivation)
   from the region's explicit facts and re-deriving the launch words and
   SETC16 slot program independently.  Return false when no expectation
   set can be built (no capability table, or the derivation refuses).  */

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
  const desc_program *program = nullptr;
  if (derive_structure (region, schedule, &derived))
    program = find_program (derived, schedule.absorb_into_explicit);
  if (program && program->uniform_mode_required)
    {
      /* Apply the same data-mode filter as synthesis, so that the
         expectations never match a different program than synthesis did
         (otherwise a mixed-mode row would use the derived calendar in
         synthesis but the uniform-mode program here).  */
      rtx first_mode = nullptr;
      for (rtx_insn *insn : region.rows[0].insns)
	{
	  xtt_effect_set e = rvtt_macro_insn_effects (insn);
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
      /* Derived-calendar expectations: re-run the derivation from the
         region's explicit facts.  The same-table limitation above applies;
         the derivation itself is tested separately by
         rvtt-macro-derive-test.cc against recorded descriptor words.  */
      derived_synthesis ds;
      if (!derive_row (region, schedule, c, &ds))
	return false;
      out->n_templates = ds.n_templates;
      for (unsigned t = 0; t != ds.n_templates; ++t)
	{
	  rvtt_macro_verify::expect_template &e = out->templates[t];
	  e.whole_word = false;
	  e.opcode = ds.template_opcode[t];
	  e.imm12 = ds.template_imm12[t];
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
	      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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
	  a.vd = is_store_only ? ds.store_only_vd
	    : ds.vd_pin[m] >= 0 ? (unsigned) ds.vd_pin[m] : 0;
	  ++out->n_accesses;
	}

      out->planned_lregs = region.internal_lregs | 0x3u
	| (1u << ds.store_only_vd);
      /* Hidden template writes are checked through the synthesized words
         in verify(); record the ownership expectation here.  */
      for (unsigned t = 0; t != ds.n_templates; ++t)
	{
	  template_spec spec;
	  spec.opcode = ds.template_opcode[t];
	  spec.imm12 = ds.template_imm12[t];
	  spec.src_c = ds.template_src_c[t];
	  spec.dest_sel = 0xc + t;
	  spec.mod1 = ds.template_mod1[t];
	  uint32_t word = 0;
	  if (encode_template (c, spec, &word))
	    out->planned_lregs |= template_hidden_lreg_writes (c, word);
	}
      return true;
    }

  /* CC-template expectations, re-derived from the region's
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
	  xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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

/* Output helper for the rvtt_owned_setc16 pattern, which carries the
   fields; the word is packed here from the capability tables.  */

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

/* ------------------------------------------------------------------ */
/* Descriptor-program residency (-mtt-tensix-macro-planner-residency,
   default off; interface in rvtt-macro-desc.h).  A region's descriptor
   words need not be written again when an identical program, written
   earlier, dominates the region and nothing in between can change the
   owned state (rvtt_macro_epoch_owned_state_invariant_p in
   rvtt-macro-epoch.cc).  The first program formed is kept and never
   evicted; since only identical programs are shared, the descriptor
   registers are never contended.  Every refusal leaves the placement
   unchanged.  */

const char *macro_resid_refusal_skip_path = "resid-skip-path-unproven";
const char *macro_resid_refusal_span = "resid-span-unproven";
const char *macro_resid_refusal_dominance = "resid-dominance-unproven";

namespace {

/* Return whether A and B have identical descriptor words.  SETC16
   programs and launch tuples are not compared: they are emitted per
   region regardless of residency, and launches only read the resident
   state.  */

static bool
residency_content_equal (const macro_residency_entry &e,
			 const macro_descriptor &d)
{
  if (e.n_templates != d.n_templates || e.n_seq != d.n_seq
      || e.has_misc != d.has_misc)
    return false;
  for (unsigned t = 0; t != d.n_templates; ++t)
    if (e.templ[t] != d.templ[t])
      return false;
  for (unsigned m = 0; m != d.n_seq; ++m)
    if (e.seq[m] != d.seq[m])
      return false;
  if (d.has_misc && e.misc != d.misc)
    return false;
  return true;
}

/* Build the set of insns the invariance walks ignore: every insn this
   planner invocation emitted (their owned-dest writes are the residency
   candidates themselves) plus REGION's own members (deleted or
   re-expressed by emission).  */

static void
residency_benign_set (const macro_region &region,
		      macro_residency_state *state,
		      hash_set<rtx_insn *> *benign)
{
  for (hash_set<rtx_insn *>::iterator it = state->emitted.begin ();
       it != state->emitted.end (); ++it)
    benign->add (*it);
  for (const macro_row &row : region.rows)
    {
      if (row.enable)
	benign->add (row.enable);
      if (row.separator)
	benign->add (row.separator);
      for (rtx_insn *member : row.insns)
	benign->add (member);
    }
  for (rtx_insn *sep : region.run_separators)
    benign->add (sep);
}

} /* anonymous namespace */

/* See rvtt-macro-desc.h.  */

bool
rvtt_macro_residency_extend (function *fn, const macro_region &region,
			     const macro_descriptor &desc,
			     const rvtt_macro::caps *c,
			     macro_residency_state *state,
			     basic_block *hoist_preheader, edge *hoist_edge,
			     unsigned *levels, FILE *dump)
{
  (void) desc;
  *levels = 0;
  if (!riscv_tt_macro_planner_residency)
    return false;
  if (!*hoist_preheader && !*hoist_edge)
    return false;

  /* The resident program may also execute on paths that never reach the
     region (guarded loops), so no instruction of the function outside the
     ignored set may read or redefine the owned state.  The enable word
     re-establishes the all-lanes state, which cc_enable_all_lanes_proved_p
     checked at formation.  */
  hash_set<rtx_insn *> benign;
  residency_benign_set (region, state, &benign);
  rtx_insn *skip_insn = nullptr;
  if (const char *why
	= rvtt_macro_epoch_owned_state_invariant_p (fn, benign, c,
						    &skip_insn))
    {
      rvtt_refuse_by_name (macro_resid_refusal_skip_path, dump,
			   "Macro-planner residency-refusal: %s (%s%s%d)\n",
			   macro_resid_refusal_skip_path, why,
			   skip_insn ? ", insn " : "",
			   skip_insn ? INSN_UID (skip_insn) : 0);
      return false;
    }

  /* Move the placement outward one loop at a time.  Each level repeats the
     full configuration-epoch check (including the conservative refusal of
     other LREG/CC dataflow) over the next enclosing loop's body; a level
     without a unique entry, or any refusal, stops at the last valid
     placement.  An outer placement never executes more often than an inner
     one, so no benefit threshold is needed.  */
  basic_block preheader = *hoist_preheader;
  edge entry = *hoist_edge;
  for (;;)
    {
      basic_block place = preheader ? preheader : entry->src;
      if (!place || place == ENTRY_BLOCK_PTR_FOR_FN (fn))
	break;
      basic_block ph = nullptr;
      edge e = nullptr;
      const char *why = nullptr;
      rtx_insn *why_insn = nullptr;
      if (!rvtt_macro_prefix_epoch_hoist (fn, region, place, c, &ph, &e,
					  &why, &why_insn))
	{
	  if (why && dump)
	    {
	      fprintf (dump, "Macro-planner residency-extension-stop: %s",
		       why);
	      if (why_insn)
		fprintf (dump, " (insn %d)", INSN_UID (why_insn));
	      fprintf (dump, "\n");
	    }
	  break;
	}
      preheader = ph;
      entry = e;
      ++*levels;
    }
  if (!*levels)
    return false;
  *hoist_preheader = preheader;
  *hoist_edge = entry;
  if (dump)
    fprintf (dump, "Macro-planner residency: descriptor program resident"
	     " %u enclosing level(s) further out (owned-state invariance"
	     " function-wide; placement executions monotone"
	     " non-increasing)\n", *levels);
  return true;
}

/* See rvtt-macro-desc.h.  */

bool
rvtt_macro_residency_lookup (function *fn, const macro_region &region,
			     const macro_descriptor &desc,
			     const rvtt_macro::caps *c,
			     macro_residency_state *state, FILE *dump)
{
  if (!riscv_tt_macro_planner_residency || state->programmed.is_empty ())
    return false;

  /* The dominance target is the launch site itself: region discovery
     collects a region's rows within one basic block, so the first
     row's block covers every launch the elision must feed.  */
  const macro_row &first = region.rows[0];
  rtx_insn *anchor = first.enable ? first.enable : first.insns[0];
  basic_block anchor_bb = BLOCK_FOR_INSN (anchor);

  for (const macro_residency_entry &e : state->programmed)
    {
      if (!residency_content_equal (e, desc))
	continue;

      /* The resident program must dominate this region's configuration
	 anchor.  Same-block domination is program order: regions are
	 discovered in forward program order, so an earlier entry's
	 programming precedes this region's rows within the block.  */
      bool free_dom = !dom_info_available_p (CDI_DOMINATORS);
      calculate_dominance_info (CDI_DOMINATORS);
      bool dominates
	= dominated_by_p (CDI_DOMINATORS, anchor_bb, e.placement);
      if (free_dom)
	free_dominance_info (CDI_DOMINATORS);
      if (!dominates)
	{
	  rvtt_refuse_by_name (macro_resid_refusal_dominance, dump,
			       "Macro-planner residency-refusal: %s\n",
			       macro_resid_refusal_dominance);
	  continue;
	}

      /* No other instruction in the function may redefine the owned state,
         so in particular nothing between the resident program and this
         region's launches; omitting an identical rewrite then cannot be
         observed.  */
      hash_set<rtx_insn *> benign;
      residency_benign_set (region, state, &benign);
      rtx_insn *span_insn = nullptr;
      if (const char *why
	    = rvtt_macro_epoch_owned_state_invariant_p (fn, benign, c,
							&span_insn))
	{
	  rvtt_refuse_by_name (macro_resid_refusal_span, dump,
			       "Macro-planner residency-refusal: %s (%s%s%d)\n",
			       macro_resid_refusal_span, why,
			       span_insn ? ", insn " : "",
			       span_insn ? INSN_UID (span_insn) : 0);
	  return false;	/* the walk is entry-independent: stop */
	}

      if (dump)
	{
	  unsigned words
	    = desc.n_templates + desc.n_seq + (desc.has_misc ? 1 : 0);
	  fprintf (dump, "Macro-planner residency: descriptor program"
		   " content-identical to a dominating resident program;"
		   " %u descriptor words elided for this region"
		   " (delivery priced by the delivery-cost model, 1.23"
		   " slot-equivalents per pushed word)\n", words);
	}
      return true;
    }
  return false;
}

/* See rvtt-macro-desc.h.  */

void
rvtt_macro_residency_record (const macro_descriptor &desc,
			     basic_block placement,
			     macro_residency_state *state)
{
  if (!riscv_tt_macro_planner_residency || !placement)
    return;
  macro_residency_entry e;
  memset (&e, 0, sizeof (e));
  e.n_templates = desc.n_templates;
  for (unsigned t = 0; t != desc.n_templates; ++t)
    e.templ[t] = desc.templ[t];
  e.n_seq = desc.n_seq;
  for (unsigned m = 0; m != desc.n_seq; ++m)
    e.seq[m] = desc.seq[m];
  e.has_misc = desc.has_misc;
  e.misc = desc.misc;
  e.placement = placement;
  state->programmed.safe_push (e);
}
