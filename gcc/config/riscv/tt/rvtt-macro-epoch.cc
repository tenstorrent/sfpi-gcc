/* Cross-tile configuration-epoch proof for the macro planner (WP11).
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
#include "rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-epoch.h"

const char *macro_epoch_refusal_invalidated = "prefix-epoch-invalidated";
const char *macro_epoch_refusal_unproven = "prefix-epoch-unproven";
const char *macro_epoch_refusal_preheader = "prefix-hoist-preheader-unproven";

namespace {

/* ------------------------------------------------------------------ */
/* 32-bit value intervals for stored/issued words.		      */
/*								      */
/* Three-state lattice: KNOWN carries a closed interval [lo, hi] of   */
/* 32-bit values; UNKNOWN poisons; IGNORE is the join identity,       */
/* contributed only by in-progress cycles that provably do not define */
/* the resolved register (the value is invariant around them).	      */
/* ------------------------------------------------------------------ */

struct epoch_ival
{
  enum { KNOWN, UNKNOWN, IGNORE } kind;
  uint64_t lo, hi;		/* 32-bit values, lo <= hi (KNOWN)     */
  bool known () const { return kind == KNOWN; }
};

static const epoch_ival ival_unknown = { epoch_ival::UNKNOWN, 0, 0 };
static const epoch_ival ival_ignore = { epoch_ival::IGNORE, 0, 0 };

static epoch_ival
ival_const (uint64_t v)
{
  epoch_ival r = { epoch_ival::KNOWN, v & 0xffffffff, v & 0xffffffff };
  return r;
}

static epoch_ival
ival_add (const epoch_ival &a, const epoch_ival &b)
{
  if (!a.known () || !b.known ())
    return ival_unknown;
  uint64_t lo = a.lo + b.lo, hi = a.hi + b.hi;
  if (hi > 0xffffffff)		/* 32-bit wrap: no interval claim      */
    return ival_unknown;
  epoch_ival r = { epoch_ival::KNOWN, lo, hi };
  return r;
}

/* Path join: interval union; UNKNOWN poisons; IGNORE is identity.  */

static epoch_ival
ival_union (const epoch_ival &a, const epoch_ival &b)
{
  if (a.kind == epoch_ival::IGNORE)
    return b;
  if (b.kind == epoch_ival::IGNORE)
    return a;
  if (!a.known () || !b.known ())
    return ival_unknown;
  epoch_ival r = { epoch_ival::KNOWN, MIN (a.lo, b.lo), MAX (a.hi, b.hi) };
  return r;
}

/* Constant field extraction over a monotone interval: the bits above
   FIELD_SHIFT of every value in [lo, hi] are the same exactly when the
   interval endpoints agree there.  */

static bool
ival_field_constant (const epoch_ival &v, unsigned shift, uint64_t *field)
{
  if (!v.known () || (v.lo >> shift) != (v.hi >> shift))
    return false;
  *field = v.lo >> shift;
  return true;
}

/* ------------------------------------------------------------------ */
/* Backward value resolution (post-RA hard registers).		      */
/*								      */
/* A demand-driven reaching-value walk: block-local backward scan,    */
/* then a memoized join over predecessors.  Registers stepped by a    */
/* positive constant inside a structural self-loop resolve through    */
/* the monotone induction range [init, equality-exit bound]; every    */
/* other self-referential definition refuses (UNKNOWN).  Values are   */
/* deterministic per (block, register), so memoization is sound; a    */
/* revisit while a block's query is in progress can only be a cycle   */
/* whose blocks do not define the register (a definition would have   */
/* ended the scan), so the value is invariant around it and the	      */
/* contribution is the join identity.				      */
/* ------------------------------------------------------------------ */

#define EPOCH_EVAL_DEPTH_MAX 32

typedef int_hash<uint64_t, UINT64_MAX, UINT64_MAX - 1> epoch_key_hash;

struct epoch_resolver
{
  function *fn;
  /* (bb, regno) -> end value  */
  hash_map<uint64_t, epoch_ival,
	   simple_hashmap_traits<epoch_key_hash, epoch_ival>> memo;
  hash_set<uint64_t, false, epoch_key_hash> in_progress;

  static uint64_t key (basic_block bb, rtx reg)
  {
    return ((uint64_t) bb->index << 10) | REGNO (reg);
  }
};

static epoch_ival eval_rtx (epoch_resolver *ctx, rtx x, rtx_insn *pos,
			    int depth);
static epoch_ival resolve_reg_before (epoch_resolver *ctx, rtx reg,
				      rtx_insn *from, bool inclusive,
				      int depth, bool bb_local);
static epoch_ival resolve_reg_at_end (epoch_resolver *ctx, basic_block bb,
				      rtx reg, int depth);

/* Monotone self-loop induction: REG's in-block definition is REG +=
   STEP (STEP > 0) in the self-looping block BB whose exit is an
   equality-class comparison of REG against a resolvable loop-invariant
   bound.  Every value REG takes inside BB then lies in [init, bound].  */

static epoch_ival
induction_range (epoch_resolver *ctx, basic_block bb, rtx reg,
		 uint64_t step, int depth)
{
  if (step == 0 || step > 0xffff || depth > EPOCH_EVAL_DEPTH_MAX)
    return ival_unknown;

  /* Structural self-loop with a unique external predecessor.  */
  edge e, external = nullptr;
  edge_iterator ei;
  unsigned self_edges = 0;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == bb)
      ++self_edges;
    else
      {
	if (external)
	  return ival_unknown;
	external = e;
      }
  if (self_edges != 1 || !external
      || external->src == ENTRY_BLOCK_PTR_FOR_FN (ctx->fn))
    return ival_unknown;

  /* Exactly one definition of REG inside BB (the step).  */
  unsigned defs = 0;
  for (rtx_insn *i = BB_HEAD (bb); i; i = NEXT_INSN (i))
    {
      if (NONDEBUG_INSN_P (i) && reg_set_p (reg, i))
	++defs;
      if (i == BB_END (bb))
	break;
    }
  if (defs != 1)
    return ival_unknown;

  /* The equality-exit bound, loop-invariant in BB.  */
  rtx_insn *jump = BB_END (bb);
  if (!jump || !JUMP_P (jump))
    return ival_unknown;
  rtx pat = single_set (jump);
  if (!pat || GET_CODE (SET_SRC (pat)) != IF_THEN_ELSE)
    return ival_unknown;
  rtx cond = XEXP (SET_SRC (pat), 0);
  if (GET_CODE (cond) != NE && GET_CODE (cond) != EQ)
    return ival_unknown;
  rtx a = XEXP (cond, 0), b = XEXP (cond, 1);
  rtx bound_rtx;
  if (REG_P (a) && REGNO (a) == REGNO (reg))
    bound_rtx = b;
  else if (REG_P (b) && REGNO (b) == REGNO (reg))
    bound_rtx = a;
  else
    return ival_unknown;
  if (REG_P (bound_rtx))
    for (rtx_insn *i = BB_HEAD (bb); i; i = NEXT_INSN (i))
      {
	if (NONDEBUG_INSN_P (i) && reg_set_p (bound_rtx, i))
	  return ival_unknown;
	if (i == BB_END (bb))
	  break;
      }

  epoch_ival init
    = resolve_reg_at_end (ctx, external->src, reg, depth + 1);
  epoch_ival bound = eval_rtx (ctx, bound_rtx, jump, depth + 1);
  if (!init.known () || !bound.known () || init.hi > bound.lo)
    return ival_unknown;
  epoch_ival r = { epoch_ival::KNOWN, init.lo, bound.hi };
  return r;
}

/* Evaluate the value REG holds immediately after DEF (a definition of
   REG).  A self-referential step (REG += K) resolves through the
   self-loop induction, or -- outside any self-loop -- through the
   BLOCK-LOCAL chain only (the lui/addi compose); a chain crossing
   blocks could re-enter the definition through a loop and is
   refused.  */

static epoch_ival
eval_def (epoch_resolver *ctx, rtx reg, rtx_insn *def, int depth)
{
  if (depth > EPOCH_EVAL_DEPTH_MAX)
    return ival_unknown;
  rtx set = single_set (def);
  if (!set || !REG_P (SET_DEST (set))
      || REGNO (SET_DEST (set)) != REGNO (reg))
    return ival_unknown;
  rtx src = SET_SRC (set);
  if (GET_CODE (src) == PLUS && REG_P (XEXP (src, 0))
      && REGNO (XEXP (src, 0)) == REGNO (reg)
      && CONST_INT_P (XEXP (src, 1))
      && INTVAL (XEXP (src, 1)) > 0)
    {
      uint64_t step = (uint64_t) INTVAL (XEXP (src, 1));
      basic_block bb = BLOCK_FOR_INSN (def);
      bool self_loop = false;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	self_loop |= e->dest == bb;
      if (self_loop)
	return induction_range (ctx, bb, reg, step, depth);
      return ival_add (resolve_reg_before (ctx, reg, def, false,
					   depth + 1, /*bb_local=*/true),
		       ival_const (step));
    }
  return eval_rtx (ctx, src, def, depth + 1);
}

/* Value of REG at the end of BB (memoized; in-progress cycles that do
   not define REG contribute the join identity).  */

static epoch_ival
resolve_reg_at_end (epoch_resolver *ctx, basic_block bb, rtx reg, int depth)
{
  if (depth > EPOCH_EVAL_DEPTH_MAX
      || bb == ENTRY_BLOCK_PTR_FOR_FN (ctx->fn))
    return ival_unknown;
  uint64_t key = epoch_resolver::key (bb, reg);
  if (epoch_ival *cached = ctx->memo.get (key))
    return *cached;
  if (ctx->in_progress.contains (key))
    return ival_ignore;
  ctx->in_progress.add (key);
  epoch_ival r = resolve_reg_before (ctx, reg, BB_END (bb),
				     /*inclusive=*/true, depth,
				     /*bb_local=*/false);
  ctx->in_progress.remove (key);
  /* Do not memoize IGNORE: it is only valid inside the cycle that
     produced it.  */
  if (r.kind != epoch_ival::IGNORE)
    ctx->memo.put (key, r);
  return r;
}

/* Value of REG immediately before FROM (or at FROM when INCLUSIVE):
   block-local backward scan to the nearest definition, then the join
   over predecessors (unless BB_LOCAL restricts the walk).  */

static epoch_ival
resolve_reg_before (epoch_resolver *ctx, rtx reg, rtx_insn *from,
		    bool inclusive, int depth, bool bb_local)
{
  if (depth > EPOCH_EVAL_DEPTH_MAX || !REG_P (reg))
    return ival_unknown;
  basic_block bb = BLOCK_FOR_INSN (from);
  rtx_insn *i = inclusive ? from : PREV_INSN (from);
  for (; i && BLOCK_FOR_INSN (i) == bb; i = PREV_INSN (i))
    {
      if (!NONDEBUG_INSN_P (i))
	continue;
      if (reg_set_p (reg, i))
	return eval_def (ctx, reg, i, depth);
    }
  if (bb_local)
    return ival_unknown;

  epoch_ival r = ival_ignore;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      epoch_ival p = resolve_reg_at_end (ctx, e->src, reg, depth + 1);
      r = ival_union (r, p);
      if (r.kind == epoch_ival::UNKNOWN)
	return r;
    }
  return r;
}

static epoch_ival
eval_rtx (epoch_resolver *ctx, rtx x, rtx_insn *pos, int depth)
{
  if (depth > EPOCH_EVAL_DEPTH_MAX)
    return ival_unknown;
  switch (GET_CODE (x))
    {
    case CONST_INT:
      return ival_const ((uint64_t) UINTVAL (x));
    case REG:
      {
	epoch_ival r = resolve_reg_before (ctx, x, pos, false, depth,
					   /*bb_local=*/false);
	/* An all-cycles resolution at expression level has no acyclic
	   reaching definition: refuse.  */
	return r.kind == epoch_ival::IGNORE ? ival_unknown : r;
      }
    case PLUS:
      return ival_add (eval_rtx (ctx, XEXP (x, 0), pos, depth + 1),
		       eval_rtx (ctx, XEXP (x, 1), pos, depth + 1));
    case ASHIFT:
      if (CONST_INT_P (XEXP (x, 1)) && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) < 32)
	{
	  epoch_ival a = eval_rtx (ctx, XEXP (x, 0), pos, depth + 1);
	  unsigned k = INTVAL (XEXP (x, 1));
	  if (a.known () && (a.hi << k) <= 0xffffffff)
	    {
	      epoch_ival r = { epoch_ival::KNOWN, a.lo << k, a.hi << k };
	      return r;
	    }
	}
      return ival_unknown;
    case AND:
      if (CONST_INT_P (XEXP (x, 1)) && INTVAL (XEXP (x, 1)) > 0)
	{
	  epoch_ival r = { epoch_ival::KNOWN, 0,
			   (uint64_t) UINTVAL (XEXP (x, 1)) & 0xffffffff };
	  return r;
	}
      return ival_unknown;
    case IOR:
      {
	epoch_ival a = eval_rtx (ctx, XEXP (x, 0), pos, depth + 1);
	epoch_ival b = eval_rtx (ctx, XEXP (x, 1), pos, depth + 1);
	if (a.known () && b.known () && a.lo == a.hi && b.lo == b.hi)
	  return ival_const (a.lo | b.lo);
	/* Disjoint-bit OR of a constant with an interval below its
	   lowest set bit is an addition (the canonicalized form of the
	   dynamic-field compose).  */
	if (a.known () && b.known () && a.lo == a.hi && a.lo != 0
	    && b.hi < (a.lo & -a.lo))
	  return ival_add (a, b);
	if (a.known () && b.known () && b.lo == b.hi && b.lo != 0
	    && a.hi < (b.lo & -b.lo))
	  return ival_add (a, b);
	return ival_unknown;
      }
    case NE: case EQ: case LT: case LTU: case GT: case GTU:
    case LE: case LEU: case GE: case GEU:
      {
	epoch_ival r = { epoch_ival::KNOWN, 0, 1 };
	return r;
      }
    default:
      return ival_unknown;
    }
}

/* ------------------------------------------------------------------ */
/* The owner predicate on a (possibly interval-valued) 32-bit word.   */
/* ------------------------------------------------------------------ */

/* A word owns the hoisted state exactly when it is an SFPCONFIG whose
   destination is planner-owned; opcode and field layout come from the
   capability tables (op << 24 | imm16 << 8 | dest << 4 | mod1).  */

static const char *
epoch_word_check (const epoch_ival &v, const rvtt_macro::caps *c)
{
  uint64_t opcode;
  if (!ival_field_constant (v, 24, &opcode))
    return macro_epoch_refusal_unproven;
  if (opcode != c->sfpconfig_opcode)
    return nullptr;
  if (v.lo != v.hi)
    return macro_epoch_refusal_unproven;
  unsigned dest = (v.lo >> 4) & 0xf;
  return ((c->owned_config_dests >> dest) & 1)
    ? macro_epoch_refusal_invalidated : nullptr;
}

/* ------------------------------------------------------------------ */
/* Audited scalar assembly templates (the RTL mirror of the gimple    */
/* raw-word scan's list): base-ISA instructions with no Tensix	      */
/* encoding space and no configuration effect.			      */
/* ------------------------------------------------------------------ */

/* Template equality up to trailing whitespace (some LLK templates end
   in a newline+tab).  */

static bool
asm_template_eq (const char *s, const char *ref)
{
  size_t n = strlen (ref);
  if (strncmp (s, ref, n) != 0)
    return false;
  for (s += n; *s; ++s)
    if (*s != ' ' && *s != '\t' && *s != '\n')
      return false;
  return true;
}

static bool
audited_scalar_asm_p (const char *s)
{
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return true;		/* pure barrier, no instruction	       */
  return asm_template_eq (s, "fence") || asm_template_eq (s, "ebreak")
    || asm_template_eq (s, "la sp, %0")
    /* The scalar store-load-consume roundtrip idiom (pcbuf/mailbox
       reads), in both LLK spellings: base-ISA memory operations
       only.  */
    || asm_template_eq (s, "sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0")
    || asm_template_eq (s, "sw %0, (%1)\n\tlw %0, (%1)\n\tandi %0, %0, 0")
    /* The crt0 global-pointer initialization.  */
    || asm_template_eq (s, ".option push\n.option norelax\n"
			   "la gp, __global_pointer$\n.option pop");
}

/* Classify one assembly insn.  Returns null when provably inert.  */

static const char *
epoch_asm_check (epoch_resolver *ctx, rtx_insn *insn,
		 const rvtt_macro::caps *c)
{
  rtx pat = PATTERN (insn);
  rtx asmop = extract_asm_operands (pat);
  const char *tmpl = nullptr;
  if (asmop)
    tmpl = ASM_OPERANDS_TEMPLATE (asmop);
  else if (GET_CODE (pat) == ASM_INPUT)
    tmpl = XSTR (pat, 0);
  else if (GET_CODE (pat) == PARALLEL
	   && GET_CODE (XVECEXP (pat, 0, 0)) == ASM_INPUT)
    /* An operand-less asm with clobbers (the ebreak idiom).  */
    tmpl = XSTR (XVECEXP (pat, 0, 0), 0);
  if (!tmpl)
    return macro_epoch_refusal_unproven;

  const char *s = tmpl;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strncmp (s, ".ttinsn", 7) == 0)
    {
      s += 7;
      while (*s == ' ' || *s == '\t')
	++s;
      if (strcmp (s, "%0") != 0 || !asmop
	  || ASM_OPERANDS_INPUT_LENGTH (asmop) != 1)
	return macro_epoch_refusal_unproven;
      rtx word = ASM_OPERANDS_INPUT (asmop, 0);
      return epoch_word_check (eval_rtx (ctx, word, insn, 0), c);
    }
  return audited_scalar_asm_p (tmpl) ? nullptr
    : macro_epoch_refusal_unproven;
}

/* Classify volatile memory references: every volatile store is treated
   as a potential RISC instruction push and its stored word must pass
   the owner predicate; volatile loads deliver no words.  */

static const char *
epoch_volatile_check (epoch_resolver *ctx, rtx_insn *insn, rtx pat,
		      const rvtt_macro::caps *c)
{
  if (GET_CODE (pat) == PARALLEL)
    {
      for (int i = 0; i != XVECLEN (pat, 0); ++i)
	if (const char *why
	      = epoch_volatile_check (ctx, insn, XVECEXP (pat, 0, i), c))
	  return why;
      return nullptr;
    }
  if (GET_CODE (pat) != SET)
    /* Recognized non-Tensix volatile forms without a store (memory
       barriers) deliver no words.  */
    return nullptr;
  rtx dest = SET_DEST (pat);
  if (MEM_P (dest) && MEM_VOLATILE_P (dest))
    return epoch_word_check (eval_rtx (ctx, SET_SRC (pat), insn, 0), c);
  return nullptr;
}

/* Classify one instruction of the enclosing loop.  Returns null when
   provably inert for the hoisted state.  */

static const char *
epoch_insn_check (epoch_resolver *ctx, rtx_insn *insn,
		  hash_set<rtx_insn *> &members,
		  const rvtt_macro::caps *c)
{
  if (!NONDEBUG_INSN_P (insn))
    return nullptr;
  if (members.contains (insn))
    return nullptr;
  if (CALL_P (insn))
    return macro_epoch_refusal_unproven;
  if (GET_CODE (PATTERN (insn)) == USE || GET_CODE (PATTERN (insn)) == CLOBBER)
    return nullptr;
  if (asm_noperands (PATTERN (insn)) >= 0)
    return epoch_asm_check (ctx, insn, c);
  if (recog_memoized (insn) >= 0)
    {
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (!e.opaque)
	{
	  if ((e.config_dests_written | e.config_dests_read)
	      & c->owned_config_dests)
	    return macro_epoch_refusal_invalidated;
	  /* Foreign SFPU dataflow inside the epoch (another region's
	     rows or emitted calendar): conservative refusal.  */
	  if (e.lreg_write || e.lreg_read || e.cc_write)
	    return macro_epoch_refusal_invalidated;
	  return nullptr;
	}
      if (get_attr_type (insn) == TYPE_TENSIX)
	return macro_epoch_refusal_unproven;
      if (volatile_refs_p (PATTERN (insn)))
	return epoch_volatile_check (ctx, insn, PATTERN (insn), c);
      return nullptr;
    }
  return macro_epoch_refusal_unproven;
}

/* ------------------------------------------------------------------ */
/* Enclosing natural loop and its structural entry.		      */
/* ------------------------------------------------------------------ */

/* Find the smallest natural loop containing BLOCK, as the union of
   natural loops of the backedges sharing the chosen header.  Fills
   BODY with the loop's block indices; null when BLOCK sits in no
   loop.  Dominance-based and read-only.  */

static basic_block
enclosing_loop (function *fn, basic_block block, bitmap body)
{
  bool free_dom = !dom_info_available_p (CDI_DOMINATORS);
  calculate_dominance_info (CDI_DOMINATORS);

  basic_block best_header = nullptr;
  unsigned best_size = 0;
  basic_block header;
  FOR_EACH_BB_FN (header, fn)
    {
      /* Latches: predecessors dominated by HEADER.  */
      auto_vec<basic_block, 4> latches;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, header->preds)
	if (e->src != ENTRY_BLOCK_PTR_FOR_FN (fn)
	    && dominated_by_p (CDI_DOMINATORS, e->src, header))
	  latches.safe_push (e->src);
      if (latches.is_empty ())
	continue;
      /* Natural loop: blocks reaching a latch without passing the
	 header.  */
      auto_bitmap loop;
      bitmap_set_bit (loop, header->index);
      auto_vec<basic_block, 16> work;
      for (basic_block latch : latches)
	if (bitmap_set_bit (loop, latch->index))
	  work.safe_push (latch);
      while (!work.is_empty ())
	{
	  basic_block b = work.pop ();
	  FOR_EACH_EDGE (e, ei, b->preds)
	    if (e->src != ENTRY_BLOCK_PTR_FOR_FN (fn)
		&& bitmap_set_bit (loop, e->src->index))
	      work.safe_push (e->src);
	}
      if (!bitmap_bit_p (loop, block->index))
	continue;
      unsigned size = bitmap_count_bits (loop);
      if (!best_header || size < best_size)
	{
	  best_header = header;
	  best_size = size;
	  bitmap_copy (body, loop);
	}
    }

  if (free_dom)
    free_dominance_info (CDI_DOMINATORS);
  return best_header;
}

/* The enclosing loop's unique external entry.  When the entry edge's
   source is a dedicated single-successor block (not the entry block),
   that block is the structural preheader (insertion at its tail).
   Otherwise the unique entry EDGE itself is returned for a commit-time
   split: the split block executes exactly when the loop is entered,
   which discharges the zero-trip obligation by construction.  Null
   both when the loop has no unique external entry.  */

static void
outer_structural_entry (function *fn, basic_block header, bitmap body,
			basic_block *preheader, edge *entry)
{
  *preheader = nullptr;
  *entry = nullptr;
  edge e, incoming = nullptr;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, header->preds)
    {
      if (bitmap_bit_p (body, e->src->index))
	continue;		/* latch side */
      if (incoming)
	return;			/* multiple external entries */
      incoming = e;
    }
  if (!incoming)
    return;
  if (incoming->src != ENTRY_BLOCK_PTR_FOR_FN (fn)
      && EDGE_COUNT (incoming->src->succs) == 1)
    *preheader = incoming->src;
  else
    *entry = incoming;
}

} // anonymous namespace

/* See rvtt-macro-epoch.h.  */

bool
rvtt_macro_prefix_epoch_hoist (function *fn, const macro_region &region,
			       basic_block config_preheader,
			       const rvtt_macro::caps *c,
			       basic_block *hoist_preheader,
			       edge *hoist_edge,
			       const char **refusal,
			       rtx_insn **refusal_insn)
{
  *hoist_preheader = nullptr;
  *hoist_edge = nullptr;
  *refusal = nullptr;
  *refusal_insn = nullptr;

  auto_bitmap body;
  basic_block header = enclosing_loop (fn, config_preheader, body);
  if (!header)
    return false;		/* no enclosing loop: nothing to elide */

  basic_block preheader;
  edge entry;
  outer_structural_entry (fn, header, body, &preheader, &entry);
  if (!preheader && !entry)
    {
      *refusal = macro_epoch_refusal_preheader;
      return false;
    }

  hash_set<rtx_insn *> members;
  for (const macro_row &row : region.rows)
    {
      if (row.enable)
	members.add (row.enable);
      if (row.separator)
	members.add (row.separator);
      for (rtx_insn *member : row.insns)
	members.add (member);
    }
  for (rtx_insn *sep : region.run_separators)
    members.add (sep);

  epoch_resolver ctx;
  ctx.fn = fn;

  bitmap_iterator bi;
  unsigned index;
  EXECUTE_IF_SET_IN_BITMAP (body, 0, index, bi)
    {
      basic_block bb = BASIC_BLOCK_FOR_FN (fn, index);
      if (!bb)
	{
	  *refusal = macro_epoch_refusal_unproven;
	  return false;
	}
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	if (const char *why = epoch_insn_check (&ctx, insn, members, c))
	  {
	    *refusal = why;
	    *refusal_insn = insn;
	    return false;
	  }
    }

  *hoist_preheader = preheader;
  *hoist_edge = entry;
  return true;
}
