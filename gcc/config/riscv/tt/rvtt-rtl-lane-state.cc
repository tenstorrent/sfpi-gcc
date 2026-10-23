/* Shared RTL lane-enable proof.  Copyright (C) 2026 Tenstorrent Inc.
   This file is part of GCC, distributed under the GNU General Public
   License version 3 or later.  See COPYING3.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "emit-rtl.h"
#include "df.h"
#include "function.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-mop-tables.h"

#include "rvtt-rtl-lane-state.h"

/* How an instruction can change which SFPU lanes are enabled.  */


/* The lane effect of raw instruction word WORD.  */

static bool rtl_mop_launches_proven_p (function *, rtx_insn *, uint32_t);
static bool word_direct_issue_p (uint32_t, rtx_insn * = nullptr);

/* Basic asm is ASM_INPUT, often wrapped with a memory clobber.  Scalar
   fences need the same exact-spelling proof as extended ASM_OPERANDS.  */
static const char *
lane_asm_template (rtx pat)
{
  if (GET_CODE (pat) == ASM_INPUT)
    return XSTR (pat, 0);
  if (GET_CODE (pat) == ASM_OPERANDS)
    return ASM_OPERANDS_TEMPLATE (pat);
  if (GET_CODE (pat) == PARALLEL)
    {
      const char *templ = nullptr;
      for (int i = 0; i < XVECLEN (pat, 0); ++i)
	{
	  rtx part = XVECEXP (pat, 0, i);
	  if (GET_CODE (part) == CLOBBER || GET_CODE (part) == USE)
	    continue;
	  const char *found = lane_asm_template (part);
	  if (!found || templ)
	    return nullptr;
	  templ = found;
	}
      return templ;
    }
  return nullptr;
}

static rvtt_rtl_lane_effect
raw_word_lane_effect (uint32_t word, rtx_insn *at)
{
  if ((word >> 24) == 0x01 && rtl_mop_launches_proven_p (cfun, at, word))
    return LANES_KEPT;
  if (TARGET_XTT_TENSIX_WH_BH)
    {
      if (word == 0x8a00300a)
	return LANES_ALL;
      if (word == 0x910000f1)
	return LANES_ROW_MASK_CLEAR;
    }
  switch (rvtt_raw_cc_word_class (word))
    {
    case RVTT_RAW_CC_INERT:
      return LANES_KEPT;
    case RVTT_RAW_CC_ALL_LANES:
      return LANES_KEEP_ALL;
    default:
      /* These opcodes update lane flags, never LaneConfig.  Their
	 individual modes remain unproved as flag values.  */
      if (TARGET_XTT_TENSIX_WH_BH)
	switch (word >> 24)
	  {
	  case 0x77: case 0x79: case 0x7b: case 0x81:
	  case 0x87: case 0x88: case 0x8a: case 0x8b:
	  case 0x96: case 0x97:
	    return LANES_FLAGS_ANY;
	  }
      return LANES_ANY;
    }
}

/* Possible one bits of a scalar field, resolving actual same-block
   definitions at their use rather than assuming a TT_OP field discipline.  */
static uint32_t
runtime_field_bits (rtx value, rtx_insn *at, unsigned depth = 0)
{
  uint32_t bits = nonzero_bits (value, SImode);
  if (depth > 8)
    return bits;
  if (REG_P (value))
    for (rtx_insn *def = PREV_INSN (at);
	 def && BLOCK_FOR_INSN (def) == BLOCK_FOR_INSN (at);
	 def = PREV_INSN (def))
      if (NONDEBUG_INSN_P (def) && reg_set_p (value, def))
	{
	  rtx set = single_set (def);
	  return set && rtx_equal_p (SET_DEST (set), value)
	    ? bits & runtime_field_bits (SET_SRC (set), def, depth + 1) : bits;
	}
  if (GET_CODE (value) == AND)
    return bits & runtime_field_bits (XEXP (value, 0), at, depth + 1)
      & runtime_field_bits (XEXP (value, 1), at, depth + 1);
  if (GET_CODE (value) == IOR || GET_CODE (value) == XOR)
    return bits & (runtime_field_bits (XEXP (value, 0), at, depth + 1)
		   | runtime_field_bits (XEXP (value, 1), at, depth + 1));
  if ((GET_CODE (value) == ASHIFT || GET_CODE (value) == LSHIFTRT)
      && CONST_INT_P (XEXP (value, 1)))
    {
      unsigned shift = UINTVAL (XEXP (value, 1));
      if (shift < 32)
	{
	  uint32_t source = runtime_field_bits (XEXP (value, 0), at, depth + 1);
	  return bits & (GET_CODE (value) == ASHIFT
			 ? source << shift : source >> shift);
	}
    }
  return bits;
}

/* The word that volatile store INSN pushes, the shape of a TT_
   instruction-buffer issue.  Returns false if it is unknown.  Otherwise
   *EXACT says whether *WORD is the whole word; if not, *WORD is the
   constant part of a REG_EQUAL constant plus bounded runtime fields.
   The upper opcode/register/modifier bits must be proved unchanged; raw
   metadata is not a range contract for an arbitrary scalar expression.  */

static bool
stored_word (rtx_insn *insn, uint32_t *word, bool *exact)
{
  rtx value = SET_SRC (single_set (insn));
  rtx equal = NULL_RTX;
  rtx_insn *equal_at = insn;
  if (REG_P (value))
    {
    for (rtx_insn *def = PREV_INSN (insn);
	 def && BLOCK_FOR_INSN (def) == BLOCK_FOR_INSN (insn);
	 def = PREV_INSN (def))
      if (NONDEBUG_INSN_P (def) && reg_set_p (value, def))
	{
	  rtx def_set = single_set (def);
	  rtx note = find_reg_equal_equiv_note (def);
	  if (note)
	    equal = XEXP (note, 0);
	  else if (def_set && rtx_equal_p (SET_DEST (def_set), value))
	    equal = SET_SRC (def_set);
	  equal_at = def;
	  break;
	}
    }
  else
    equal = value;
  if (!equal)
    return false;
  *exact = CONST_INT_P (equal);
  if (!*exact
      && (GET_CODE (equal) == PLUS || GET_CODE (equal) == IOR)
      && CONST_INT_P (XEXP (equal, 1)))
    {
      rtx runtime = XEXP (equal, 0);
      unsigned HOST_WIDE_INT bits = runtime_field_bits (runtime, equal_at);
      /* Recognize a same-block explicit mask/extension immediately
	 feeding the word construction.  Resolve at the construction, not
	 the store: a source register can be reassigned in between.  */
      if (REG_P (runtime))
	for (rtx_insn *def = PREV_INSN (equal_at);
	     def && BLOCK_FOR_INSN (def) == BLOCK_FOR_INSN (equal_at);
	     def = PREV_INSN (def))
	  if (NONDEBUG_INSN_P (def) && reg_set_p (runtime, def))
	    {
	      rtx set = single_set (def);
	      if (set && rtx_equal_p (SET_DEST (set), runtime))
		bits &= nonzero_bits (SET_SRC (set), SImode);
	      break;
	    }
      unsigned HOST_WIDE_INT base = UINTVAL (XEXP (equal, 1));
      if ((bits & 0xffff0000u)
	  || (GET_CODE (equal) == PLUS && (bits & base)))
	return false;
      equal = XEXP (equal, 1);
    }
  if (!CONST_INT_P (equal))
    return false;
  *word = UINTVAL (equal);
  return true;
}

/* Prove delivery, rather than merely recognizing the value being stored.
   Only word zero of the instruction FIFO issues a word.  In particular an
   arbitrary pointer argument (or another offset in its aperture) is not an
   instruction-delivery contract.  Resolve registers at their use point.  */

static bool
known_store_address (rtx addr, rtx_insn *at, uint32_t *value,
		     unsigned depth = 0)
{
  if (depth > 6)
    return false;
  if (CONST_INT_P (addr))
    {
      *value = UINTVAL (addr);
      return true;
    }
  if (GET_CODE (addr) == SYMBOL_REF)
    {
      tree decl = SYMBOL_REF_DECL (addr);
      bool fifo = decl && VAR_P (decl) && DECL_EXTERNAL (decl)
	&& TREE_PUBLIC (decl)
	&& rvtt_instrn_buffer_name_p (XSTR (addr, 0));
      if (fifo)
	*value = XTT_INSTRN_BUF_MMIO_BASE;
      return fifo;
    }
  if (GET_CODE (addr) == CONST)
    return known_store_address (XEXP (addr, 0), at, value, depth + 1);
  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1))
      && known_store_address (XEXP (addr, 0), at, value, depth + 1))
    {
      *value += UINTVAL (XEXP (addr, 1));
      return true;
    }
  if (REG_P (addr))
    for (rtx_insn *def = PREV_INSN (at);
	 def && BLOCK_FOR_INSN (def) == BLOCK_FOR_INSN (at);
	 def = PREV_INSN (def))
      if (NONDEBUG_INSN_P (def) && reg_set_p (addr, def))
	{
	  rtx set = single_set (def);
	  rtx note = find_reg_equal_equiv_note (def);
	  rtx source = note ? XEXP (note, 0)
	    : set && rtx_equal_p (SET_DEST (set), addr)
	      ? SET_SRC (set) : NULL_RTX;
	  return source && known_store_address (source, def, value, depth + 1);
	}
  return false;
}

static bool
instruction_fifo_address_p (rtx addr, rtx_insn *at)
{
  uint32_t value;
  return known_store_address (addr, at, &value)
    && value == XTT_INSTRN_BUF_MMIO_BASE;
}

/* Writing the nine architected MOP template registers programs a future
   launch; it does not itself issue their data as instruction words.  */
static bool
mop_template_store_p (rtx_insn *insn)
{
  rtx set = single_set (insn);
  uint32_t address;
  return set && MEM_P (SET_DEST (set)) && MEM_VOLATILE_P (SET_DEST (set))
    && known_store_address (XEXP (SET_DEST (set), 0), insn, &address)
    && address >= XTT_MOP_CFG_MMIO_BASE
    && address <= XTT_MOP_CFG_MMIO_BASE + 8 * 4 && !(address & 3);
}

/* This is a current-CFG must proof, not a retained function permission.
   All nine physical template registers must have valid known contents
   before EVERY launch.  An earlier unproved launch could start recording,
   so it invalidates the entire proof, including later apparent writes.  */
static bool
rtl_mop_launches_proven_p (function *fn, rtx_insn *query, uint32_t query_word)
{
  /* Retained MAY facts are a veto only: a poisoned/unavailable source
     closure cannot grant permission, and a clean closure still needs
     the current instruction's complete must-initialization proof below.  */
  if (!fn || !fn->cfg || !rvtt_mop_lane_proof_hook
      || !rvtt_mop_lane_proof_hook (fn))
    return false;
  const unsigned all = 0x1ff;
  auto_vec<unsigned> out;
  out.safe_grow (last_basic_block_for_fn (fn));
  for (unsigned i = 0; i != out.length (); ++i)
    out[i] = all;
  auto input = [&] (basic_block bb) -> unsigned {
    unsigned mask = EDGE_COUNT (bb->preds) ? all : 0;
    edge e;
    edge_iterator ei;
    FOR_EACH_EDGE (e, ei, bb->preds)
    mask &= (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn)
	     || (e->flags & (EDGE_ABNORMAL | EDGE_EH)))
		? 0
		: out[e->src->index];
    return mask;
  };
  bool bad_channel = false;
  auto step = [&] (rtx_insn *insn, unsigned mask) -> unsigned {
    if (!NONDEBUG_INSN_P (insn))
      return mask;
    if (CALL_P (insn))
      {
	if (!rvtt_callee_direct_issue_p (get_call_fndecl (insn)))
	  bad_channel = true;
	return 0;
      }
    rtx set = single_set (insn);
    if (set && MEM_P (SET_DEST (set)))
      {
	uint32_t addr, word;
	bool exact;
	if (!known_store_address (XEXP (SET_DEST (set), 0), insn, &addr))
	  {
	    bad_channel = true;
	    return 0;
	  }
	if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	  {
	    if (addr > XTT_MOP_CFG_MMIO_BASE + 8 * 4 || (addr & 3)
		|| !known_eq (GET_MODE_SIZE (GET_MODE (SET_DEST (set))), 4))
	      {
		bad_channel = true;
		return 0;
	      }
	    unsigned slot = (addr - XTT_MOP_CFG_MMIO_BASE) / 4;
	    bool valid = stored_word (insn, &word, &exact) && exact;
	    valid = valid && rvtt_mop_lane_slot_word_p (slot, word);
	    return valid ? mask | (1u << slot) : mask & ~(1u << slot);
	  }
	if (addr < XTT_INSTRN_BUF_MMIO_BASE
	    || addr > XTT_INSTRN_BUF_MMIO_LIMIT)
	  return mask;
      }
    uint32_t word;
    if (rvtt_rtl_raw_insn_fields (insn, &word))
      {
	if ((word >> 24) == XTT_MOP_OPCODE)
	  return mask;
	if (!word_direct_issue_p (word))
	  {
	    bad_channel = true;
	    return 0;
	  }
	return mask;
      }
    if (asm_noperands (PATTERN (insn)) >= 0
	|| lane_asm_template (PATTERN (insn)))
      {
	const char *s = lane_asm_template (PATTERN (insn));
	if (s)
	  s += strspn (s, " \t");
	if (!s || (*s && strcmp (s, "fence") && strcmp (s, "fence rw,rw")))
	  {
	    bad_channel = true;
	    return 0;
	  }
      }
    else if (recog_memoized (insn) == CODE_FOR_rvtt_ttinsn_int
	     || (recog_memoized (insn) >= 0
		 && get_attr_type (insn) == TYPE_TENSIX
		 && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER))
      {
	bad_channel = true;
	return 0;
      }
    return mask;
  };
  basic_block bb;
  bool changed;
  do
    {
      changed = false;
      FOR_EACH_BB_FN (bb, fn)
      {
	unsigned mask = input (bb);
	rtx_insn *insn;
	FOR_BB_INSNS (bb, insn) mask = step (insn, mask);
	if (mask != out[bb->index])
	  out[bb->index] = mask, changed = true;
      }
    }
  while (changed);
  bool saw_query = false;
  FOR_EACH_BB_FN (bb, fn)
  {
    unsigned mask = input (bb);
    rtx_insn *insn;
    FOR_BB_INSNS (bb, insn)
    {
      uint32_t word;
      if (NONDEBUG_INSN_P (insn) && !mop_template_store_p (insn)
	  && rvtt_rtl_raw_insn_fields (insn, &word)
	  && (word >> 24) == XTT_MOP_OPCODE)
	{
	      saw_query |= insn == query && word == query_word;
	  if (mask != all)
	    return false;
	}
      mask = step (insn, mask);
    }
  }
  return saw_query && !bad_channel;
}

/* The lane effect of the word that volatile store INSN might push.  */

static rvtt_rtl_lane_effect
stored_word_lane_effect (rtx_insn *insn)
{
  if (mop_template_store_p (insn))
    return LANES_KEPT;
  uint32_t word;
  bool exact;
  if (!stored_word (insn, &word, &exact))
    return LANES_ANY;
  if (exact)
    {
      rvtt_rtl_lane_effect effect = raw_word_lane_effect (word, insn);
      if ((effect == LANES_ALL || effect == LANES_ROW_MASK_CLEAR)
	  && !instruction_fifo_address_p
	       (XEXP (SET_DEST (single_set (insn)), 0), insn))
	/* Either it issues this idempotent reset, or it is just data.
	   Neither possibility invalidates existing facts, but only proved
	   delivery can establish new facts.  KEEP_ALL also prevents the
	   backwards possible-enable analysis treating this as a no-op.  */
	return LANES_KEEP_ALL;
      return effect;
    }
  return rvtt_raw_cc_opcode_inert_p (word >> 24) ? LANES_KEPT : LANES_ANY;
}

/* The constant opcode and register fields of raw instruction RAW, a
   canonical `.ttinsn' word or a TT_ instruction-buffer store, in *WORD.  */

bool
rvtt_rtl_raw_insn_fields (rtx_insn *raw, uint32_t *word)
{
  if (recog_memoized (raw) == CODE_FOR_rvtt_ttinsn_int)
    {
      rtx value = XVECEXP (PATTERN (raw), 0, 1);
      if (!CONST_INT_P (value))
	return false;
      *word = UINTVAL (value) & 0xffffffffu;
      return true;
    }
  if (asm_noperands (PATTERN (raw)) >= 0)
    return rvtt_raw_ttinsn_word (raw, word);
  rtx set = single_set (raw);
  bool exact;
  return (set && MEM_P (SET_DEST (set)) && MEM_VOLATILE_P (SET_DEST (set))
	  && stored_word (raw, word, &exact));
}

/* The lane effect of INSN.  Raw words are classified by
   rvtt_raw_cc_word_class and typed instructions by their effect
   attributes; anything unknown is LANES_ANY.  */

rvtt_rtl_lane_effect
rvtt_rtl_lane_effect_of (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return LANES_KEPT;
  if (CALL_P (insn))
    return rvtt_callee_preserves_lanes_p (get_call_fndecl (insn), false)
      ? LANES_KEPT : LANES_ANY;
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
    return LANES_KEPT;
  uint32_t word;
  if (recog_memoized (insn) == CODE_FOR_rvtt_ttinsn_int)
    return rvtt_rtl_raw_insn_fields (insn, &word)
      ? raw_word_lane_effect (word, insn) : LANES_ANY;
  if (asm_noperands (pat) >= 0 || lane_asm_template (pat))
    {
      if (rvtt_raw_ttinsn_word (insn, &word))
	return raw_word_lane_effect (word, insn);
      const char *templ = lane_asm_template (pat);
      if (templ)
	templ += strspn (templ, " \t");
	return templ && (!*templ || !strcmp (templ, "fence")
			 || !strcmp (templ, "fence rw,rw"))
	? LANES_KEPT : LANES_ANY;
    }
  int code = recog_memoized (insn);
  if (code < 0)
    return LANES_ANY;
  /* Bookkeeping markers issue no word and cannot change either fact.  */
  if (get_attr_type (insn) == TYPE_TENSIX && !get_attr_length (insn))
    return LANES_KEPT;
  if (get_attr_type (insn) != TYPE_TENSIX)
    {
      /* A volatile store may push a raw word into the instruction
	 buffer.  */
      rtx set = single_set (insn);
      if (!set || !MEM_P (SET_DEST (set)) || !MEM_VOLATILE_P (SET_DEST (set)))
	return LANES_KEPT;
      return stored_word_lane_effect (insn);
    }
  /* The descale forms deliberately retain opaque scheduling/replay
     effects to protect PRNG ordering.  SFPSTOCHRND nevertheless writes
     neither column flags nor LaneConfig, independent of rounding mode.  */
  if (code == CODE_FOR_rvtt_sfpstochrnd_descale_i_lv_int
      || code == CODE_FOR_rvtt_sfpstochrnd_descale_v_lv)
    return LANES_KEPT;
  /* Advancing the PRNG is an opaque reordering barrier, but does not
     make its independently audited lane-state effect unknown.  */
  xtt_effect_set e = rvtt_macro_insn_effects (insn);
  if (e.opaque)
    return LANES_ANY;
  if (e.config_dests_written & (1u << 15))
    {
      if (code == CODE_FOR_rvtt_sfpconfig_i)
	{
	  extract_insn (insn);
	  if (INTVAL (recog_data.operand[0]) == 0
	      && INTVAL (recog_data.operand[1]) == 15
	      && INTVAL (recog_data.operand[2]) == 1)
	    return LANES_ROW_MASK_CLEAR;
	}
      return LANES_ANY;
    }
  if (e.cc_write)
    return e.cc_write_all_lanes ? LANES_ALL : LANES_FLAGS_ANY;
  return LANES_KEPT;
}

unsigned
rvtt_rtl_lanes_step (rtx_insn *insn, unsigned state, bool direct_issue)
{
  if (CALL_P (insn) && state == RVTT_RTL_ALL_LANES
      && rvtt_callee_preserves_lanes_p (get_call_fndecl (insn), true))
    return state;
  switch (rvtt_rtl_lane_effect_of (insn))
    {
    case LANES_KEPT:
    case LANES_KEEP_ALL:
      return state;
    case LANES_ALL:
      return direct_issue ? state | RVTT_RTL_FLAGS_ALL : state;
    case LANES_ROW_MASK_CLEAR:
      return direct_issue && (state & RVTT_RTL_FLAGS_ALL)
	? state | RVTT_RTL_ROW_MASK_CLEAR : state;
    case LANES_FLAGS_ANY:
      return state & ~RVTT_RTL_FLAGS_ALL;
    default:
      return 0;
    }
}

static const rvtt_rtl_lane_facts *original_lane_snapshot;

/* A reset can establish facts only when issuance is known not to start
   or feed an unmodelled recorder.  Unknown raw words are not an ABI.  */
static bool
word_direct_issue_p (uint32_t word, rtx_insn *at)
{
  rvtt_word_facts f;
  rvtt_word_facts_classify (word, &f);
  if (f.is_mop)
    return rtl_mop_launches_proven_p (cfun, at, word);
  if (f.row != RVTT_WF_UNAUDITED)
    return !f.is_replay;
  if (TARGET_XTT_TENSIX_WH_BH)
    switch (word >> 24)
      {
      case 0x77: case 0x79: case 0x7b: case 0x81:
      case 0x87: case 0x88: case 0x8a: case 0x8b:
      case 0x96: case 0x97:
	return true;
      }
  return false;
}

rvtt_rtl_lane_snapshot_scope::rvtt_rtl_lane_snapshot_scope
  (const rvtt_rtl_lane_facts &facts)
  : m_saved (original_lane_snapshot)
{
  gcc_assert (!facts.m_original);
  original_lane_snapshot = &facts;
}

rvtt_rtl_lane_snapshot_scope::~rvtt_rtl_lane_snapshot_scope ()
{
  original_lane_snapshot = m_saved;
}

rvtt_rtl_lane_facts::rvtt_rtl_lane_facts (function *fn)
  : m_fn (fn), m_original (original_lane_snapshot
			  && original_lane_snapshot->m_fn == fn
			  ? original_lane_snapshot : nullptr)
{
  if (m_original)
    return;
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  /* Raw replay or a runtime/opaque issue may record a reset rather than
     execute it.  Without an execution proof, resets only preserve facts.
     This deliberately refuses whole functions containing such issuance.  */
  bool direct_issue = true;
  basic_block scan_bb;
  FOR_EACH_BB_FN (scan_bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (scan_bb, insn)
	if (NONDEBUG_INSN_P (insn))
	  {
	    if (CALL_P (insn))
	      direct_issue &= rvtt_callee_direct_issue_p (get_call_fndecl (insn));
	    else if (asm_noperands (PATTERN (insn)) >= 0
		     || lane_asm_template (PATTERN (insn))
		     || recog_memoized (insn) == CODE_FOR_rvtt_ttinsn_int
		     || (single_set (insn)
			 && MEM_P (SET_DEST (single_set (insn)))
			 && MEM_VOLATILE_P (SET_DEST (single_set (insn)))))
	      {
		if (mop_template_store_p (insn))
		  continue;
		uint32_t word;
		if (!rvtt_rtl_raw_insn_fields (insn, &word))
		  {
		    const char *t = lane_asm_template (PATTERN (insn));
		    if (t)
		      t += strspn (t, " \t");
		    if (!t || (*t && strcmp (t, "fence")
			       && strcmp (t, "fence rw,rw")))
		      direct_issue = false;
		  }
		else if (!word_direct_issue_p (word, insn))
		  direct_issue = false;
	      }
	    else if (recog_memoized (insn) >= 0
		     && get_attr_type (insn) == TYPE_TENSIX
		     && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	      direct_issue = false;
	  }
    }

  m_in.safe_grow (n_bbs);
  m_out.safe_grow (n_bbs);
  for (unsigned i = 0; i != n_bbs; ++i)
    m_in[i] = m_out[i] = RVTT_RTL_ALL_LANES;
  auto entry = [&] (basic_block bb)
    {
      unsigned state = EDGE_COUNT (bb->preds) ? RVTT_RTL_ALL_LANES : 0;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->preds)
	state &= (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn)
		  || (e->flags & (EDGE_ABNORMAL | EDGE_EH)))
	  ? 0 : m_out[e->src->index];
      return state;
    };
  bool changed;
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned state = m_in[bb->index] = entry (bb);
	  rtx_insn *insn;
	  FOR_BB_INSNS (bb, insn)
	    state = rvtt_rtl_lanes_step (insn, state, direct_issue);
	  if (state != m_out[bb->index])
	    {
	      m_out[bb->index] = state;
	      changed = true;
	    }
	}
    }
  while (changed);

  m_before.safe_grow_cleared (get_max_uid ());
  m_after.safe_grow_cleared (get_max_uid ());
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      unsigned state = m_in[bb->index];
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  m_before[INSN_UID (insn)] = state;
	  state = rvtt_rtl_lanes_step (insn, state, direct_issue);
	  m_after[INSN_UID (insn)] = state;
	}
    }
}
