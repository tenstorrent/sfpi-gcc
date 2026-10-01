/* MOP-driven loop delivery: the caller-side outward-ownership proof.

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

/* Split out of rtl-rvtt-mop-form.cc, which had grown past 2200 lines.
   That file forms MOP loops in RTL; this one answers the separate
   question its commit depends on -- whether every transitive caller
   leaves the MOP template live -- by a must-dataflow over GIMPLE
   bodies, including the rvtt-ipa-summary cover-face digest.  Two
   IRs, two disciplines, one narrow interface: the single name that
   crosses is rvtt_mop_outward_owned_p.

   The proof obligation this discharges, and the axioms it rests on
   (crt0-benign, kernel-single-TU, tt-op-field-discipline), are
   stated in the rtl-rvtt-mop-form.cc file header and are cited from
   there by rvtt-mop-derive.cc, rvtt-mop-tables.h, rvtt-ipa-summary.h,
   gimple-rvtt-crosscall-census.cc and rvtt-cost.md.  They stay there
   so those citations keep resolving.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#define INCLUDE_MAP
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "print-rtl.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "expr.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "cgraph.h"
#include "attribs.h"
#include "langhooks.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-mop-tables.h"
#include "rvtt-ipa-summary.h"

namespace {

/* ---- Outward ownership: caller-side MOP-template liveness ----

   See the file header for the proof obligation and the axioms.  The
   analysis runs over the GIMPLE bodies of the transitive caller
   closure of the forming function (callers expand after their callees,
   so those bodies are still gimple when this RTL pass runs; when they
   are not, the proof fails closed).

   Cover-state lattice: a 10-bit must-set -- bits 0..8 are the MOP
   config words at TENSIX_MOP_CFG_BASE + 4*i, bit 9 the MOP_CFG zmask
   high half -- of template state rewritten by the caller since the
   last call into the forming function.  Two states are tracked in
   parallel (entry assumed empty / entry assumed full) so a function's
   effect summarizes as a per-bit gen/pass-through transfer plus the
   entry bits its exposed launches require.  */

constexpr unsigned MOP_STATE_ZMASK = 1u << 9;
constexpr unsigned MOP_STATE_FULL = (1u << 10) - 1;

/* Template state the formed class writes: flags (word 1), A0 (word 3),
   the flags&2 step slots (words 4..6; written whenever any candidate
   carries steps -- the proof conservatively assumes the maximal
   class), and the MOP_CFG zmask high half (the emitted TTMOPCFG 0).  */
constexpr unsigned MOP_CLOBBER_SET
  = (1u << XTT_MOP_CFG_FLAGS_INDEX) | (1u << XTT_MOP_CFG_A0_INDEX)
    | (1u << 4) | (1u << 5) | (1u << 6) | MOP_STATE_ZMASK;

/* What a caller's launch consumes of the clobbered set.  A type-1 MOP
   reads the nine config words and never the zmask; a type-0 (or
   unclassifiable) launch additionally consumes the zmask high half
   (rvtt-mop-tables.h, expander facts).  */
constexpr unsigned MOP_REQ_TYPE1 = MOP_CLOBBER_SET & ~MOP_STATE_ZMASK;
constexpr unsigned MOP_REQ_ANY = MOP_CLOBBER_SET;

struct mop_caller_summary
{
  bool computed = false;
  bool in_progress = false;
  bool valid = false;
  const char *invalid_why = nullptr;
  /* Hazard regardless of entry state (an internal clobber reaches a
     launch without full re-arm).  */
  bool hazard = false;
  /* First hazard's classified event and the function carrying it.  */
  const char *hazard_what = nullptr;
  tree hazard_fn = NULL_TREE;
  /* First entry-exposed launch's classification (for the dump when a
     caller turns the exposure into a hazard).  */
  const char *exposed_what = nullptr;
  tree exposed_fn = NULL_TREE;
  /* Entry bits some reachable launch requires beyond internal cover.  */
  unsigned exposed_need = 0;
  /* Exit cover-state for entry == empty / entry == full (meet over
     exit paths; per-bit transfer out(in) = out_empty | (in & (out_full
     & ~out_empty))).  */
  unsigned out_empty = MOP_STATE_FULL;
  unsigned out_full = MOP_STATE_FULL;
};

struct mop_outward_ctx
{
  tree formee = NULL_TREE;
  cgraph_node *formee_node = nullptr;
  /* Node-stable storage: summaries are referenced across recursive
     insertions.  */
  std::map<tree, mop_caller_summary> summaries;
};

static mop_caller_summary &mop_analyze_fn (mop_outward_ctx &ctx, tree decl);

/* Fold PTR (a pointer value) to a constant byte address, following a
   short SSA chain of casts and constant pointer arithmetic.  */

static bool
mop_pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
			      unsigned depth = 0)
{
  if (!ptr || depth > 8)
    return false;
  if (TREE_CODE (ptr) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (ptr))
	return false;
      *addr = tree_to_uhwi (ptr) & 0xffffffff;
      return true;
    }
  if (TREE_CODE (ptr) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (ptr);
  if (!def || !is_gimple_assign (def))
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  if (CONVERT_EXPR_CODE_P (code) || code == INTEGER_CST
      || code == SSA_NAME)
    return mop_pointer_constant_address (gimple_assign_rhs1 (def), addr,
					 depth + 1);
  if (code == POINTER_PLUS_EXPR || code == PLUS_EXPR)
    {
      tree off = gimple_assign_rhs2 (def);
      unsigned HOST_WIDE_INT base;
      if (TREE_CODE (off) != INTEGER_CST || !tree_fits_shwi_p (off)
	  || !mop_pointer_constant_address (gimple_assign_rhs1 (def),
					    &base, depth + 1))
	return false;
      *addr = (base + (unsigned HOST_WIDE_INT) tree_to_shwi (off))
	      & 0xffffffff;
      return true;
    }
  return false;
}

/* Fold REF (a store lhs) to a constant byte address if possible.  */

static bool
mop_ref_constant_address (tree ref, unsigned HOST_WIDE_INT *addr)
{
  poly_int64 bitsize, bitpos;
  tree offset;
  machine_mode mode;
  int unsignedp, reversep, volatilep = 0;
  tree base = get_inner_reference (ref, &bitsize, &bitpos, &offset, &mode,
				   &unsignedp, &reversep, &volatilep);
  if (offset || !base || TREE_CODE (base) != MEM_REF)
    return false;
  tree moff = TREE_OPERAND (base, 1);
  if (TREE_CODE (moff) != INTEGER_CST || !tree_fits_shwi_p (moff))
    return false;
  HOST_WIDE_INT pos;
  if (!bitpos.is_constant (&pos) || (pos % BITS_PER_UNIT) != 0)
    return false;
  unsigned HOST_WIDE_INT a;
  if (!mop_pointer_constant_address (TREE_OPERAND (base, 0), &a))
    return false;
  a += (unsigned HOST_WIDE_INT) tree_to_shwi (moff);
  a += (unsigned HOST_WIDE_INT) (pos / BITS_PER_UNIT);
  *addr = a & 0xffffffff;
  return true;
}

/* Classify the 32-bit word VAL (a value stored toward a possible
   instruction-FIFO push) by the constant opcode base of its PLUS /
   BIT_IOR composition (AXIOM tt-op-field-discipline, file header).
   Returns the frontend opcode byte, or -1 when no constant base
   pins it.  *TYPE1 is set when bit 23 of the constant base is set.  */

static int
mop_pushed_word_base (tree val, bool *type1, unsigned depth = 0)
{
  if (depth > 12 || !val)
    return -1;
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	return -1;
      unsigned HOST_WIDE_INT w
	= TREE_INT_CST_LOW (val) & 0xffffffff;
      *type1 = (w >> 23) & 1;
      return (int) (w >> 24);
    }
  if (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (!def || !is_gimple_assign (def))
	return -1;
      tree_code code = gimple_assign_rhs_code (def);
      if (code == PLUS_EXPR || code == BIT_IOR_EXPR)
	{
	  bool t1a = false, t1b = false;
	  int a = mop_pushed_word_base (gimple_assign_rhs1 (def), &t1a,
					depth + 1);
	  int b = mop_pushed_word_base (gimple_assign_rhs2 (def), &t1b,
					depth + 1);
	  /* Exactly one side carries the opcode base; two competing
	     bases (or none) leave the word unclassified.  */
	  if (a > 0 && b <= 0)
	    {
	      *type1 = t1a;
	      return a;
	    }
	  if (b > 0 && a <= 0)
	    {
	      *type1 = t1b;
	      return b;
	    }
	  if (a == 0 && b == 0)
	    {
	      *type1 = t1a | t1b;
	      return 0;
	    }
	  return -1;
	}
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	  || code == NOP_EXPR)
	return mop_pushed_word_base (gimple_assign_rhs1 (def), type1,
				     depth + 1);
      /* Shifted single fields below the opcode byte cannot construct
	 an opcode by themselves under the discipline axiom.  */
      if (code == LSHIFT_EXPR || code == BIT_AND_EXPR
	  || code == RSHIFT_EXPR)
	return 0;
      return -1;
    }
  return -1;
}

/* One classified caller event.  */

struct mop_event
{
  enum { BENIGN, LAUNCH, COVER, CLOBBER, COMPOSE } kind = BENIGN;
  unsigned bits = 0;	 /* LAUNCH: required set; COVER: covered set */
  tree callee = NULL_TREE; /* COMPOSE */
  const char *what = nullptr; /* LAUNCH: classification for the dump */
};

/* Classify the word VAL delivered (or potentially delivered) by an
   asm; fills EV.  TTINSN_DIRECT marks a word directly issued by a
   `.ttinsn' directive (creditable as a MOP_CFG zmask rewrite).  */

static void
mop_classify_delivered_word (tree word, bool ttinsn_direct, mop_event &ev)
{
  bool type1 = false;
  int opc = mop_pushed_word_base (word, &type1);
  if (opc == (int) XTT_MOP_OPCODE)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = type1 ? MOP_REQ_TYPE1 : MOP_REQ_ANY;
      ev.what = "a raw MOP word in assembly";
    }
  else if (opc == (int) XTT_MOP_CFG_OPCODE && ttinsn_direct)
    {
      /* A directly delivered MOP_CFG rewrites the zmask high half.
	 (A computed 0x03-based word behind a store idiom is never
	 credited: its destination is not provably the FIFO.)  */
      ev.kind = mop_event::COVER;
      ev.bits = MOP_STATE_ZMASK;
    }
  else if (opc < 0)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "an unclassifiable delivered word in assembly";
    }
}

/* Classify a gimple store.  */

static mop_event
mop_classify_store (gimple *stmt)
{
  mop_event ev;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return ev;

  unsigned HOST_WIDE_INT addr;
  if (mop_ref_constant_address (lhs, &addr))
    {
      unsigned HOST_WIDE_INT base = XTT_MOP_CFG_MMIO_BASE & 0xffffffff;
      if (addr >= base && addr < base + 4 * 9 && (addr - base) % 4 == 0)
	{
	  ev.kind = mop_event::COVER;
	  ev.bits = 1u << ((addr - base) / 4);
	  return ev;
	}
      /* Any other constant MMIO address: it can only deliver an
	 instruction if it is an instruction-FIFO alias, so classify
	 the stored word.  */
    }
  else
    {
      tree base = get_base_address (lhs);
      /* A store into a known non-volatile object is memory, not MMIO
	 (hardware registers are declared volatile).  */
      if (!TREE_THIS_VOLATILE (lhs)
	  && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
	return ev;
    }

  tree val = gimple_assign_rhs1 (stmt);
  bool type1 = false;
  int opc = mop_pushed_word_base (val, &type1);
  if (opc == (int) XTT_MOP_OPCODE)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = type1 ? MOP_REQ_TYPE1 : MOP_REQ_ANY;
      ev.what = "a computed MOP push";
    }
  else if (opc < 0)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "an unclassifiable volatile store";
    }
  /* opc == XTT_MOP_CFG_OPCODE: a zmask WRITE at worst -- never a
     template consumer, and not creditable as cover (the destination
     is not provably the FIFO).  Benign.  */
  return ev;
}

/* Apply EV to the parallel states (SE = cover assuming empty entry,
   SF = assuming full entry).  When RECORD is non-null, accumulate
   hazards and entry requirements into it (FNDECL names the function
   being analyzed for the hazard detail).  Returns false when the
   analysis becomes invalid (unanalyzable callee).  */

static bool
mop_apply_event (mop_outward_ctx &ctx, const mop_event &ev,
		 unsigned *se, unsigned *sf,
		 mop_caller_summary *record, tree fndecl)
{
  switch (ev.kind)
    {
    case mop_event::BENIGN:
      return true;
    case mop_event::LAUNCH:
      if (record)
	{
	  if ((ev.bits & ~*sf) && !record->hazard)
	    {
	      record->hazard = true;
	      record->hazard_what = ev.what;
	      record->hazard_fn = fndecl;
	    }
	  if ((ev.bits & *sf & ~*se) && !record->exposed_what)
	    {
	      record->exposed_what = ev.what;
	      record->exposed_fn = fndecl;
	    }
	  record->exposed_need |= ev.bits & *sf & ~*se;
	}
      return true;
    case mop_event::COVER:
      *se |= ev.bits;
      *sf |= ev.bits;
      return true;
    case mop_event::CLOBBER:
      *se = 0;
      *sf = 0;
      return true;
    case mop_event::COMPOSE:
      {
	mop_caller_summary &sub = mop_analyze_fn (ctx, ev.callee);
	if (!sub.valid)
	  return false;
	if (record)
	  {
	    if (sub.hazard && !record->hazard)
	      {
		record->hazard = true;
		record->hazard_what = sub.hazard_what;
		record->hazard_fn = sub.hazard_fn;
	      }
	    if ((sub.exposed_need & ~*sf) && !record->hazard)
	      {
		record->hazard = true;
		record->hazard_what = sub.exposed_what
		  ? sub.exposed_what : "an exposed launch in a callee";
		record->hazard_fn = sub.exposed_fn
		  ? sub.exposed_fn : ev.callee;
	      }
	    if ((sub.exposed_need & *sf & ~*se) && !record->exposed_what)
	      {
		record->exposed_what = sub.exposed_what;
		record->exposed_fn = sub.exposed_fn;
	      }
	    record->exposed_need |= sub.exposed_need & *sf & ~*se;
	  }
	unsigned transp = sub.out_full & ~sub.out_empty;
	*se = sub.out_empty | (*se & transp);
	*sf = sub.out_empty | (*sf & transp);
	return true;
      }
    }
  return true;
}

/* ---- Item #15 (rvtt-ipa-summary): the MOP cover-face body digest ----

   The legacy engine re-walked the gimple bodies of the transitive
   caller closure once per FORMING function.  The digest records the
   formee-independent classification once per body (the classified
   events plus the CFG skeleton the must-dataflow iterates over); the
   analysis replays it per formee, resolving only the formee-dependent
   CLOBBER/COMPOSE split at consult time (mop_resolve_event) with the
   identical cgraph lookups.

   ASM TIGHTENING (the plan's conversion 2 sub-step): the digest
   classifies assembly through the CANONICAL vocabulary only -- the
   empty barrier, the four audited scalar templates, and the
   single-constant `.ttinsn %0' form; every other template (including
   the base-ISA store-idiom shapes mop_classify_asm line-parsed)
   classifies as an opaque potential launch, strictly MORE
   conservative.  A whole-corpus -fchecking comparison verified the
   tightening clean; the legacy walk, its asm line parser, and the
   transitional verification shadow were then deleted.  */

static void
mop_digest_push (vec<rvtt_ipa_event> *out, const mop_event &ev, gimple *stmt)
{
  if (ev.kind == mop_event::BENIGN)
    return;
  gcc_assert (ev.kind == mop_event::LAUNCH || ev.kind == mop_event::COVER);
  rvtt_ipa_event de = {};
  de.kind = ev.kind == mop_event::LAUNCH
    ? rvtt_ipa_event::EV_LAUNCH : rvtt_ipa_event::EV_COVER;
  de.stmt = stmt;
  de.bits = ev.bits;
  de.what = ev.what;
  out->safe_push (de);
}

/* One statement's contribution to the digest: the formee-independent
   image of mop_classify_stmt (typed rvtt builtins and scalar builtins
   can never be a formee, so resolving them here commutes with the
   consult-time CLOBBER split).  */

static void
mop_digest_stmt (vec<rvtt_ipa_event> *out, gimple *stmt)
{
  if (is_gimple_debug (stmt))
    return;
  if (const gasm *a = dyn_cast<const gasm *> (stmt))
    {
      mop_event ev;
      const char *s = gimple_asm_string (a);
      while (*s == ' ' || *s == '\t')
	++s;
      if (!*s
	  || !strcmp (s, "fence") || !strcmp (s, "ebreak")
	  || !strcmp (s, "la sp, %0")
	  || !strcmp (s, ".option push\n.option norelax\n"
			 "la gp, __global_pointer$\n.option pop"))
	return;			/* barrier / audited scalar templates */
      if (strncmp (s, ".ttinsn", 7) == 0)
	{
	  s += 7;
	  while (*s == ' ' || *s == '\t')
	    ++s;
	  if (strcmp (s, "%0") == 0 && gimple_asm_ninputs (a) == 1
	      && gimple_asm_noutputs (a) == 0)
	    {
	      mop_classify_delivered_word
		(TREE_VALUE (gimple_asm_input_op (a, 0)), true, ev);
	      mop_digest_push (out, ev, stmt);
	      return;
	    }
	  ev.kind = mop_event::LAUNCH;
	  ev.bits = MOP_REQ_ANY;
	  ev.what = "a non-canonical .ttinsn template";
	  mop_digest_push (out, ev, stmt);
	  return;
	}
      /* The canonical-vocabulary tightening: no base-ISA line
	 parsing.  */
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "opaque assembly";
      mop_digest_push (out, ev, stmt);
      return;
    }
  if (is_gimple_call (stmt))
    {
      if (gimple_call_internal_p (stmt))
	return;
      tree fndecl = gimple_call_fndecl (stmt);
      if (!fndecl)
	{
	  rvtt_ipa_event de = {};
	  de.kind = rvtt_ipa_event::EV_LAUNCH;
	  de.stmt = stmt;
	  de.bits = MOP_REQ_ANY;
	  de.what = "an indirect call";
	  out->safe_push (de);
	  return;
	}
      if (const rvtt_insn_data *d = rvtt_get_insn_data (stmt))
	{
	  if (strncmp (d->name, "ttmop", 5) == 0)
	    {
	      rvtt_ipa_event de = {};
	      de.kind = rvtt_ipa_event::EV_LAUNCH;
	      de.stmt = stmt;
	      de.bits = MOP_REQ_ANY;
	      de.what = "a ttmop builtin";
	      out->safe_push (de);
	    }
	  return;
	}
      if (fndecl_built_in_p (fndecl))
	return;
      rvtt_ipa_event de = {};
      de.kind = rvtt_ipa_event::EV_CALL;
      de.stmt = stmt;
      de.decl = fndecl;
      de.composable = false;
      if (cgraph_node *cn = cgraph_node::get (fndecl))
	if (cn->definition || DECL_STRUCT_FUNCTION (fndecl))
	  de.composable = true;
      out->safe_push (de);
      return;
    }
  if (gimple_store_p (stmt) && is_gimple_assign (stmt))
    mop_digest_push (out, mop_classify_store (stmt), stmt);
}

/* Build FN's cover-face digest into IPA (events per block plus the CFG
   skeleton).  */

static void
mop_digest_build (function *fn, rvtt_ipa_fn_summary *ipa)
{
  ipa->mop_nblocks = last_basic_block_for_fn (fn);
  basic_block entry_bb = ENTRY_BLOCK_PTR_FOR_FN (fn);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rvtt_ipa_mop_block b;
      b.index = bb->index;
      b.exit_succ = false;
      b.preds = vNULL;
      b.events = vNULL;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->preds)
	b.preds.safe_push (e->src == entry_bb ? -1 : e->src->index);
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == EXIT_BLOCK_PTR_FOR_FN (fn))
	  b.exit_succ = true;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	mop_digest_stmt (&b.events, gsi_stmt (gsi));
      ipa->mop_blocks.safe_push (b);
    }
  ipa->mop_computed = true;
}

/* Resolve one digest event against this analysis's formee: the
   consult-time CLOBBER (a call into the forming function or an
   alias/clone of it) vs COMPOSE (an analyzable in-TU callee) vs benign
   (extern under the kernel-single-TU axiom) split, with the legacy
   lookup order.  */

static mop_event
mop_resolve_event (mop_outward_ctx &ctx, const rvtt_ipa_event &ev)
{
  mop_event mev;
  switch (ev.kind)
    {
    case rvtt_ipa_event::EV_LAUNCH:
      mev.kind = mop_event::LAUNCH;
      mev.bits = ev.bits;
      mev.what = ev.what;
      break;
    case rvtt_ipa_event::EV_COVER:
      mev.kind = mop_event::COVER;
      mev.bits = ev.bits;
      break;
    case rvtt_ipa_event::EV_CALL:
      if (ev.decl == ctx.formee)
	{
	  mev.kind = mop_event::CLOBBER;
	  break;
	}
      if (ctx.formee_node && ev.decl)
	if (cgraph_node *cn = cgraph_node::get (ev.decl))
	  if (cn->ultimate_alias_target () == ctx.formee_node)
	    {
	      mev.kind = mop_event::CLOBBER;
	      break;
	    }
      if (ev.composable)
	{
	  mev.kind = mop_event::COMPOSE;
	  mev.callee = ev.decl;
	}
      break;			/* extern with no body: benign */
    default:
      gcc_unreachable ();
    }
  return mev;
}

/* Analyze DECL through its digest: the must-dataflow and recording
   pass over recorded events instead of statements.  */

static mop_caller_summary &
mop_analyze_fn (mop_outward_ctx &ctx, tree decl)
{
  mop_caller_summary &sum = ctx.summaries[decl];
  if (sum.computed)
    return sum;
  if (sum.in_progress)
    {
      /* Recursive caller chain: no epoch discipline is provable.  */
      sum.computed = true;
      sum.valid = false;
      sum.invalid_why = "recursive call chain";
      return sum;
    }
  sum.in_progress = true;

  function *fn = DECL_STRUCT_FUNCTION (decl);
  if (!fn || !fn->cfg || (fn->curr_properties & PROP_rtl))
    {
      sum.in_progress = false;
      sum.computed = true;
      sum.valid = false;
      sum.invalid_why = "body not analyzable at formation time";
      return sum;
    }
  cgraph_node *node = cgraph_node::get (decl);
  rvtt_ipa_fn_summary *ipa = node ? rvtt_ipa_summary_get (node) : nullptr;
  if (!ipa)
    {
      sum.in_progress = false;
      sum.computed = true;
      sum.valid = false;
      sum.invalid_why = "body not analyzable at formation time";
      return sum;
    }
  if (!ipa->mop_computed)
    {
      mop_digest_build (fn, ipa);
      if (dump_file)
	fprintf (dump_file,
		 "ipa-summary: mop-face digest built (%s, %d blocks)\n",
		 node->dump_name (), ipa->mop_nblocks);
    }

  unsigned n = ipa->mop_nblocks;
  std::vector<const rvtt_ipa_mop_block *> blocks (n, nullptr);
  for (const rvtt_ipa_mop_block &b : ipa->mop_blocks)
    blocks[b.index] = &b;

  /* Per-BB IN states; TOP = all-ones on both tracks.  */
  std::vector<unsigned> in_se (n, MOP_STATE_FULL);
  std::vector<unsigned> in_sf (n, MOP_STATE_FULL);

  bool valid = true;
  const char *invalid_why = nullptr;

  /* Fixpoint (states only descend).  */
  bool changed = true;
  unsigned iter = 0;
  while (changed && valid && iter++ < 64)
    {
      changed = false;
      for (const rvtt_ipa_mop_block &blk : ipa->mop_blocks)
	{
	  unsigned se = MOP_STATE_FULL, sf = MOP_STATE_FULL;
	  bool first = true;
	  for (int pred : blk.preds)
	    {
	      unsigned pse, psf;
	      if (pred < 0)
		{
		  pse = 0;
		  psf = MOP_STATE_FULL;
		}
	      else
		{
		  /* Predecessor OUT: recompute cheaply by transfer of
		     its stored IN (the legacy discipline).  */
		  pse = in_se[pred];
		  psf = in_sf[pred];
		  if (const rvtt_ipa_mop_block *pb = blocks[pred])
		    for (const rvtt_ipa_event &pev : pb->events)
		      if (!mop_apply_event (ctx,
					    mop_resolve_event (ctx, pev),
					    &pse, &psf, nullptr, decl))
			{
			  valid = false;
			  invalid_why = "unanalyzable callee";
			}
		}
	      if (first)
		{
		  se = pse;
		  sf = psf;
		  first = false;
		}
	      else
		{
		  se &= pse;
		  sf &= psf;
		}
	    }
	  if (first)
	    /* Unreachable block; keep TOP.  */
	    continue;
	  if (se != in_se[blk.index] || sf != in_sf[blk.index])
	    {
	      /* Must-meet only descends.  */
	      in_se[blk.index] &= se;
	      in_sf[blk.index] &= sf;
	      changed = true;
	    }
	}
    }
  if (changed && valid)
    {
      valid = false;
      invalid_why = "cover dataflow did not converge";
    }

  /* Recording pass: hazards, entry requirements, exit meets.  */
  unsigned out_e = MOP_STATE_FULL, out_f = MOP_STATE_FULL;
  bool have_exit = false;
  if (valid)
    for (const rvtt_ipa_mop_block &blk : ipa->mop_blocks)
      {
	unsigned se = in_se[blk.index];
	unsigned sf = in_sf[blk.index];
	for (const rvtt_ipa_event &ev : blk.events)
	  if (!mop_apply_event (ctx, mop_resolve_event (ctx, ev),
				&se, &sf, &sum, decl))
	    {
	      valid = false;
	      invalid_why = "unanalyzable callee";
	    }
	if (blk.exit_succ)
	  {
	    out_e &= se;
	    out_f &= sf;
	    have_exit = true;
	  }
      }
  if (!have_exit)
    {
      out_e = MOP_STATE_FULL;
      out_f = MOP_STATE_FULL;
    }

  sum.in_progress = false;
  sum.computed = true;
  sum.valid = valid;
  sum.invalid_why = invalid_why;
  sum.out_empty = out_e;
  sum.out_full = out_f;
  return sum;
}

/* Discharge the outward ownership obligation for CFN.  On failure,
   *WHY and *WHY_FN carry the refusal detail; on success *HOW names the
   discharged form for the dump.  */

} /* anon namespace */

/* See rvtt-mop-tables.h.  The only name this translation unit exports:
   whether every transitive caller of CFN leaves the MOP template live.  */

bool
rvtt_mop_outward_owned_p (function *cfn, const char **why,
			  const char **why_fn, const char **how)
{
  tree decl = cfn->decl;
  *why = nullptr;
  *why_fn = nullptr;

  if (DECL_NAME (decl) && MAIN_NAME_P (DECL_NAME (decl)))
    {
      /* The kernel entry: its only caller is crt0, which delivers no
	 Tensix work (AXIOM crt0-benign).  */
      *how = "kernel entry (crt0-benign axiom)";
      return true;
    }

  cgraph_node *node = cgraph_node::get (decl);
  if (!node)
    {
      *why = "no callgraph node for the function";
      return false;
    }

  /* Transitive caller closure under the kernel-single-TU axiom.  */
  auto_vec<cgraph_node *> closure;
  hash_set<cgraph_node *> seen;
  auto_vec<cgraph_node *> work;
  work.safe_push (node);
  seen.add (node);
  while (!work.is_empty ())
    {
      cgraph_node *cur = work.pop ();
      if (cur->address_taken)
	{
	  *why = "address-taken function on the caller chain";
	  *why_fn = cur->dump_name ();
	  return false;
	}
      for (cgraph_edge *e = cur->callers; e; e = e->next_caller)
	{
	  cgraph_node *c = e->caller;
	  if (c->inlined_to)
	    c = c->inlined_to;
	  if (seen.add (c))
	    continue;
	  closure.safe_push (c);
	  work.safe_push (c);
	}
    }

  if (closure.is_empty ())
    {
      /* No caller inside the thread program: outermost by the
	 kernel-single-TU + crt0-benign axioms.  */
      *how = "outermost (no caller in the TU)";
      return true;
    }

  mop_outward_ctx ctx;
  ctx.formee = decl;
  ctx.formee_node = node;

  unsigned roots = 0;
  for (cgraph_node *m : closure)
    {
      if (m->callers)
	continue;		/* analyzed via its own callers */
      ++roots;
      mop_caller_summary &sum = mop_analyze_fn (ctx, m->decl);
      if (!sum.valid)
	{
	  *why = sum.invalid_why ? sum.invalid_why
				 : "caller not analyzable";
	  *why_fn = m->dump_name ();
	  return false;
	}
      if (sum.hazard)
	{
	  static char detail[256];
	  const char *what = sum.hazard_what ? sum.hazard_what
					     : "a MOP launch";
	  const char *where
	    = sum.hazard_fn ? lang_hooks.decl_printable_name (sum.hazard_fn, 2)
			    : m->dump_name ();
	  snprintf (detail, sizeof (detail),
		    "%s in '%s' is reachable after a call to this"
		    " function without a full template re-arm",
		    what, where);
	  *why = detail;
	  *why_fn = m->dump_name ();
	  return false;
	}
      /* Root entry: no caller-programmed template can be live (the
	 axioms above), so exposed_need at a root is pre-existing
	 caller-owned state, not our clobber.  */
    }

  if (roots == 0)
    {
      /* Every closure member has callers: a cycle with no entry.  */
      *why = "recursive caller chain";
      return false;
    }

  *how = "every caller root re-arms the template before its next"
	 " post-return MOP launch";
  return true;
}
