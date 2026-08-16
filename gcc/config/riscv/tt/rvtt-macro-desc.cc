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
  /* Proven whole word (internal event; field semantics 9(d)/9(e)
     partial).  */
  TR_WHOLE_WORD,
  /* Proven whole word whose imm12 field is packed from the source's
     typed immediate operand (established: the field carries exactly the
     explicit shift amount; frozen :469, :852).  */
  TR_WHOLE_WORD_IMM12_FROM_SOURCE,
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
  uint32_t word;		/* whole-word payloads (imm12 zeroed)  */
  uint8_t src_c_plan;		/* planned physical src field	       */
  int source_event;		/* flat value-event index; -1 internal */
  int mod1_op;			/* source operand carrying mod1	       */
  int imm12_op;			/* source operand carrying imm12; -1   */
  uint8_t routing_mod_bit;	/* TR_.._ROUTING_MOD only	       */
};

struct desc_program
{
  const char *provenance;
  unsigned n_macros;
  desc_macro_key macros[2];
  unsigned n_templates;
  desc_template_rule templates[2];
  uint32_t seq_words[2];
  uint32_t misc_word;
  int fixed_vd;			/* -1: alternating pair {0,1}	       */
  unsigned store_only_vd;	/* VD of a store-only carrier	       */
  /* CRAQ-validated envelope: the program was proven only with one
     uniform data mode across every Dst access of the row.  */
  bool uniform_mode_required;
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
    { { TR_FIELDS_FROM_SOURCE_ROUTING_MOD, 0, 2 /* planned RHS L2 */,
	0, 4 /* swap mod1 operand */, -1, 8 },
      { TR_WHOLE_WORD, 0x940000d6, 0, -1, -1, -1, 0 } },
    { 0x00dd008c, 0x53000000 },
    0x00000330,
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
    { { TR_WHOLE_WORD_IMM12_FROM_SOURCE, 0x940000c6, 0, 0, -1,
	4 /* shift imm operand */, 0 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 1, 3 /* cast mod1 operand */, -1, 0 } },
    { 0x5384004d, 0 },
    0x00000110,
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
    { { TR_FIELDS_FROM_SOURCE, 0, 0, 0, 3 /* cast mod1 */, -1, 0 },
      { TR_FIELDS_FROM_SOURCE, 0, 0, 1, 8 /* stochrnd mod1 */, -1, 0 } },
    { 0x534d0004, 0 },
    0x00000100,
    -1, 0, false,
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

  /* Templates.  */
  out->n_templates = program->n_templates;
  for (unsigned t = 0; t != program->n_templates; ++t)
    {
      const desc_template_rule &rule = program->templates[t];
      uint32_t word = 0;
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
	case TR_WHOLE_WORD:
	  word = rule.word;
	  break;
	case TR_WHOLE_WORD_IMM12_FROM_SOURCE:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT imm12 = 0;
	    if (!const_operand (src, rule.imm12_op, &imm12))
	      out->refusal = macro_desc_refusal_encoding_failed;
	    else
	      word = rule.word | ((uint32_t) (imm12 & 0xfff) << 12);
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

  /* Sequences and misc: proven whole words of the matched program.  */
  out->n_seq = program->n_macros;
  for (unsigned m = 0; m != program->n_macros; ++m)
    out->seq[m] = program->seq_words[m];
  out->misc = program->misc_word;
  out->has_misc = true;

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
   independently assembled expectation set.			       */
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

  /* Template expectations.  */
  out->n_templates = program->n_templates;
  for (unsigned t = 0; t != program->n_templates; ++t)
    {
      const desc_template_rule &rule = program->templates[t];
      rvtt_macro_verify::expect_template &e = out->templates[t];
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
	case TR_WHOLE_WORD:
	  e.whole_word = true;
	  e.word = rule.word;
	  break;
	case TR_WHOLE_WORD_IMM12_FROM_SOURCE:
	  {
	    rtx_insn *src = derived.value_insns[rule.source_event];
	    HOST_WIDE_INT imm12 = 0;
	    if (!const_operand (src, rule.imm12_op, &imm12))
	      return false;
	    e.whole_word = true;
	    e.word = rule.word | ((uint32_t) (imm12 & 0xfff) << 12);
	  }
	  break;
	}
    }

  /* Sequence/misc expectations from the matched program.  */
  out->n_seq = program->n_macros;
  for (unsigned m = 0; m != program->n_macros; ++m)
    out->seq_words[m] = program->seq_words[m];
  out->misc = program->misc_word;
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
