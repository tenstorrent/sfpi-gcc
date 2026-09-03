/* Cross-call constant delivery: the init and ADDR_MOD hoist services
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* rvtt_crosscall_init_hoist and rvtt_crosscall_addrmod_hoist, the
   cross-call hoist services the invariant pass calls: replay a
   callee's delivered-word digest against the caller chain, prove
   value equality at the dominating call, and commit the hoisted
   programming in the caller preheader.  Split from
   gimple-rvtt-crosscall.cc; the algorithm essay lives there.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssanames.h"
#include "tree-eh.h"
#include "gimplify.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "tree-dfa.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "insn-codes.h"
#include "insn-config.h"
#include "recog.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-macro-tables.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-ipa-summary.h"
#include "rvtt-cc-region.h"
#include "rvtt-raw-boundary.h"
#include "gimple-rvtt-crosscall-int.h"

/* ==================================================================
   Lane CA: cross-call invariant-init hoist (macro-planner service).

   A noinline per-tile callee whose macro formation emits an idempotent
   init prefix -- the derived descriptor program (staged SFPLOADI +
   SFPCONFIG writes), the owned SETC16 address-modifier program, and the
   proven all-lanes enable -- re-executes that prefix on EVERY call,
   although every written value is compile-time descriptor data that
   provably cannot change between calls.  A hand kernel programs the
   equivalent state once at kernel init.  This service, called from the
   callee's macro-planner formation (rtl-rvtt-macro-planner.cc) while
   every caller body is still gimple (callees run the late pipeline
   before their callers -- the same ordering fact this file's
   coefficient hoist relies on), proves the caller side and, on a
   complete proof, inserts the prefix as typed builtin calls in the
   caller's loop preheader; the planner then omits the hoisted part
   from the callee's emission.  Both sides commit together or not at
   all; every unproven link refuses by name and the per-call prefix
   stays byte-identically.

   Two stages, decided by proof strength:

     stage 1 (descriptor words): the SFPCONFIG destinations are read
       ONLY by SFPLOADMACRO launches (SFPLOADMACRO.md: the launch
       resolves templates/sequence/misc from LoadMacroConfig), and the
       callee holds the function's only launches; the caller epoch must
       merely prove no LoadMacroConfig writer and no launch inside the
       loop.  The callee retains its per-call enable + owned SETC16
       program.  The hoisted block carries its own all-lanes enable for
       the lane-predicated staging loads, under the architectural
       outermost-CC contract (the same license as the planner's
       materialized enable).

     stage 2 (full prefix): additionally the owned SETC16 rows and the
       lane state.  Sound only when the caller's reaching configuration
       is VALUE-EQUAL: every decodable SETC16-class delivery to an
       owned row anywhere in the caller equals the contract's encoded
       word, and each owned row has such a write dominating the loop --
       then no instruction between the preheader and the first call can
       distinguish the hoisted programming from today's state, whatever
       it reads.  Loop CC-cleanliness keeps the hoisted enable's state
       across every trip (the callee's own body was proven CC-neutral
       by its side of the contract).

   Refusal vocabulary (stable, append-only):
     drain-init-callers-unproven   closure (alias/clone/address-taken/
				   multi-caller/multi-site/expanded)
     drain-init-loop-unproven      no natural loop / no provable entry
     drain-init-ownership-unproven a loop statement or delivered word
				   that could write the hoisted state,
				   launch a macro, or replay recorded
				   content (the mission-named refusal)
     drain-init-vector-live	   vector dataflow in the loop (a later
				   formation could own the state)
     drain-init-mop-slot-unproven  the TU template census cannot prove
				   the MOP words init-inert
   Stage-2 demotions (to stage 1) are not refusals: value inequality or
   loop CC writes simply keep the enable + SETC16 per call.  */

namespace {

/* Word classification for the init face: can this delivered word write
   LoadMacroConfig (SFPCONFIG class), launch a macro, or replay recorded
   content?  The classifier now lives in THE unified audited word-fact
   table (rvtt-raw-boundary.cc rvtt_word_facts_classify);
   rvtt_word_init_class is this face's query accessor
   -- same question, same verdicts, same refusal names, refusing
   default for every class not on record, with the caps-keyed
   SETC16/SFPCONFIG opcode checks and the owned-row tracking (stage 2)
   applied at the accessor.  The verdict struct keeps its local
   spelling.  */

typedef rvtt_wf_init_verdict init_word_verdict;

/* Init-face analogue of classify_delivered_value: classify the
   delivered word VAL against contract program PROG under target
   capabilities C.  A constant resolves exactly; a runtime-completed
   composition keeps its opcode class but clears WORD_EXACT (the
   stage-2 value-equality proof cannot use it); an underivable word
   refuses.  */

static init_word_verdict
classify_delivered_init (tree val, const rvtt_init_hoist_program &prog,
			 const rvtt_macro::caps *c)
{
  init_word_verdict v
    = { false, false, false, 0, false, "drain-init-ownership-unproven" };
  if (TREE_CODE (val) == INTEGER_CST)
    return rvtt_word_init_class ((uint32_t) (TREE_INT_CST_LOW (val)
					   & 0xffffffff), prog, c);
  uint32_t base;
  if (!pushed_word_base (val, &base))
    return v;
  v = rvtt_word_init_class (base, prog, c);
  /* A runtime-completed word keeps its opcode and non-value fields
     under the field axiom, but its exact value is not a constant --
     the stage-2 value-equality proof cannot use it.  */
  v.word_exact = false;
  return v;
}

struct init_scan_ctx
{
  const rvtt_init_hoist_program *prog;
  const rvtt_macro::caps *c;
  tree callee_decl;		/* admitted contract-call target      */
  hash_set<tree> *chain_decls = nullptr; /* admitted chain-call targets */
  /* The resolved contract call STATEMENT, admitted by identity: a
     constprop/IPA clone's call statement can still spell the origin
     decl while the cgraph edge targets the clone, so decl comparison
     alone mis-refuses the contract call itself.  Statement
     identity admits exactly the one proven edge and nothing else.  */
  gimple *contract_call = nullptr;
  bool saw_mop = false;
  bool cc_dirty = false;	/* loop CC write: demotes stage 2      */
  bool owned_row_dirty = false;	/* in-loop owned-row write: demotes    */
  const char *why = nullptr;
  gimple *why_stmt = nullptr;
};

/* Record the init-face refusal WHY at STMT in CTX, emit the named
   refusal counter and its dump line, and return false as the scan's
   failing result.  */

static bool
init_refuse (init_scan_ctx *ctx, const char *why, gimple *stmt)
{
  ctx->why = why;
  ctx->why_stmt = stmt;
  rvtt_refuse_by_name_at (why, stmt, dump_file,
			  "init-hoist: refused (%s)", why);
  if (dump_file)
    {
      if (stmt)
	{
	  fprintf (dump_file, ": ");
	  print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
	}
      else
	fprintf (dump_file, "\n");
    }
  return false;
}

/* Fold word verdict V into CTX: note a seen MOP launch and any
   owned-row write (a stage-2 demotion), and refuse through
   init_refuse when the word is not proven inert.  */

static bool
apply_init_verdict (init_scan_ctx *ctx, const init_word_verdict &v,
		    gimple *stmt)
{
  if (v.is_mop)
    ctx->saw_mop = true;
  if (v.owned_row_write)
    ctx->owned_row_dirty = true;
  if (!v.ok)
    return init_refuse (ctx, v.why, stmt);
  return true;
}

/* One asm statement of the caller's loop (mirrors scan_asm on the
   init face).  */

static bool
init_scan_asm (init_scan_ctx *ctx, gasm *stmt)
{
  const char *str = gimple_asm_string (stmt);
  const char *sp = str;
  while (*sp == ' ' || *sp == '\t')
    ++sp;
  if (strncmp (sp, ".ttinsn", 7) == 0)
    {
      const char *t = sp + 7;
      while (*t == ' ' || *t == '\t')
	++t;
      if (strcmp (t, "%0") != 0 || gimple_asm_ninputs (stmt) != 1
	  || gimple_asm_noutputs (stmt) != 0)
	return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
      return apply_init_verdict
	(ctx, classify_delivered_init
	   (TREE_VALUE (gimple_asm_input_op (stmt, 0)), *ctx->prog, ctx->c),
	 stmt);
    }
  tree value, ptr;
  if (blocking_store_asm_p (stmt, &value, &ptr))
    {
      unsigned HOST_WIDE_INT addr;
      if (pointer_constant_address (ptr, &addr))
	{
	  if (addr >= XTT_INSTRN_BUF_MMIO_BASE
	      && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	    return apply_init_verdict
	      (ctx, classify_delivered_init (value, *ctx->prog, ctx->c),
	       stmt);
	  if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	    return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
	  return true;		/* sync/data aperture */
	}
      return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
    }
  if (audited_scalar_asm_p (str))
    return true;
  return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
}

/* One statement of the caller's loop body.  */

static bool
init_scan_stmt (init_scan_ctx *ctx, gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_COND
      || gimple_code (stmt) == GIMPLE_GOTO
      || gimple_code (stmt) == GIMPLE_NOP
      || gimple_code (stmt) == GIMPLE_PREDICT
      || gimple_code (stmt) == GIMPLE_RETURN)
    return true;

  if (gasm *a = dyn_cast <gasm *> (stmt))
    return init_scan_asm (ctx, a);

  if (gcall *call = dyn_cast <gcall *> (stmt))
    {
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (insnd)
	{
	  if (insnd->sets_cc (call))
	    {
	      /* A loop CC write demotes stage 2 (the hoisted enable
		 would not survive the trip); stage 1 is indifferent
		 (the callee re-establishes its own lane state).  */
	      ctx->cc_dirty = true;
	      return true;
	    }
	  switch (insnd->id)
	    {
	    case rvtt_insn_data::ttreplay:
	      return init_refuse (ctx, "drain-init-ownership-unproven",
				  stmt);
	    case rvtt_insn_data::sfpwriteconfig_v:
	    case rvtt_insn_data::sfpconfig_i:
	      return init_refuse (ctx, "drain-init-ownership-unproven",
				  stmt);
	    case rvtt_insn_data::ttsetc16:
	    case rvtt_insn_data::sfpencc_all_lanes:
	      /* Another compiler contract's programming: not ordered
		 against this one.  */
	      return init_refuse (ctx, "drain-init-ownership-unproven",
				  stmt);
	    case rvtt_insn_data::ttmovd2b:
	    case rvtt_insn_data::ttmovb2a:
	    case rvtt_insn_data::ttmovb2d:
	    case rvtt_insn_data::ttmova2d:
	    case rvtt_insn_data::tttrnspsrcb:
	    case rvtt_insn_data::ttstallwait:
	    case rvtt_insn_data::ttrmwcib:
	      /* The FPU face-transpose family: Matrix-Unit
		 choreography programming Dst rows, Src banks, and backend
		 configuration -- state this contract neither owns nor
		 orders against.  Fail closed.  */
	      return init_refuse (ctx, "drain-init-ownership-unproven",
				  stmt);
	    default:
	      break;
	    }
	  if (call_has_vector_dataflow_p (call))
	    /* Vector dataflow inside the loop: a later formation in
	       the caller could own the very state this contract
	       hoists.	*/
	    return init_refuse (ctx, "drain-init-vector-live", stmt);
	  return true;
	}
      if (ctx->contract_call && stmt == ctx->contract_call)
	return true;		/* the resolved contract edge itself */
      tree target = gimple_call_fndecl (call);
      if (ctx->callee_decl && target == ctx->callee_decl)
	return true;		/* the contract call itself */
      if (target && ctx->chain_decls && ctx->chain_decls->contains (target))
	return true;		/* a proven chain hop */
      if (gimple_call_internal_p (call))
	return gimple_vdef (call)
	  ? init_refuse (ctx, "drain-init-ownership-unproven", stmt) : true;
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	return true;		/* scalar compiler builtin */
      return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
    }

  if (is_gimple_assign (stmt))
    {
      if (vector_typed_p (gimple_assign_lhs (stmt)))
	return init_refuse (ctx, "drain-init-vector-live", stmt);
      if (!gimple_store_p (stmt))
	return true;
      tree lhs = gimple_get_lhs (stmt);
      if (!lhs || TREE_CODE (lhs) == SSA_NAME)
	return true;
      unsigned HOST_WIDE_INT addr;
      if (ref_constant_address (lhs, &addr))
	{
	  if (addr >= XTT_INSTRN_BUF_MMIO_BASE
	      && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	    return apply_init_verdict
	      (ctx, classify_delivered_init (gimple_assign_rhs1 (stmt),
					     *ctx->prog, ctx->c), stmt);
	  if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	    return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
	  return true;		/* other constant MMIO / L1 */
	}
      tree base = get_base_address (lhs);
      if (!TREE_THIS_VOLATILE (lhs)
	  && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
	return true;		/* plain memory */
      if (base && DECL_P (base))
	{
	  const char *name = DECL_ASSEMBLER_NAME (base)
	    ? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (base)) : nullptr;
	  if (name && !strcmp (name, "__instrn_buffer"))
	    return apply_init_verdict
	      (ctx, classify_delivered_init (gimple_assign_rhs1 (stmt),
					     *ctx->prog, ctx->c), stmt);
	  if (!DECL_EXTERNAL (base))
	    return true;	/* TU data object */
	}
      return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
    }

  return init_refuse (ctx, "drain-init-ownership-unproven", stmt);
}

/* ------------------------------------------------------------------ */
/* Item #15 (rvtt-ipa-summary): the init/epoch-face body digest.

   The chain hops' whole-body scans re-ran init_scan_stmt over the same
   hop bodies once per consuming contract (the init hoist and the
   ADDR_MOD hoist each re-walked every hop per callee).  The digest
   records init_scan_stmt's classification of one body ONCE in a
   parameter-independent event stream; init_replay_events re-folds it
   against a contract's parameters (owned rows via rvtt_word_init_class
   on the recorded word image, call admission via the contract's decl
   set) with the exact legacy accumulator and refusal emission.  Every
   arm below mirrors init_scan_stmt / init_scan_asm /
   classify_delivered_init's input handling verbatim (the one-pin
   flag_checking shadow walk that hard-asserted this was deleted at
   pin 53 on a clean corpus -fchecking leg).  */

static void
init_digest_push_refuse (vec<rvtt_ipa_event> *out, gimple *stmt,
			 const char *why)
{
  rvtt_ipa_event ev = {};
  ev.kind = rvtt_ipa_event::EV_REFUSE;
  ev.stmt = stmt;
  ev.what = why;
  out->safe_push (ev);
}

/* A delivered word: record the constant image (or the constant opcode
   base of a runtime-completed word, WORD_EXACT false -- the
   classify_delivered_init split), refusing the underivable default.  */

static void
init_digest_push_deliver (vec<rvtt_ipa_event> *out, gimple *stmt, tree val)
{
  rvtt_ipa_event ev = {};
  ev.kind = rvtt_ipa_event::EV_DELIVER;
  ev.stmt = stmt;
  if (TREE_CODE (val) == INTEGER_CST)
    {
      ev.word = (uint32_t) (TREE_INT_CST_LOW (val) & 0xffffffff);
      ev.word_exact = true;
      out->safe_push (ev);
      return;
    }
  uint32_t base;
  if (!pushed_word_base (val, &base))
    {
      init_digest_push_refuse (out, stmt, "drain-init-ownership-unproven");
      return;
    }
  ev.word = base;
  ev.word_exact = false;
  out->safe_push (ev);
}

/* One statement's contribution to the digest (mirrors init_scan_stmt;
   statements the scan admits parameter-free record nothing).  */

static void
init_digest_stmt (vec<rvtt_ipa_event> *out, gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_COND
      || gimple_code (stmt) == GIMPLE_GOTO
      || gimple_code (stmt) == GIMPLE_NOP
      || gimple_code (stmt) == GIMPLE_PREDICT
      || gimple_code (stmt) == GIMPLE_RETURN)
    return;

  if (gasm *a = dyn_cast <gasm *> (stmt))
    {
      const char *str = gimple_asm_string (a);
      const char *sp = str;
      while (*sp == ' ' || *sp == '\t')
	++sp;
      if (strncmp (sp, ".ttinsn", 7) == 0)
	{
	  const char *t = sp + 7;
	  while (*t == ' ' || *t == '\t')
	    ++t;
	  if (strcmp (t, "%0") != 0 || gimple_asm_ninputs (a) != 1
	      || gimple_asm_noutputs (a) != 0)
	    return init_digest_push_refuse (out, stmt,
					    "drain-init-ownership-unproven");
	  return init_digest_push_deliver
	    (out, stmt, TREE_VALUE (gimple_asm_input_op (a, 0)));
	}
      tree value, ptr;
      if (blocking_store_asm_p (a, &value, &ptr))
	{
	  unsigned HOST_WIDE_INT addr;
	  if (pointer_constant_address (ptr, &addr))
	    {
	      if (addr >= XTT_INSTRN_BUF_MMIO_BASE
		  && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
		return init_digest_push_deliver (out, stmt, value);
	      if (addr >= XTT_MOP_CFG_MMIO_BASE
		  && addr <= XTT_MOP_CFG_MMIO_LIMIT)
		return init_digest_push_refuse
		  (out, stmt, "drain-init-ownership-unproven");
	      return;		/* sync/data aperture */
	    }
	  return init_digest_push_refuse (out, stmt,
					  "drain-init-ownership-unproven");
	}
      if (audited_scalar_asm_p (str))
	return;
      return init_digest_push_refuse (out, stmt,
				      "drain-init-ownership-unproven");
    }

  if (gcall *call = dyn_cast <gcall *> (stmt))
    {
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (insnd)
	{
	  if (insnd->sets_cc (call))
	    {
	      rvtt_ipa_event ev = {};
	      ev.kind = rvtt_ipa_event::EV_CC_WRITE;
	      ev.stmt = stmt;
	      out->safe_push (ev);
	      return;
	    }
	  switch (insnd->id)
	    {
	    case rvtt_insn_data::ttreplay:
	    case rvtt_insn_data::sfpwriteconfig_v:
	    case rvtt_insn_data::sfpconfig_i:
	    case rvtt_insn_data::ttsetc16:
	    case rvtt_insn_data::sfpencc_all_lanes:
	    case rvtt_insn_data::ttmovd2b:
	    case rvtt_insn_data::ttmovb2a:
	    case rvtt_insn_data::ttmovb2d:
	    case rvtt_insn_data::ttmova2d:
	    case rvtt_insn_data::tttrnspsrcb:
	    case rvtt_insn_data::ttstallwait:
	    case rvtt_insn_data::ttrmwcib:
	      return init_digest_push_refuse
		(out, stmt, "drain-init-ownership-unproven");
	    default:
	      break;
	    }
	  if (call_has_vector_dataflow_p (call))
	    return init_digest_push_refuse (out, stmt,
					    "drain-init-vector-live");
	  return;
	}
      /* Untyped calls that admit parameter-free: internal without a
	 vdef, scalar compiler builtins.  Everything else is a
	 consumer-side admission (the contract call / chain decls) --
	 an internal call or a builtin can never be one of those, so
	 hoisting their classification ahead of the admission checks is
	 order-inert.  */
      if (gimple_call_internal_p (call))
	{
	  if (gimple_vdef (call))
	    init_digest_push_refuse (out, stmt,
				     "drain-init-ownership-unproven");
	  return;
	}
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	return;
      rvtt_ipa_event ev = {};
      ev.kind = rvtt_ipa_event::EV_CALL;
      ev.stmt = stmt;
      ev.decl = fndecl;
      out->safe_push (ev);
      return;
    }

  if (is_gimple_assign (stmt))
    {
      if (vector_typed_p (gimple_assign_lhs (stmt)))
	return init_digest_push_refuse (out, stmt, "drain-init-vector-live");
      if (!gimple_store_p (stmt))
	return;
      tree lhs = gimple_get_lhs (stmt);
      if (!lhs || TREE_CODE (lhs) == SSA_NAME)
	return;
      unsigned HOST_WIDE_INT addr;
      if (ref_constant_address (lhs, &addr))
	{
	  if (addr >= XTT_INSTRN_BUF_MMIO_BASE
	      && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	    return init_digest_push_deliver (out, stmt,
					     gimple_assign_rhs1 (stmt));
	  if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	    return init_digest_push_refuse
	      (out, stmt, "drain-init-ownership-unproven");
	  return;		/* other constant MMIO / L1 */
	}
      tree base = get_base_address (lhs);
      if (!TREE_THIS_VOLATILE (lhs)
	  && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
	return;			/* plain memory */
      if (base && DECL_P (base))
	{
	  const char *name = DECL_ASSEMBLER_NAME (base)
	    ? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (base)) : nullptr;
	  if (name && !strcmp (name, "__instrn_buffer"))
	    return init_digest_push_deliver (out, stmt,
					     gimple_assign_rhs1 (stmt));
	  if (!DECL_EXTERNAL (base))
	    return;		/* TU data object */
	}
      return init_digest_push_refuse (out, stmt,
				      "drain-init-ownership-unproven");
    }

  init_digest_push_refuse (out, stmt, "drain-init-ownership-unproven");
}

/* Replay a body digest against this contract's parameters, with the
   legacy accumulator/emission semantics (stop at the first refusal;
   dumps and refusal counters fire exactly as the legacy walk's
   init_refuse did).  */

static bool
init_replay_events (init_scan_ctx *ctx, const vec<rvtt_ipa_event> &evs)
{
  for (const rvtt_ipa_event &ev : evs)
    switch (ev.kind)
      {
      case rvtt_ipa_event::EV_CC_WRITE:
	ctx->cc_dirty = true;
	break;
      case rvtt_ipa_event::EV_REFUSE:
	return init_refuse (ctx, ev.what, ev.stmt);
      case rvtt_ipa_event::EV_DELIVER:
	{
	  init_word_verdict v
	    = rvtt_word_init_class (ev.word, *ctx->prog, ctx->c);
	  if (!ev.word_exact)
	    v.word_exact = false;
	  if (!apply_init_verdict (ctx, v, ev.stmt))
	    return false;
	  break;
	}
      case rvtt_ipa_event::EV_CALL:
	if (ctx->contract_call && ev.stmt == ctx->contract_call)
	  break;			/* the resolved contract edge */
	if (ctx->callee_decl && ev.decl == ctx->callee_decl)
	  break;			/* the contract call itself */
	if (ev.decl && ctx->chain_decls
	    && ctx->chain_decls->contains (ev.decl))
	  break;			/* a proven chain hop */
	return init_refuse (ctx, "drain-init-ownership-unproven", ev.stmt);
      default:
	gcc_unreachable ();
      }
  return true;
}

/* The chain hops' whole-body epoch scans, IPA-summary-fed: one
   digest per hop body, computed once and consulted per contract,
   replacing the per-contract re-walks.  FACE_UNAVAILABLE is the
   consuming face's closure refusal; TAG its dump prefix.  Returns the
   refusal name, or null with CTX's accumulators advanced.  */

static const char *
scan_chain_hops (init_scan_ctx *ctx, const auto_vec<cgraph_node *, 4> &chain,
		 const char *face_unavailable, const char *tag)
{
  for (cgraph_node *hop : chain)
    {
      cgraph_node *body_node = hop;
      while (body_node && !gimple_has_body_p (body_node->decl)
	     && body_node->clone_of)
	body_node = body_node->clone_of;
      function *hfn = body_node
	? DECL_STRUCT_FUNCTION (body_node->decl) : nullptr;
      if (!hfn || !hfn->cfg)
	{
	  if (dump_file)
	    fprintf (dump_file, "%s: closure (hop-body %s)\n", tag,
		     hop->dump_name ());
	  return face_unavailable;
	}
      rvtt_ipa_fn_summary *sum = rvtt_ipa_summary_get (body_node);
      if (!sum)
	{
	  /* The engine could not see the walkable body this face just
	     probed: fail closed on the face's own name.  */
	  if (dump_file)
	    fprintf (dump_file, "%s: closure (hop-body %s)\n", tag,
		     hop->dump_name ());
	  return face_unavailable;
	}
      if (!sum->init_computed)
	{
	  basic_block hbb;
	  FOR_EACH_BB_FN (hbb, hfn)
	    for (gimple_stmt_iterator gsi = gsi_start_bb (hbb);
		 !gsi_end_p (gsi); gsi_next (&gsi))
	      init_digest_stmt (&sum->init_events, gsi_stmt (gsi));
	  sum->init_computed = true;
	  /* The one new dump spelling is face-neutral by design: the
	     bare face tags are pinned scan-dump-not patterns in the
	     twin suite.  */
	  if (dump_file)
	    fprintf (dump_file,
		     "ipa-summary: init-face digest built (%s, %u events)\n",
		     body_node->dump_name (), sum->init_events.length ());
	}
      bool ok = init_replay_events (ctx, sum->init_events);
      if (!ok)
	return ctx->why;
    }
  return nullptr;
}

/* Stage-2 value equality: every decodable SETC16-class delivery to an
   owned row anywhere in CALLER_FN must equal the contract's encoded
   word, and each owned row needs one such delivery whose block
   dominates the loop header.  Any shortfall demotes to stage 1 (never
   a refusal).  Runs under the caller's cfun with dominators live.  */

/* One statement's contribution to the value-equality proof: when STMT
   delivers a SETC16-class word to an owned row, require it equal the
   contract's encoded word; DOM_BB (the statement's executing site in
   the hoist function, or the U-level call block for a committed-inline
   body) marks a dominating reaching write.  Returns false on an
   unequal or unprovable owned-row write.  */

static bool
init_value_equal_stmt (gimple *stmt, basic_block dom_bb, class loop *loop,
		       const rvtt_init_hoist_program &prog,
		       const rvtt_macro::caps *c, const uint32_t *want,
		       bool *have_dom)
{
  /* Typed ttsetc16 builtin calls are SETC16 deliveries too.  Only the
     ADDR_MOD contract commit plants them in caller bodies, so
     the widening is gated by its flag: with the flag off no such call
     exists on any audited path and the historical walk is byte-
     identical.  An owned-row write with a non-constant operand or an
     unequal value refuses (demotes) exactly like a delivered word.  */
  if (riscv_tt_opt_crosscall_addrmod)
    if (gcall *call = dyn_cast <gcall *> (stmt))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	if (insnd && insnd->id == rvtt_insn_data::ttsetc16)
	  {
	    tree reg_arg = gimple_call_arg (call, 0);
	    tree val_arg = gimple_call_arg (call, 1);
	    if (TREE_CODE (reg_arg) != INTEGER_CST
		|| TREE_CODE (val_arg) != INTEGER_CST)
	      {
		/* Unresolvable row: could be an owned-row write.  */
		if (dump_file)
		  fprintf (dump_file, "init-hoist: value-equality: typed "
			   "setc16 with non-constant operands\n");
		return false;
	      }
	    unsigned reg = TREE_INT_CST_LOW (reg_arg) & 0xff;
	    unsigned value = TREE_INT_CST_LOW (val_arg) & 0xffff;
	    for (unsigned i = 0; i != prog.n_setc16; ++i)
	      if (prog.setc16[i].reg == reg)
		{
		  uint32_t word;
		  if (!rvtt_macro::encode_setc16 (c, reg, value, &word)
		      || word != want[i])
		    {
		      if (dump_file)
			fprintf (dump_file, "init-hoist: value-equality: "
				 "row %u typed setc16 unequal\n", reg);
		      return false;
		    }
		  if (dom_bb
		      && dominated_by_p (CDI_DOMINATORS, loop->header,
					 dom_bb))
		    have_dom[i] = true;
		}
	    return true;
	  }
      }
  {
	tree val = NULL_TREE;
	if (gasm *a = dyn_cast <gasm *> (stmt))
	  {
	    const char *sp = gimple_asm_string (a);
	    while (*sp == ' ' || *sp == '\t')
	      ++sp;
	    if (strncmp (sp, ".ttinsn", 7) == 0
		&& gimple_asm_ninputs (a) == 1)
	      val = TREE_VALUE (gimple_asm_input_op (a, 0));
	    else
	      {
		tree ptr;
		tree v2;
		if (blocking_store_asm_p (a, &v2, &ptr))
		  {
		    unsigned HOST_WIDE_INT addr;
		    if (pointer_constant_address (ptr, &addr)
			&& addr >= XTT_INSTRN_BUF_MMIO_BASE
			&& addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
		      val = v2;
		  }
	      }
	  }
	else if (is_gimple_assign (stmt) && gimple_store_p (stmt))
	  {
	    tree lhs = gimple_get_lhs (stmt);
	    unsigned HOST_WIDE_INT addr;
	    if (lhs && TREE_CODE (lhs) != SSA_NAME
		&& ref_constant_address (lhs, &addr)
		&& addr >= XTT_INSTRN_BUF_MMIO_BASE
		&& addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	      val = gimple_assign_rhs1 (stmt);
	    else if (lhs)
	      {
		tree base = get_base_address (lhs);
		if (base && DECL_P (base) && DECL_ASSEMBLER_NAME (base)
		    && !strcmp (IDENTIFIER_POINTER
				  (DECL_ASSEMBLER_NAME (base)),
				"__instrn_buffer"))
		  val = gimple_assign_rhs1 (stmt);
	      }
	  }
	if (!val)
	  return true;
	uint32_t base_word;
	bool exact = false;
	uint32_t word = 0;
	if (TREE_CODE (val) == INTEGER_CST)
	  {
	    word = (uint32_t) (TREE_INT_CST_LOW (val) & 0xffffffff);
	    base_word = word;
	    exact = true;
	  }
	else if (pushed_word_base (val, &base_word))
	  word = base_word;
	else
	  return true;		/* not a pinned word; other faces cover */
	if ((base_word >> 24) != c->setc16_opcode)
	  return true;
	unsigned reg, value;
	if (!rvtt_macro::decode_setc16 (c, base_word, &reg, &value))
	  return true;
	for (unsigned i = 0; i != prog.n_setc16; ++i)
	  if (prog.setc16[i].reg == reg)
	    {
	      if (!exact || word != want[i])
		{
		  if (dump_file)
		    fprintf (dump_file, "init-hoist: value-equality: row %u"
			     " write %s (0x%08x vs 0x%08x)\n", reg,
			     exact ? "unequal" : "runtime-completed",
			     word, want[i]);
		  return false;	/* unequal or unprovable value */
		}
	      if (dom_bb
		  && dominated_by_p (CDI_DOMINATORS, loop->header, dom_bb))
		have_dom[i] = true;
	    }
  }
  return true;
}

/* The U-level call block through which committed-inline node N's
   statements execute: walk N's single-caller chain up to UCALLER; the
   final hop's call statement lives in UCALLER's body.  Null when the
   chain is not single-sited.  */

static basic_block
init_dom_call_bb (cgraph_node *ucaller, cgraph_node *n)
{
  gcall *stmt = nullptr;
  unsigned depth = 0;
  while (n != ucaller)
    {
      if (++depth > 8)
	return nullptr;
      cgraph_edge *e = n->callers;
      if (!e || e->next_caller)
	return nullptr;
      stmt = e->call_stmt;
      n = e->caller;
    }
  return stmt ? gimple_bb (stmt) : nullptr;
}

/* The stage-2 value-equality proof for SUBJECT's contract hoisted
   into UCALLER/CALLER_FN's LOOP: every decodable SETC16-class
   delivery to an owned row of PROG -- in the caller body, in every
   committed-inline body, and among the audited MOP template words --
   must equal the contract's encoded word (C gives the encoding), and
   each owned row needs a write whose block dominates the loop
   header.  A slot-census failure is excusable only when attributable
   solely to SUBJECT itself.  Returns false to demote to stage 1.  */

static bool
init_value_equal_p (cgraph_node *ucaller, function *caller_fn,
		    class loop *loop,
		    const rvtt_init_hoist_program &prog,
		    const rvtt_macro::caps *c, cgraph_node *subject)
{
  /* The owned-row scan below reads TU_FACTS.SLOT_WORDS; a census that
     failed closed leaves it empty or truncated, and the equality would
     pass VACUOUSLY.  mop_init_ok_p guards this only when a MOP word is
     delivered inside the scanned epoch, while template words execute
     at their MOP sites before and between calls -- guard here too.
     One census verdict is excusable: body-unavailability attributable
     SOLELY to the contract subject itself (the callee being planned is
     past gimple at planner time by construction; its delivered words
     are exactly the prog its own planner audits -- the established
     discipline).  Everything else fails closed: the caller demotes to
     stage 1.  */
  if (tu_facts.slots_unproven)
    {
      bool excusable = !tu_facts.slot_refusal_non_body
	&& subject && tu_facts.unavailable_bodies;
      if (excusable)
	for (hash_set<cgraph_node *>::iterator it
	       = tu_facts.unavailable_bodies->begin ();
	     it != tu_facts.unavailable_bodies->end (); ++it)
	  {
	    cgraph_node *u = *it;
	    while (u && u != subject)
	      u = u->clone_of;
	    if (u != subject)
	      {
		excusable = false;
		break;
	      }
	  }
      if (!excusable)
	{
	  if (dump_file)
	    fprintf (dump_file, "init-hoist: value-equality: mop slot "
		     "census unproven (%s) -- owned-row equality "
		     "unprovable\n",
		     tu_facts.slot_reason ? tu_facts.slot_reason : "?");
	  return false;
	}
    }

  uint32_t want[8];
  bool have_dom[8] = {};
  for (unsigned i = 0; i != prog.n_setc16; ++i)
    if (!rvtt_macro::encode_setc16 (c, prog.setc16[i].reg,
				    prog.setc16[i].value, &want[i]))
      return false;

  basic_block bb;
  FOR_EACH_BB_FN (bb, caller_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (!init_value_equal_stmt (gsi_stmt (gsi), bb, loop, prog, c,
				  want, have_dom))
	return false;

  /* Committed-inline bodies (inlined_to == UCALLER): their statements
     execute at their U-level call sites -- the production hw-configure
     init lives here until the inline transform materializes it.  The
     body resolves through the clone_of origin; the write's
     dominance is its U-level call block's.  */
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (node->inlined_to != ucaller)
	continue;
      cgraph_node *body_node = node;
      while (body_node && !gimple_has_body_p (body_node->decl)
	     && body_node->clone_of)
	body_node = body_node->clone_of;
      function *nfn = body_node
	? DECL_STRUCT_FUNCTION (body_node->decl) : nullptr;
      if (!nfn || !nfn->cfg)
	return false;		/* unscannable committed-inline body */
      basic_block dom_bb = init_dom_call_bb (ucaller, node);
      basic_block nbb;
      FOR_EACH_BB_FN (nbb, nfn)
	for (gimple_stmt_iterator gsi = gsi_start_bb (nbb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  if (!init_value_equal_stmt (gsi_stmt (gsi), dom_bb, loop, prog, c,
				      want, have_dom))
	    return false;
    }

  /* MOP template words touching owned rows must be value-equal too
     (they execute at their MOP sites, before and between calls).  */
  for (uint32_t w : tu_facts.slot_words)
    if ((w >> 24) == c->setc16_opcode)
      {
	unsigned reg, value;
	if (!rvtt_macro::decode_setc16 (c, w, &reg, &value))
	  return false;
	for (unsigned i = 0; i != prog.n_setc16; ++i)
	  if (prog.setc16[i].reg == reg && w != want[i])
	    return false;
      }

  for (unsigned i = 0; i != prog.n_setc16; ++i)
    if (!have_dom[i])
      {
	if (dump_file)
	  fprintf (dump_file, "init-hoist: value-equality: row %u has no"
		   " dominating reaching write\n", prog.setc16[i].reg);
	return false;		/* no dominating reaching write */
      }
  return true;
}

/* The MOP template audit on the init face.  */

static bool
mop_init_ok_p (const rvtt_init_hoist_program &prog,
	       const rvtt_macro::caps *c, bool *owned_dirty,
	       const char **why)
{
  if (tu_facts.slots_unproven)
    {
      *why = tu_facts.slot_reason;
      return false;
    }
  if (tu_facts.slot_replay)
    {
      *why = "drain-init-mop-slot-unproven";
      return false;
    }
  for (uint32_t w : tu_facts.slot_words)
    {
      init_word_verdict v = rvtt_word_init_class (w, prog, c);
      if (v.owned_row_write)
	*owned_dirty = true;
      if (v.is_mop || !v.ok)
	{
	  *why = "drain-init-mop-slot-unproven";
	  return false;
	}
    }
  return true;
}

/* Commit: insert the hoisted prefix as typed builtin calls at the tail
   of the caller loop's dedicated preheader (created on the entry edge
   when needed), in the callee's own emission order: enable, owned
   SETC16 program (stage 2), staged descriptor words.  The caller's
   inline transform has not run yet, so every inserted call carries a
   cgraph edge (the coefficient-hoist commit's discipline).  */

static void
init_commit_caller (cgraph_node *caller, edge entry,
		    const rvtt_init_hoist_program &prog, int stage)
{
  const rvtt_insn_data *encc_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpencc_all_lanes);
  const rvtt_insn_data *setc16_d
    = rvtt_get_insn_data (rvtt_insn_data::ttsetc16);
  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  tree vec_type = TREE_TYPE (TREE_TYPE (xloadi_d->decl));

  basic_block ph = rvtt_commit_hoist_preheader (entry);
  auto place = [&] (gimple *stmt)
    {
      insert_in_preheader (ph, stmt);
      caller->create_edge (cgraph_node::get_create
			     (gimple_call_fndecl (as_a <gcall *> (stmt))),
			   as_a <gcall *> (stmt), ph->count);
    };

  /* The canonical zero-argument all-lanes enable: expands to the
     architectural word the callee's proven enable was compared
     against, so both sides program the identical lane state.  */
  gcall *encc = gimple_build_call (encc_d->decl, 0);
  place (encc);
  if (stage >= 2)
    for (unsigned i = 0; i != prog.n_setc16; ++i)
      {
	gcall *sc = gimple_build_call
	  (setc16_d->decl, 2,
	   build_int_cst (unsigned_type_node, prog.setc16[i].reg),
	   build_int_cst (unsigned_type_node, prog.setc16[i].value));
	place (sc);
      }
  for (unsigned i = 0; i != prog.n_words; ++i)
    {
      gcall *load = gimple_build_call
	(xloadi_d->decl, 5, null_pointer_node,
	 build_int_cst (unsigned_type_node, prog.words[i].word),
	 build_int_cst (unsigned_type_node, 0),
	 build_int_cst (unsigned_type_node, 0),
	 build_int_cst (integer_type_node, -32));
      tree staged = make_ssa_name (vec_type);
      gimple_call_set_lhs (load, staged);
      gcall *wrcfg = gimple_build_call
	(wrcfg_d->decl, 2, staged,
	 build_int_cst (unsigned_type_node, prog.words[i].dest));
      place (load);
      place (wrcfg);
    }
  update_ssa (TODO_update_ssa_only_virtuals);
  if (dump_file)
    fprintf (dump_file,
	     "init-hoist: placed stage-%d init contract (%u setc16, %u"
	     " descriptor words) in %s preheader bb %d\n",
	     stage, stage >= 2 ? prog.n_setc16 : 0, prog.n_words,
	     caller->dump_name (), ph->index);
}

} /* anonymous namespace (init hoist) */

/* The ONE caller-chain resolver behind the init-face
   contracts (the init hoist and the ADDR_MOD hoist) -- previously
   two byte-similar copies.  Resolve the effective caller chain
   F <- W1 <- ... <- U: each intermediate must be the target of exactly
   one call edge, not address-taken, and COMMITTED into its inliner
   (inlined_to) -- so at execution time its statements run inline at
   the call site, between the loop's trips -- and its body (through the
   clone_of origin chain: origin body = sound
   over-approximation) is scanned as part of the loop epoch.  U is the
   outermost node still carrying its own gimple CFG; the hoist lands in
   U's loop preheader.  REQUIRE_GIMPLE_BODY adds the ADDR_MOD face's
   commit-target check (typed builtin calls must still be insertable).
   Ends with the TU census rooting check on U (the coefficient hoist's
   crosscall-caller-unrooted discipline).  Returns null on success with
   CHAIN/UCALLER/UCALL/CFN filled; otherwise FACE_UNAVAILABLE after the
   face's historical "TAG: closure (<what>)" dump line.  */

static const char *
resolve_contract_chain (function *callee_fn,
			auto_vec<cgraph_node *, 4> *chain,
			cgraph_node **ucaller_out, gcall **ucall_out,
			function **cfn_out, bool require_gimple_body,
			const char *face_unavailable, const char *tag)
{
  cgraph_node *cn = cgraph_node::get (callee_fn->decl);
  auto closure_why = [&] (const char *what) -> const char *
    {
      if (dump_file)
	fprintf (dump_file, "%s: closure (%s)\n", tag, what);
      return face_unavailable;
    };
  if (!cn)
    return closure_why ("no-node");
  if (!cn->definition)
    return closure_why ("no-definition");
  if (cn->address_taken)
    return closure_why ("address-taken");
  if (cn->alias || cn->thunk)
    return closure_why ("alias-or-thunk");
  if (cn->clones)
    return closure_why ("clones");

  cgraph_node *cur = cn;
  cgraph_node *ucaller = nullptr;
  gcall *ucall = nullptr;
  for (unsigned depth = 0; !ucaller; ++depth)
    {
      if (depth > 3)
	return closure_why ("chain-too-deep");
      cgraph_edge *e = cur->callers;
      if (!e)
	return closure_why ("no-callers");
      if (e->next_caller)
	return closure_why ("multi-site");
      if (e->caller == cn)
	return closure_why ("recursion");
      cgraph_node *caller = e->caller;
      if (!caller->definition)
	return closure_why ("caller-body-unavailable");
      function *this_fn = DECL_STRUCT_FUNCTION (caller->decl);
      if (this_fn && this_fn->cfg && !caller->inlined_to)
	{
	  ucaller = caller;
	  ucall = e->call_stmt;
	  if (!ucall)
	    return closure_why ("caller-body-unavailable");
	  break;
	}
      /* Intermediate hop: must be committed inline (its statements
	 execute at the call site) and resolvable to a scannable
	 body.  */
      if (!caller->inlined_to || caller->address_taken || caller->alias
	  || caller->thunk)
	return closure_why ("chain-unproven");
      chain->safe_push (caller);
      cur = caller;
    }
  /* Find U's edge into the chain (its call_stmt is the loop member).  */
  {
    cgraph_node *first_hop = chain->is_empty () ? cn : chain->last ();
    cgraph_edge *found = nullptr;
    for (cgraph_edge *e = ucaller->callees; e; e = e->next_callee)
      if (e->callee == first_hop)
	{
	  if (found)
	    return closure_why ("multi-site");
	  found = e;
	}
    if (!found || !found->call_stmt)
      return closure_why ("caller-body-unavailable");
    ucall = found->call_stmt;
  }
  /* The caller must still be gimple when the face inserts typed
     builtin calls into its body (an already-expanded caller has no
     insertable gimple -- fail closed).  */
  if (require_gimple_body && !ucaller->has_gimple_body_p ())
    return closure_why ("caller-past-gimple");
  function *cfn = DECL_STRUCT_FUNCTION (ucaller->decl);
  if (!cfn || !cfn->cfg)
    return closure_why ("caller-cfg-unavailable");

  /* TU facts (MOP template census) while every body that still has
     gimple is scannable; already-expanded bodies are outside the
     census by the established discipline (their delivered words were
     scanned when they were the contract subject).  */
  compute_tu_facts ();

  /* The commit target must be a body the census vouched for.  */
  {
    cgraph_node *ucheck = ucaller->inlined_to
      ? ucaller->inlined_to : ucaller;
    if (tu_facts.executable && !tu_facts.executable->contains (ucheck))
      return closure_why ("caller-unrooted");
  }

  *ucaller_out = ucaller;
  *ucall_out = ucall;
  *cfn_out = cfn;
  return nullptr;
}

/* See rvtt-protos.h.  Returns the refusal name, or NULL after a
   committed caller-side insertion with PROG->stage set.  COMMIT false
   (the pricing pre-run) evaluates the identical proof chain and
   sets every out field -- stage and the caller-loop trip weight --
   without inserting anything.  */

const char *
rvtt_crosscall_init_hoist (function *callee_fn,
			   rvtt_init_hoist_program *prog, bool commit)
{
  prog->caller_weight_ok = false;
  prog->caller_entry_count = 1;
  prog->caller_body_count = 1;
  if (TARGET_XTT_TENSIX_QSR)
    return "drain-init-callers-unproven";
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : rvtt_macro::CPU_WH;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c)
    return "drain-init-callers-unproven";

  /* The one caller-chain resolver; dump lines and refusal
     verdicts are the historical ones.  */
  auto_vec<cgraph_node *, 4> chain;	/* intermediates, innermost first */
  cgraph_node *ucaller = nullptr;
  gcall *ucall = nullptr;
  function *cfn = nullptr;
  if (const char *chain_why
      = resolve_contract_chain (callee_fn, &chain, &ucaller, &ucall, &cfn,
				/*require_gimple_body=*/false,
				"drain-init-callers-unproven", "init-hoist"))
    return chain_why;
  cgraph_node *cn = cgraph_node::get (callee_fn->decl);

  const char *result = nullptr;
  int stage = 2;
  push_cfun (cfn);
  loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
  bool dom = dom_info_available_p (CDI_DOMINATORS);
  if (!dom)
    calculate_dominance_info (CDI_DOMINATORS);

  basic_block call_bb = gimple_bb (ucall);
  class loop *loop = call_bb ? call_bb->loop_father : nullptr;
  edge entry = nullptr;
  if (!loop || !loop_outer (loop))
    result = "drain-init-loop-unproven";
  else
    {
      entry = rvtt_loop_entry_edge (loop);
      if (!entry || rvtt_preheader_insertion_blocked_p (entry))
	result = "drain-init-loop-unproven";
    }

  /* Admitted call targets along the chain: every hop's decl and its
     clone-origin decl (U's loop calls the outermost hop; each hop's
     body calls the next).  */
  hash_set<tree> chain_decls;
  chain_decls.add (callee_fn->decl);
  for (cgraph_node *hop : chain)
    {
      chain_decls.add (hop->decl);
      for (cgraph_node *o = hop; o; o = o->clone_of)
	chain_decls.add (o->decl);
    }

  init_scan_ctx ctx;
  ctx.prog = prog;
  ctx.c = c;
  ctx.callee_decl = callee_fn->decl;
  ctx.chain_decls = &chain_decls;
  if (!result)
    {
      basic_block *body = get_loop_body (loop);
      for (unsigned ix = 0; !ctx.why && ix != loop->num_nodes; ++ix)
	{
	  for (gphi_iterator psi = gsi_start_phis (body[ix]);
	       !gsi_end_p (psi); gsi_next (&psi))
	    if (vector_typed_p (gimple_phi_result (psi.phi ()))
		&& !virtual_operand_p (gimple_phi_result (psi.phi ())))
	      {
		init_refuse (&ctx, "drain-init-vector-live", psi.phi ());
		break;
	      }
	  for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	       !ctx.why && !gsi_end_p (gsi); gsi_next (&gsi))
	    init_scan_stmt (&ctx, gsi_stmt (gsi));
	}
      free (body);
      if (ctx.why)
	result = ctx.why;
    }

  /* The chain hops' statements execute per trip, between the loop's
     calls: the same epoch discipline over every hop body (through the
     clone_of origin when the clone carries no materialized body --
     statement classification is parameter-independent, so the origin
     over-approximates soundly), IPA-summary-fed: each hop
     body's digest is computed once per TU and replayed here.  */
  if (!result)
    result = scan_chain_hops (&ctx, chain, "drain-init-callers-unproven",
			      "init-hoist");

  if (!result && ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_init_ok_p (*prog, c, &ctx.owned_row_dirty, &why))
	result = why;
    }

  if (!result)
    {
      /* Stage decision.  An in-loop owned-row write means the caller
	 re-programs an owned row between calls: with the callee's
	 per-call SETC16 program retained (stage 1) that is exactly
	 today's interleaving; hoisting it (stage 2) would change what
	 the next call's launches address -- demote.  */
      bool value_equal = !ctx.cc_dirty && !ctx.owned_row_dirty
	&& init_value_equal_p (ucaller, cfn, loop, *prog, c, cn);
      if (!value_equal)
	{
	  stage = 1;
	  if (dump_file)
	    fprintf (dump_file, "init-hoist: stage-2 demoted (%s)\n",
		     ctx.cc_dirty ? "loop-cc-write"
		     : ctx.owned_row_dirty ? "loop-owned-row-write"
		     : "value-equality-unproven");
	}
      /* Caller-loop trip weight (init-hoist-aware run
	 pricing): the proven loop's profile entry/body execution-count
	 fraction, filled for BOTH the proof-only and the committing
	 call under the planner's loop_trip_weight discipline (exact
	 where the profile is, the static estimate elsewhere; no usable
	 estimate leaves caller_weight_ok false and the frozen pricing
	 in force).  Purely a profitability weight.  */
      {
	basic_block body_bb = gimple_bb (ucall);
	profile_count bc = body_bb->count, pc = entry->count ();
	if (bc.initialized_p () && pc.initialized_p () && pc.nonzero_p ())
	  {
	    int64_t b = bc.to_gcov_type (), p = pc.to_gcov_type ();
	    if (p > 0 && b >= p)
	      {
		/* The one 48-bit scaling spelling (shared with the
		   planner's loop_trip_weight;
		   rvtt-delivery-cost-core.h).  */
		int64_t sb = b, sp = p;
		rvtt_delivery_cost::scale_trip_weight (&sb, &sp);
		prog->caller_weight_ok = true;
		prog->caller_entry_count = sp;
		prog->caller_body_count = sb;
	      }
	  }
      }
      if (commit)
	{
	  init_commit_caller (ucaller, entry, *prog, stage);
	  /* Item #15: the caller's body just mutated from outside its
	     own pipeline -- any cached summary of it is void.  */
	  rvtt_ipa_summary_invalidate (cfn);
	}
      prog->stage = stage;
    }

  if (!dom)
    free_dominance_info (CDI_DOMINATORS);
  loop_optimizer_finalize ();
  pop_cfun ();
  return result;
}

/* ==================================================================
   Cross-call ADDR_MOD contract (Dst auto-increment service).

   A straight-line callee whose Dst auto-increment groups refuse solely
   by the per-execution configuration pricing (each SETC16
   occupies the audited two-cycle configuration issue class plus the
   once-per-entry drain residual on EVERY call) re-programs its owned
   address-modifier slot on every invocation, although the program is
   the same compile-time constant triple on every call.  A hand kernel
   programs its ADDR_MOD slots ONCE at kernel init.  This service --
   called from the callee's Dst auto-increment pass
   (rtl-rvtt-dst-autoincr.cc) while every caller body is still gimple
   (the pass-pipeline ordering fact) -- proves the caller side and, on a
   complete proof, inserts the slot program as typed ttsetc16 builtin
   calls in the caller's loop-entry preheader, LIFTED across enclosing
   caller loops by the residency walk (a failing
   level stops the walk, never refuses).  The callee's groups then fire
   with the program omitted entirely.

   Soundness is the ISA-documented slot-clobber census
   (tt-isa-documentation: ThreadConfig ADDR_MOD rows are per-thread and
   writable ONLY by same-thread SETC16 -- WRCFG/CFGSHIFTMASK/RMWCIB
   cannot write ThreadConfig), instantiated as:
     - the caller epoch at the call's loop and at every lifted level is
       scanned with the init-face discipline (init_scan_stmt): every
       statement or delivered word that could write a contract row --
       typed ttsetc16/config calls, replay content, unaudited words or
       calls -- refuses by name, and vector dataflow refuses (a later
       formation in the caller could own the very slot this contract
       programs);
     - a SETC16-class delivered word to an owned row (or a watched row:
       the Wormhole ADDR_MOD_SET_Base bank-select register, whose flip
       would re-alias the scratch modifier to the base-0 bank) REFUSES
       -- there is no demotion stage, because the callee will not
       re-emit the program per call;
     - MOP template slots are audited TU-wide (mop_init_ok_p);
     - the target preheader must not already carry another compiler
       contract's typed programming (two contracts committing into one
       preheader have no defined order against each other's consumers);
     - CC state is immaterial: SETC16 is not lane-predicated.

   The callee-side conditions (whole-callee ownership census, entry
   distance guard, call-boundary crossing charge, all-groups/single-
   stride/explicit-rows shape) live with the pricing in
   rtl-rvtt-dst-autoincr.cc.

   Refusal vocabulary (stable, append-only; the scan reasons are the
   init face's own drain-init-* names -- it is the same audited scan):
     crosscall-addrmod-callers-unproven   closure (alias/clone/address-
					  taken/multi-site/expanded/
					  caller past gimple)
     crosscall-addrmod-loop-unproven      no natural loop / no provable
					  entry edge
     crosscall-addrmod-owned-row-write    a SETC16-class delivery to an
					  owned or watched row in the
					  scanned epoch
     crosscall-addrmod-preheader-occupied another compiler contract's
					  typed programming already sits
					  in the target preheader  */

const char *
rvtt_crosscall_addrmod_hoist (function *callee_fn,
			      rvtt_addrmod_hoist_program *prog)
{
  prog->lift_levels = 0;
  if (TARGET_XTT_TENSIX_QSR)
    return "crosscall-addrmod-callers-unproven";
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : rvtt_macro::CPU_WH;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c || !prog->n_setc16)
    return "crosscall-addrmod-callers-unproven";

  /* The scan program: the contract rows plus the refuse-only watched
     rows (classification compares registers only; a watched-row write
     sets owned_row_dirty, which refuses below exactly like an
     owned-row write).  */
  rvtt_init_hoist_program iprog;
  memset (&iprog, 0, sizeof (iprog));
  for (unsigned i = 0; i != prog->n_setc16; ++i)
    {
      iprog.setc16[iprog.n_setc16].reg = prog->setc16[i].reg;
      iprog.setc16[iprog.n_setc16++].value = prog->setc16[i].value;
    }
  for (unsigned i = 0; i != prog->n_watch; ++i)
    {
      iprog.setc16[iprog.n_setc16].reg = prog->watch[i];
      iprog.setc16[iprog.n_setc16++].value = 0;
    }

  /* The one caller-chain resolver (each intermediate committed
     inline, single-sited; U = the outermost node still carrying
     gimple).  Dump lines and refusal
     verdicts are the historical ones.  */
  auto_vec<cgraph_node *, 4> chain;
  cgraph_node *ucaller = nullptr;
  gcall *ucall = nullptr;
  function *cfn = nullptr;
  if (const char *chain_why
      = resolve_contract_chain (callee_fn, &chain, &ucaller, &ucall, &cfn,
				/*require_gimple_body=*/true,
				"crosscall-addrmod-callers-unproven",
				"addrmod-hoist"))
    return chain_why;

  const char *result = nullptr;
  push_cfun (cfn);
  loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
  bool dom = dom_info_available_p (CDI_DOMINATORS);
  if (!dom)
    calculate_dominance_info (CDI_DOMINATORS);

  basic_block call_bb = gimple_bb (ucall);
  class loop *loop = call_bb ? call_bb->loop_father : nullptr;
  edge entry = nullptr;
  if (!loop || !loop_outer (loop))
    result = "crosscall-addrmod-loop-unproven";
  else
    {
      entry = rvtt_loop_entry_edge (loop);
      if (!entry || rvtt_preheader_insertion_blocked_p (entry))
	result = "crosscall-addrmod-loop-unproven";
    }

  hash_set<tree> chain_decls;
  chain_decls.add (callee_fn->decl);
  for (cgraph_node *hop : chain)
    {
      chain_decls.add (hop->decl);
      for (cgraph_node *o = hop; o; o = o->clone_of)
	chain_decls.add (o->decl);
    }

  init_scan_ctx ctx;
  ctx.prog = &iprog;
  ctx.c = c;
  ctx.callee_decl = callee_fn->decl;
  ctx.chain_decls = &chain_decls;
  ctx.contract_call = ucall;
  if (!result)
    {
      basic_block *body = get_loop_body (loop);
      for (unsigned ix = 0; !ctx.why && ix != loop->num_nodes; ++ix)
	{
	  for (gphi_iterator psi = gsi_start_phis (body[ix]);
	       !gsi_end_p (psi); gsi_next (&psi))
	    if (vector_typed_p (gimple_phi_result (psi.phi ()))
		&& !virtual_operand_p (gimple_phi_result (psi.phi ())))
	      {
		init_refuse (&ctx, "drain-init-vector-live", psi.phi ());
		break;
	      }
	  for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	       !ctx.why && !gsi_end_p (gsi); gsi_next (&gsi))
	    init_scan_stmt (&ctx, gsi_stmt (gsi));
	}
      free (body);
      if (ctx.why)
	result = ctx.why;
    }

  /* Chain hops' statements execute per trip, between calls: same epoch
     discipline over every hop body, IPA-summary-fed: one digest
     per hop body, computed once per TU and replayed here.  */
  if (!result)
    result = scan_chain_hops (&ctx, chain,
			      "crosscall-addrmod-callers-unproven",
			      "addrmod-hoist");

  /* Placement residency walk (the config-prefix discipline): lift the program
     point across enclosing caller loops whose EXTRA bodies pass the
     same epoch scan.  A failing level stops the walk -- the inner
     placement stands, nothing refuses -- and a rejected level's
     accumulated facts (saw_mop, owned/cc dirt, refusal) are restored so
     they cannot constrain the committed placement.  */
  class loop *place_loop = loop;
  if (!result)
    for (class loop *outer = loop_outer (loop); outer && outer->num;
	 outer = loop_outer (outer))
      {
	edge oentry = rvtt_loop_entry_edge (outer);
	if (!oentry || rvtt_preheader_insertion_blocked_p (oentry))
	  break;
	bool saved_mop = ctx.saw_mop;
	bool saved_cc = ctx.cc_dirty;
	bool saved_owned = ctx.owned_row_dirty;
	bool level_ok = true;
	basic_block *obody = get_loop_body (outer);
	for (unsigned ix = 0; level_ok && ix != outer->num_nodes; ++ix)
	  {
	    if (flow_bb_inside_loop_p (place_loop, obody[ix]))
	      continue;		/* already proven at the level below */
	    for (gphi_iterator psi = gsi_start_phis (obody[ix]);
		 level_ok && !gsi_end_p (psi); gsi_next (&psi))
	      if (vector_typed_p (gimple_phi_result (psi.phi ()))
		  && !virtual_operand_p (gimple_phi_result (psi.phi ())))
		level_ok = false;
	    for (gimple_stmt_iterator gsi = gsi_start_bb (obody[ix]);
		 level_ok && !gsi_end_p (gsi); gsi_next (&gsi))
	      if (!init_scan_stmt (&ctx, gsi_stmt (gsi)))
		level_ok = false;
	  }
	free (obody);
	if (!level_ok)
	  {
	    if (dump_file)
	      fprintf (dump_file, "addrmod-hoist: residency walk stops at "
		       "loop bb %d (%s)\n", outer->header->index,
		       ctx.why ? ctx.why : "drain-init-vector-live");
	    ctx.saw_mop = saved_mop;
	    ctx.cc_dirty = saved_cc;
	    ctx.owned_row_dirty = saved_owned;
	    ctx.why = nullptr;
	    ctx.why_stmt = nullptr;
	    break;
	  }
	place_loop = outer;
	entry = oentry;
	++prog->lift_levels;
	if (dump_file)
	  fprintf (dump_file, "addrmod-hoist: placement lifted to enclosing "
		   "loop bb %d entry\n", outer->header->index);
      }

  if (!result && ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_init_ok_p (iprog, c, &ctx.owned_row_dirty, &why))
	result = why;
    }

  /* No demotion stage: any possible owned-row (or watched-row) write in
     the scanned epoch refuses -- the callee will not re-establish the
     program per call.  (CC dirt is immaterial: SETC16 carries no lane
     predication.)  */
  if (!result && ctx.owned_row_dirty)
    result = "crosscall-addrmod-owned-row-write";

  /* The target preheader must not already carry another compiler
     contract's typed programming: two contracts committing into one
     preheader have no defined order against each other's consumers.  */
  if (!result)
    {
      basic_block ph = entry->src;
      for (gimple_stmt_iterator gsi = gsi_start_bb (ph); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	  const rvtt_insn_data *insnd
	    = call ? rvtt_get_insn_data (call) : nullptr;
	  if (insnd
	      && (insnd->id == rvtt_insn_data::ttsetc16
		  || insnd->id == rvtt_insn_data::sfpencc_all_lanes
		  || insnd->id == rvtt_insn_data::sfpwriteconfig_v
		  || insnd->id == rvtt_insn_data::sfpconfig_i))
	    {
	      result = "crosscall-addrmod-preheader-occupied";
	      break;
	    }
	}
    }

  if (!result)
    {
      const rvtt_insn_data *setc16_d
	= rvtt_get_insn_data (rvtt_insn_data::ttsetc16);
      basic_block ph = rvtt_commit_hoist_preheader (entry);
      for (unsigned i = 0; i != prog->n_setc16; ++i)
	{
	  gcall *sc = gimple_build_call
	    (setc16_d->decl, 2,
	     build_int_cst (unsigned_type_node, prog->setc16[i].reg),
	     build_int_cst (unsigned_type_node, prog->setc16[i].value));
	  insert_in_preheader (ph, sc);
	  ucaller->create_edge (cgraph_node::get_create
				  (gimple_call_fndecl (sc)),
				sc, ph->count);
	}
      update_ssa (TODO_update_ssa_only_virtuals);
      /* Item #15: the caller's body just mutated from outside its own
	 pipeline -- any cached summary of it is void.  */
      rvtt_ipa_summary_invalidate (cfn);
      if (dump_file)
	fprintf (dump_file,
		 "addrmod-hoist: placed ADDR_MOD contract (%u setc16) in %s "
		 "preheader bb %d (lifted %u levels)\n",
		 prog->n_setc16, ucaller->dump_name (), ph->index,
		 prog->lift_levels);
    }

  if (!dom)
    free_dominance_info (CDI_DOMINATORS);
  loop_optimizer_finalize ();
  pop_cfun ();
  return result;
}
