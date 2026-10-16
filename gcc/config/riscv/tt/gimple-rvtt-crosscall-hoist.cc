/* Cross-call constant delivery: the init and ADDR_MOD hoist services
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Split out of the cross-call delivery pass originated by Paul Keller
   (pkeller@tenstorrent.com) and rewritten by Nathan Sidwell
   (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* The cross-call hoist services: rvtt_crosscall_init_hoist (called by
   the macro planner) and rvtt_crosscall_addrmod_hoist (called by the Dst
   auto-increment pass).  Each replays a digest of the words delivered
   along the caller chain, proves that the caller cannot observe the
   difference, and inserts the hoisted programming in the caller's loop
   preheader.  */

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
#include "rvtt-effects.h"
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
   Cross-call invariant-init hoist (macro-planner service).

   A noinline per-tile callee whose macro formation emits an idempotent
   init prefix -- the derived descriptor program (staged SFPLOADI +
   SFPCONFIG writes), the owned SETC16 address-modifier program and the
   all-lanes enable -- re-executes that prefix on every call, although
   every written value is compile-time descriptor data.  This service is
   called from the callee's macro-planner formation
   (rtl-rvtt-macro-planner.cc) while every caller body is still GIMPLE
   (callees run the late pipeline before their callers).  It proves the
   caller side and, on success, inserts the prefix as typed builtin calls
   in the caller's loop preheader; the planner then omits the hoisted
   part from the callee.  Both sides commit together or not at all; any
   unproven condition refuses by name and the per-call prefix stays.

   Two stages, by proof strength:

     stage 1 (descriptor words): the SFPCONFIG destinations are read only
       by SFPLOADMACRO launches (SFPLOADMACRO.md: the launch resolves
       templates/sequence/misc from LoadMacroConfig), and the callee holds
       the function's only launches, so the caller loop need only contain
       no LoadMacroConfig writer and no launch.  The callee keeps its
       per-call enable and SETC16 program.  The hoisted block carries its
       own all-lanes enable for the lane-predicated staging loads.

     stage 2 (full prefix): additionally the owned SETC16 rows and the
       lane state.  Sound only when the caller's reaching configuration
       is value-equal: every decodable SETC16-class delivery to an owned
       row anywhere in the caller equals the contract's encoded word, and
       each owned row has such a write dominating the loop, so nothing
       between the preheader and the first call can distinguish the
       hoisted programming from the per-call state.  A loop without CC
       writes keeps the hoisted enable's state across every trip.

   Refusals:
     drain-init-callers-unproven   closure (alias/clone/address-taken/
				   multi-caller/multi-site/expanded)
     drain-init-loop-unproven      no natural loop / no provable entry
     drain-init-ownership-unproven a loop statement or delivered word
				   that could write the hoisted state,
				   launch a macro, or replay recorded
				   content
     drain-init-vector-live	   vector dataflow in the loop (a later
				   formation could own the state)
     drain-init-mop-slot-unproven  the TU template scan cannot prove
				   the MOP words init-inert
   Value inequality and loop CC writes are not refusals: they demote to
   stage 1, which keeps the enable and SETC16 program per call.  */

namespace {

/* Classification of a delivered word for the init hoist: can it write
   LoadMacroConfig (SFPCONFIG class), launch a macro, or replay recorded
   content?  rvtt_word_init_class answers from the shared word-fact table
   (rvtt_word_facts_classify in rvtt-raw-boundary.cc), refusing any class
   not in the table, and adds the SETC16/SFPCONFIG opcode checks and the
   owned-row tracking used by stage 2.  */

typedef rvtt_wf_init_verdict init_word_verdict;

/* Classify the delivered word VAL against contract program PROG under
   target capabilities C.  A constant resolves exactly; a runtime-completed
   composition keeps its opcode class but clears WORD_EXACT (stage 2
   cannot use it); an underivable word refuses.  */

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
  /* A runtime-completed word keeps its opcode and non-value fields,
     but its exact value is not a constant.  */
  v.word_exact = false;
  return v;
}

struct init_scan_ctx
{
  const rvtt_init_hoist_program *prog;
  const rvtt_macro::caps *c;
  tree callee_decl;		/* admitted contract-call target      */
  hash_set<tree> *chain_decls = nullptr; /* admitted chain-call targets */
  /* The resolved contract call statement, admitted by identity: a
     constprop/IPA clone's call statement can still name the origin
     decl while the cgraph edge targets the clone, so comparing decls
     alone would refuse the contract call itself.  */
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

/* Classify asm statement STMT of the caller's loop into CTX.  */

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

/* Classify statement STMT of the caller's loop body into CTX.  Return
   false if it refuses.  */

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
	      /* The FPU face-transpose family programs Dst rows, Src
		 banks and backend configuration -- state this contract
		 neither owns nor orders against.  */
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
	  if (rvtt_instrn_buffer_name_p (name))
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
/* The init-hoist body digest (rvtt-ipa-summary).

   Each chain hop's body is classified once, as init_scan_stmt would
   classify it, into a parameter-independent event stream;
   init_replay_events folds that stream against a contract's parameters
   (owned rows via rvtt_word_init_class on the recorded word, call
   admission via the contract's decl set) with the same accumulator and
   refusals as the direct walk.  This avoids re-walking every hop body
   once per contract.  The digest functions below must classify their
   input exactly as init_scan_stmt, init_scan_asm and
   classify_delivered_init do.  */

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

/* Record the delivered word VAL from STMT in OUT: its constant image, or
   the constant opcode base of a runtime-completed word with WORD_EXACT
   false.  An underivable word records a refusal.  */

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

/* Record STMT's events in OUT.  Statements that init_scan_stmt admits
   independently of the contract record nothing.  */

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
      /* Untyped calls admitted independently of the contract: internal
	 calls without a vdef and scalar compiler builtins.  Neither can
	 be the contract call or a chain decl, so classifying them before
	 the admission checks does not change the result.  */
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
	  if (rvtt_instrn_buffer_name_p (name))
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

/* Replay a body digest against this contract's parameters, stopping at
   the first refusal; dumps and refusal counters fire as in
   init_refuse.  */

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

/* Scan the bodies of the chain hops in CHAIN through their cached
   digests.  FACE_UNAVAILABLE is the caller's closure refusal and TAG its
   dump prefix.  Returns the refusal name, or null with CTX's
   accumulators advanced.  */

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
	  /* The digest engine could not see a body this face just probed
	     as walkable: refuse.  */
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
	  /* This dump line is deliberately face-neutral: testsuite
	     scan-dump-not patterns match the bare face tags.  */
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
     ADDR_MOD hoist plants them in caller bodies, so this check is
     gated by its flag.  An owned-row write with a non-constant operand
     or an unequal value demotes like a delivered word.  */
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
		    && rvtt_instrn_buffer_name_p
			 (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (base))))
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
   header.  A slot-scan failure is excusable only when attributable
   solely to SUBJECT itself.  Returns false to demote to stage 1.  */

static bool
init_value_equal_p (cgraph_node *ucaller, function *caller_fn,
		    class loop *loop,
		    const rvtt_init_hoist_program &prog,
		    const rvtt_macro::caps *c, cgraph_node *subject)
{
  /* The owned-row scan below reads TU_FACTS.SLOT_WORDS; a failed scan
     leaves it empty or truncated and the equality would pass vacuously.
     mop_init_ok_p only guards this when a MOP word is delivered inside
     the scanned epoch, while template words execute at their MOP sites
     before and between calls, so check here too.  The one excusable
     failure is a body being unavailable solely because it is the
     contract subject itself: the callee being planned is past GIMPLE by
     construction, and its delivered words are exactly the program its
     own planner audits.  Anything else demotes to stage 1.  */
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
		     "scan unproven (%s) -- owned-row equality "
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
     execute at their U-level call sites.  The body resolves through
     the clone_of origin; the write dominates through its U-level call
     block.  */
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

enum init_lreg_event_kind
{
  INIT_LREG_NONE,
  INIT_LREG_EFFECT,
  INIT_LREG_ACCESS,
  INIT_LREG_TYPED_READ,
  INIT_LREG_TYPED_WRITE
};

struct init_lreg_event
{
  init_lreg_event_kind kind = INIT_LREG_NONE;
  unsigned first = 0;
  unsigned writes = 0;
};

/* Classify the GIMPLE forms that rtl-rvtt-lreg-livein later interprets.
   Return false rather than inventing a transfer when a metadata operand is
   not a compile-time mask/register: in particular, treating an unknown write
   as 0xff could unsoundly manufacture a demand kill.  */

static bool
init_lreg_event_for_stmt (gimple *stmt, init_lreg_event *ev)
{
  *ev = init_lreg_event {};
  gcall *call = dyn_cast <gcall *> (stmt);
  const rvtt_insn_data *insnd = call ? rvtt_get_insn_data (call) : nullptr;
  if (!insnd)
    return true;

  auto mask_arg = [&] (unsigned arg, unsigned *mask) -> bool
    {
      tree value = gimple_call_arg (call, arg);
      if (TREE_CODE (value) != INTEGER_CST)
	return false;
      *mask = TREE_INT_CST_LOW (value) & 0xff;
      return true;
    };
  auto lreg_arg = [&] (unsigned arg, unsigned *mask) -> bool
    {
      tree value = gimple_call_arg (call, arg);
      if (TREE_CODE (value) != INTEGER_CST || !tree_fits_uhwi_p (value))
	return false;
      unsigned HOST_WIDE_INT regno = tree_to_uhwi (value);
      if (regno >= 8)
	return false;
      *mask = 1u << regno;
      return true;
    };

  switch (insnd->id)
    {
    case rvtt_insn_data::sfprawlreg_effect:
      ev->kind = INIT_LREG_EFFECT;
      return mask_arg (0, &ev->first) && mask_arg (1, &ev->writes);
    case rvtt_insn_data::sfprawlreg_access:
      ev->kind = INIT_LREG_ACCESS;
      return mask_arg (0, &ev->first) && mask_arg (1, &ev->writes);
    case rvtt_insn_data::sfpreadlreg:
      {
	tree value = gimple_call_arg (call, 0);
	if (TREE_CODE (value) != INTEGER_CST || !tree_fits_uhwi_p (value))
	  return false;
	unsigned HOST_WIDE_INT regno = tree_to_uhwi (value);
	/* Reads of programmable constant registers do not own L0..L7.  */
	if (regno >= 8 && regno <= 15)
	  return true;
	ev->kind = INIT_LREG_TYPED_READ;
	return lreg_arg (0, &ev->first);
      }
    case rvtt_insn_data::sfpwritelreg:
      ev->kind = INIT_LREG_TYPED_WRITE;
      return lreg_arg (1, &ev->writes);
    default:
      return true;
    }
}

enum init_lreg_transfer_kind
{
  INIT_LREG_OWNED,
  INIT_LREG_PROVENANCE,
  INIT_LREG_DEMAND,
  INIT_LREG_EFFECT_DEMAND
};

/* Return the one committed-inline edge represented by STMT in OWNER, or null
   when STMT is an ordinary non-inline call.  Multiple matching edges are an
   unprovable call-site mapping.  */

static cgraph_edge *
init_lreg_inline_edge (cgraph_node *owner, cgraph_node *ucaller, gimple *stmt,
		       bool *known)
{
  cgraph_edge *found = nullptr;
  for (cgraph_edge *e = owner->callees; e; e = e->next_callee)
    if (e->call_stmt == stmt && e->callee->inlined_to == ucaller)
      {
	if (found)
	  {
	    *known = false;
	    return nullptr;
	  }
	found = e;
      }
  return found;
}

static cgraph_node *
init_lreg_body_node (cgraph_node *node)
{
  /* The origin is only a source of statements, never a source of invented
     clone constants: init_lreg_event_for_stmt still requires every metadata
     mask/register in that body to be an INTEGER_CST.  */
  while (node && !gimple_has_body_p (node->decl) && node->clone_of)
    node = node->clone_of;
  return node;
}

/* Every committed edge that will later materialize into OWNER must already
   name a statement in FN's body.  Without that accounting, a failed pointer
   match below could silently turn an inline transfer into identity.  */

static bool
init_lreg_inline_edges_mapped_p (cgraph_node *owner, cgraph_node *ucaller,
				 function *fn)
{
  for (cgraph_edge *e = owner->callees; e; e = e->next_callee)
    if (e->callee->inlined_to == ucaller)
      {
	if (!e->call_stmt)
	  return false;
	bool found = false;
	basic_block bb;
	FOR_EACH_BB_FN (bb, fn)
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    if (gsi_stmt (gsi) == e->call_stmt)
	      found = true;
	if (!found)
	  return false;
      }
  return true;
}

/* Whether NODE's eventual inline expansion contains a relevant raw/hard-LREG
   event.  This recursive precheck lets an arbitrary raw-free CFG remain the
   identity transfer; only a relevant body needs the bounded linear-CFG
   summary below.  */

static bool
init_lreg_inline_has_events (cgraph_node *ucaller, cgraph_node *node,
			     bool *has_events, bool *known,
			     auto_vec<cgraph_node *, 8> *active)
{
  for (cgraph_node *seen : *active)
    if (seen == node)
      {
	*known = false;
	return false;
      }
  unsigned active_mark = active->length ();
  active->safe_push (node);
  cgraph_node *body_node = init_lreg_body_node (node);
  function *fn = body_node
    ? DECL_STRUCT_FUNCTION (body_node->decl) : nullptr;
  if (!fn || !fn->cfg
      || !init_lreg_inline_edges_mapped_p (node, ucaller, fn)
      || (body_node != node
	  && !init_lreg_inline_edges_mapped_p (body_node, ucaller, fn)))
    {
      *known = false;
      active->truncate (active_mark);
      return false;
    }
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	init_lreg_event ev;
	if (!init_lreg_event_for_stmt (stmt, &ev))
	  {
	    *known = false;
	    active->truncate (active_mark);
	    return false;
	  }
	if (ev.kind != INIT_LREG_NONE)
	  *has_events = true;
	bool edge_known = true;
	cgraph_edge *e
	  = init_lreg_inline_edge (node, ucaller, stmt, &edge_known);
	if (!e && body_node != node && edge_known)
	  e = init_lreg_inline_edge (body_node, ucaller, stmt, &edge_known);
	if (!edge_known)
	  {
	    *known = false;
	    active->truncate (active_mark);
	    return false;
	  }
	if (e && !init_lreg_inline_has_events (ucaller, e->callee, has_events,
					     known, active))
	  {
	    active->truncate (active_mark);
	    return false;
	  }
      }
  active->truncate (active_mark);
  return true;
}

/* Record a body whose executable CFG is one straight entry-to-exit chain.
   Relevant committed-inline bodies outside this bounded shape refuse rather
   than receiving a flat, order-losing summary.  */

static bool
init_lreg_linear_body (function *fn, auto_vec<basic_block> *order)
{
  if (!single_succ_p (ENTRY_BLOCK_PTR_FOR_FN (fn)))
    return false;
  basic_block bb = single_succ (ENTRY_BLOCK_PTR_FOR_FN (fn));
  while (bb != EXIT_BLOCK_PTR_FOR_FN (fn))
    {
      if (!single_pred_p (bb) || !single_succ_p (bb))
	return false;
      for (basic_block seen : *order)
	if (seen == bb)
	  return false;
      order->safe_push (bb);
      bb = single_succ (bb);
    }
  return true;
}

static bool init_lreg_apply_stmt (cgraph_node *, cgraph_node *, gimple *,
				  init_lreg_transfer_kind, unsigned *,
				  bool *, auto_vec<cgraph_node *, 8> *);

static bool
init_lreg_apply_inline (cgraph_node *ucaller, cgraph_node *node,
			init_lreg_transfer_kind kind, unsigned *live,
			bool *known, auto_vec<cgraph_node *, 8> *active)
{
  bool has_events = false;
  auto_vec<cgraph_node *, 8> scan_active;
  if (!init_lreg_inline_has_events (ucaller, node, &has_events, known,
				    &scan_active)
      || !*known)
    return false;
  if (!has_events)
    return true;

  for (cgraph_node *seen : *active)
    if (seen == node)
      {
	*known = false;
	return false;
      }
  unsigned active_mark = active->length ();
  active->safe_push (node);

  cgraph_node *body_node = init_lreg_body_node (node);
  function *fn = body_node
    ? DECL_STRUCT_FUNCTION (body_node->decl) : nullptr;
  auto_vec<basic_block> order;
  if (!fn || !fn->cfg || !init_lreg_linear_body (fn, &order))
    {
      *known = false;
      active->truncate (active_mark);
      return false;
    }

  bool backward = kind == INIT_LREG_DEMAND
    || kind == INIT_LREG_EFFECT_DEMAND;
  for (unsigned ix = 0; ix != order.length (); ++ix)
    {
      basic_block bb = order[backward ? order.length () - 1 - ix : ix];
      if (backward)
	for (gimple_stmt_iterator gsi = gsi_last_bb (bb);
	     !gsi_end_p (gsi); gsi_prev (&gsi))
	  if (!init_lreg_apply_stmt (ucaller, node, gsi_stmt (gsi), kind,
				     live, known, active))
	    {
	      active->truncate (active_mark);
	      return false;
	    }
      if (!backward)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	     !gsi_end_p (gsi); gsi_next (&gsi))
	  if (!init_lreg_apply_stmt (ucaller, node, gsi_stmt (gsi), kind,
				     live, known, active))
	    {
	      active->truncate (active_mark);
	      return false;
	    }
    }
  active->truncate (active_mark);
  return true;
}

/* Apply one direct event, or recursively substitute the site-aware transfer
   of a committed-inline call.  */

static bool
init_lreg_apply_stmt (cgraph_node *ucaller, cgraph_node *owner, gimple *stmt,
		      init_lreg_transfer_kind kind, unsigned *live,
		      bool *known, auto_vec<cgraph_node *, 8> *active)
{
  init_lreg_event ev;
  if (!init_lreg_event_for_stmt (stmt, &ev))
    {
      *known = false;
      return false;
    }
  if (ev.kind == INIT_LREG_NONE)
    {
      bool edge_known = true;
      cgraph_edge *e
	= init_lreg_inline_edge (owner, ucaller, stmt, &edge_known);
      cgraph_node *body_owner = init_lreg_body_node (owner);
      if (!e && body_owner != owner && edge_known)
	e = init_lreg_inline_edge (body_owner, ucaller, stmt, &edge_known);
      if (!edge_known)
	{
	  *known = false;
	  return false;
	}
      return !e || init_lreg_apply_inline (ucaller, e->callee, kind, live,
					   known, active);
    }

  switch (kind)
    {
    case INIT_LREG_OWNED:
      if (ev.kind == INIT_LREG_ACCESS)
	*live = (*live & ~ev.first) | ev.writes;
      else if (ev.kind == INIT_LREG_TYPED_READ
	       || ev.kind == INIT_LREG_TYPED_WRITE)
	*live &= ~(ev.first | ev.writes);
      break;
    case INIT_LREG_PROVENANCE:
      if (ev.kind == INIT_LREG_EFFECT)
	*live |= ev.writes;
      else if (ev.kind == INIT_LREG_ACCESS)
	*live = (*live & ~ev.first) | ev.writes;
      else if (ev.kind == INIT_LREG_TYPED_READ
	       || ev.kind == INIT_LREG_TYPED_WRITE)
	*live &= ~(ev.first | ev.writes);
      break;
    case INIT_LREG_DEMAND:
    case INIT_LREG_EFFECT_DEMAND:
      if (ev.kind == INIT_LREG_EFFECT)
	*live = (*live & ~ev.writes) | ev.first;
      else if (ev.kind == INIT_LREG_ACCESS)
	*live &= ~(ev.first | ev.writes);
      else if (ev.kind == INIT_LREG_TYPED_READ)
	*live = kind == INIT_LREG_EFFECT_DEMAND
	  ? *live & ~ev.first : *live | ev.first;
      else if (ev.kind == INIT_LREG_TYPED_WRITE)
	*live &= ~ev.writes;
      break;
    }
  return true;
}

static unsigned
init_lreg_transfer_block (cgraph_node *ucaller, basic_block bb, unsigned live,
			  init_lreg_transfer_kind kind, bool *known)
{
  auto_vec<cgraph_node *, 8> active;
  bool backward = kind == INIT_LREG_DEMAND
    || kind == INIT_LREG_EFFECT_DEMAND;
  if (backward)
    for (gimple_stmt_iterator gsi = gsi_last_bb (bb);
	 !gsi_end_p (gsi); gsi_prev (&gsi))
      {
	if (!init_lreg_apply_stmt (ucaller, ucaller, gsi_stmt (gsi), kind,
				   &live, known, &active))
	  return live;
      }
  if (!backward)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	if (!init_lreg_apply_stmt (ucaller, ucaller, gsi_stmt (gsi), kind,
				   &live, known, &active))
	  return live;
      }
  return live;
}

/* Return null when FIXED_MASK is disjoint from every raw architectural value
   live through ENTRY.  The edge-local composition is the RTL pass's block-
   entry rule: explicit ownership, raw-effect demand, and typed demand with a
   reaching raw definition.  */

static const char *
init_fixed_lreg_live_through (cgraph_node *ucaller, function *fn, edge entry,
			      unsigned fixed_mask, unsigned *live_out)
{
  *live_out = 0;
  if (!fixed_mask)
    return nullptr;
  if (!init_lreg_inline_edges_mapped_p (ucaller, ucaller, fn))
    return "drain-init-rawlreg-unproven";

  const unsigned n_bbs = last_basic_block_for_fn (fn);
  auto_vec<unsigned> owned_in (n_bbs), owned_out (n_bbs);
  auto_vec<unsigned> provenance_in (n_bbs), provenance_out (n_bbs);
  auto_vec<unsigned> demand_in (n_bbs), demand_out (n_bbs);
  auto_vec<unsigned> effect_in (n_bbs), effect_out (n_bbs);
  owned_in.safe_grow_cleared (n_bbs);
  owned_out.safe_grow_cleared (n_bbs);
  provenance_in.safe_grow_cleared (n_bbs);
  provenance_out.safe_grow_cleared (n_bbs);
  demand_in.safe_grow_cleared (n_bbs);
  demand_out.safe_grow_cleared (n_bbs);
  effect_in.safe_grow_cleared (n_bbs);
  effect_out.safe_grow_cleared (n_bbs);

  bool known = true;
  bool changed;
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_in = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    next_in |= owned_out[e->src->index];
	  unsigned next_out = init_lreg_transfer_block
	    (ucaller, bb, next_in, INIT_LREG_OWNED, &known);
	  if (!known)
	    return "drain-init-rawlreg-unproven";
	  if (next_in != owned_in[bb->index]
	      || next_out != owned_out[bb->index])
	    {
	      owned_in[bb->index] = next_in;
	      owned_out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_in = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    next_in |= provenance_out[e->src->index];
	  unsigned next_out = init_lreg_transfer_block
	    (ucaller, bb, next_in, INIT_LREG_PROVENANCE, &known);
	  if (!known)
	    return "drain-init-rawlreg-unproven";
	  if (next_in != provenance_in[bb->index]
	      || next_out != provenance_out[bb->index])
	    {
	      provenance_in[bb->index] = next_in;
	      provenance_out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  for (unsigned pass = 0; pass != 2; ++pass)
    {
      auto_vec<unsigned> &in = pass ? effect_in : demand_in;
      auto_vec<unsigned> &out = pass ? effect_out : demand_out;
      do
	{
	  changed = false;
	  basic_block bb;
	  FOR_EACH_BB_FN (bb, fn)
	    {
	      unsigned next_out = 0;
	      edge e;
	      edge_iterator ei;
	      FOR_EACH_EDGE (e, ei, bb->succs)
		next_out |= in[e->dest->index];
	      unsigned next_in = init_lreg_transfer_block
		(ucaller, bb, next_out,
		 pass ? INIT_LREG_EFFECT_DEMAND : INIT_LREG_DEMAND, &known);
	      if (!known)
		return "drain-init-rawlreg-unproven";
	      if (next_in != in[bb->index] || next_out != out[bb->index])
		{
		  in[bb->index] = next_in;
		  out[bb->index] = next_out;
		  changed = true;
		}
	    }
	}
      while (changed);
    }

  unsigned live = owned_out[entry->src->index]
    | effect_in[entry->dest->index]
    | (demand_in[entry->dest->index]
	& provenance_out[entry->src->index]);
  *live_out = live & 0xff;
  return (live & fixed_mask) ? "drain-init-fixed-lreg-live" : nullptr;
}

/* Commit: insert the hoisted prefix as typed builtin calls at the tail
   of the caller loop's dedicated preheader (created on the entry edge
   when needed), in the callee's own emission order: enable, owned
   SETC16 program (stage 2), staged descriptor words.  The caller's
   inline transform has not run yet, so every inserted call needs a
   cgraph edge.  */

static void
init_commit_caller (cgraph_node *caller, edge entry,
		    const rvtt_init_hoist_program &prog, int stage)
{
  const rvtt_insn_data *encc_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpencc_all_lanes);
  const rvtt_insn_data *setc16_d
    = rvtt_get_insn_data (rvtt_insn_data::ttsetc16);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);

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
      auto_vec<gcall *> seq;
      tree staged = rvtt_build_loadimm32 (prog.words[i].word, &seq);
      gcall *wrcfg = gimple_build_call
	(wrcfg_d->decl, 3, staged, build_int_cst (unsigned_type_node, 0),
	 build_int_cst (unsigned_type_node, prog.words[i].dest));
      for (gcall *c : seq)
	place (c);
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

/* The caller-chain resolver shared by the init hoist and the ADDR_MOD
   hoist.  Resolve the effective caller chain F <- W1 <- ... <- U: each
   intermediate must be the target of exactly one call edge, not
   address-taken, and committed into its inliner (inlined_to), so at run
   time its statements execute inline at the call site between the
   loop's trips; its body (through the clone_of origin, a sound
   over-approximation) is scanned as part of the loop epoch.  U is the
   outermost node still carrying its own GIMPLE CFG; the hoist lands in
   U's loop preheader.  REQUIRE_GIMPLE_BODY additionally requires that
   typed builtin calls can still be inserted into U.  Finally U must be
   rooted in the TU scan.  Returns null on success with
   CHAIN/UCALLER/UCALL/CFN filled; otherwise FACE_UNAVAILABLE after a
   "TAG: closure (<what>)" dump line.  */

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
  compute_tu_facts ();
  if (TREE_PUBLIC (callee_fn->decl) || tu_facts.scan_unrooted
      || !tu_facts.entry_roots
      || tu_facts.entry_roots->contains (cn))
    return closure_why ("callee-entry-unproved");
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
  /* The caller must still be GIMPLE to receive typed builtin calls.  */
  if (require_gimple_body && !ucaller->has_gimple_body_p ())
    return closure_why ("caller-past-gimple");
  function *cfn = DECL_STRUCT_FUNCTION (ucaller->decl);
  if (!cfn || !cfn->cfg)
    return closure_why ("caller-cfg-unavailable");

  /* TU facts (MOP template scan) over every body that still has
     GIMPLE; already-expanded bodies had their delivered words scanned
     when they were the contract subject.  */
  compute_tu_facts ();

  /* The commit target must be a body the scan vouched for.  */
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
   committed caller-side insertion with PROG->stage set.  With COMMIT
   false (the pricing pre-run) run the same proof and set every out
   field -- stage and the caller-loop trip weight -- without inserting
   anything.  */

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

  /* Resolve the caller chain.  */
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
      /* The hoisted prefix emits ENCC, which resets column flags only.
	 It must not change a masked caller's programming into an apparent
	 full-register definition when LaneConfig is unproved.  */
      rvtt_gimple_lane_facts lanes (cfn);
      if (!(lanes.block_out (entry->src) & RVTT_LANE_ROW_MASK_CLEAR))
	result = "drain-init-row-mask-unproved";
    }
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
     calls, so every hop body is scanned too (through the clone_of
     origin when the clone has no materialized body; classification is
     parameter-independent, so the origin over-approximates soundly).  */
  if (!result)
    result = scan_chain_hops (&ctx, chain, "drain-init-callers-unproven",
			      "init-hoist");

  if (!result && ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_init_ok_p (*prog, c, &ctx.owned_row_dirty, &why))
	result = why;
    }

  if (!result && prog->n_words)
    {
      unsigned live = 0;
      result = init_fixed_lreg_live_through (ucaller, cfn, entry,
					    1u << 0, &live);
      if (result)
	{
	  if (strcmp (result, "drain-init-fixed-lreg-live") == 0)
	    rvtt_refuse_by_name_at
	      (result, ucall, dump_file,
	       "init-hoist: refused (%s): entry raw-live mask 0x%02x, "
	       "fixed prefix mask 0x01\n", result, live);
	  else
	    rvtt_refuse_by_name_at
	      (result, ucall, dump_file,
	       "init-hoist: refused (%s): caller raw-LREG transfer unknown, "
	       "fixed prefix mask 0x01\n", result);
	}
    }

  if (!result)
    {
      /* Stage decision.  An in-loop owned-row write means the caller
	 re-programs an owned row between calls: with the callee's
	 per-call SETC16 program retained (stage 1) the interleaving is
	 unchanged, but hoisting it (stage 2) would change what the next
	 call's launches address, so demote.  */
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
      /* Caller-loop trip weight for pricing: the proven loop's profile
	 entry/body execution-count fraction, filled for both the
	 proof-only and the committing call as in the planner's
	 loop_trip_weight.  Without a usable estimate CALLER_WEIGHT_OK
	 stays false.  This only affects profitability.  */
      {
	basic_block body_bb = gimple_bb (ucall);
	profile_count bc = body_bb->count, pc = entry->count ();
	if (bc.initialized_p () && pc.initialized_p () && pc.nonzero_p ())
	  {
	    int64_t b = bc.to_gcov_type (), p = pc.to_gcov_type ();
	    if (p > 0 && b >= p)
	      {
		/* The same 48-bit scaling as the planner's
		   loop_trip_weight (rvtt-delivery-cost-core.h).  */
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
	  /* The caller's body was changed outside its own pipeline, so
	     any cached summary of it is stale.  */
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
   Cross-call ADDR_MOD hoist (Dst auto-increment service).

   A straight-line callee whose Dst auto-increment groups are rejected
   only because of the per-call configuration cost (each SETC16 takes
   the two-cycle configuration issue class plus the per-entry drain on
   every call) re-programs its owned address-modifier slot on every
   call, although the program is the same compile-time constant triple
   each time.  This service is called from the callee's Dst
   auto-increment pass (rtl-rvtt-dst-autoincr.cc) while every caller
   body is still GIMPLE.  It proves the caller side and, on success,
   inserts the slot program as typed ttsetc16 builtin calls in the
   caller's loop-entry preheader, lifted across enclosing caller loops
   where possible.  The callee's groups then omit the program.

   Soundness rests on the ISA fact that ThreadConfig ADDR_MOD rows are
   per-thread and written only by same-thread SETC16 (WRCFG,
   CFGSHIFTMASK and RMWCIB cannot write ThreadConfig).  Hence:
     - the caller epoch at the call's loop and at every lifted level is
       scanned as for the init hoist (init_scan_stmt): any statement or
       delivered word that could write a contract row -- typed
       ttsetc16/config calls, replay content, unclassified words or
       calls -- refuses, and vector dataflow refuses (a later formation
       in the caller could own the slot);
     - a SETC16-class delivered word to an owned row, or to a watched
       row (the Wormhole ADDR_MOD_SET_Base bank-select register, whose
       flip would re-alias the scratch modifier to the base-0 bank),
       refuses; there is no demotion stage because the callee no longer
       emits the program;
     - MOP template slots are checked TU-wide (mop_init_ok_p);
     - the target preheader must not already hold another hoist's typed
       programming (two hoists into one preheader have no defined order
       against each other's consumers);
     - CC state is irrelevant: SETC16 is not lane-predicated.

   The callee-side conditions (ownership scan, entry distance, call
   boundary cost, group shape) live in rtl-rvtt-dst-autoincr.cc.

   Refusals (scan failures use the init hoist's drain-init-* names):
     crosscall-addrmod-callers-unproven   closure (alias/clone/address-
					  taken/multi-site/expanded/
					  caller past gimple)
     crosscall-addrmod-loop-unproven      no natural loop / no provable
					  entry edge
     crosscall-addrmod-owned-row-write    a SETC16-class delivery to an
					  owned or watched row in the
					  scanned epoch
     crosscall-addrmod-preheader-occupied another hoist's typed
					  programming already sits
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

  /* The scan program: the contract rows plus the watched rows.  A
     watched-row write sets owned_row_dirty and so refuses like an
     owned-row write.  */
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

  /* Resolve the caller chain.  */
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

  /* Chain hops' statements execute per trip, between calls: scan
     every hop body too.  */
  if (!result)
    result = scan_chain_hops (&ctx, chain,
			      "crosscall-addrmod-callers-unproven",
			      "addrmod-hoist");

  /* Lift the program point across enclosing caller loops whose extra
     bodies pass the same epoch scan.  A failing level stops the walk --
     the inner placement stands and nothing refuses -- and that level's
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

  /* Any possible owned-row (or watched-row) write in the scanned epoch
     refuses: the callee no longer re-establishes the program per call.
     CC writes are irrelevant since SETC16 is not lane-predicated.  */
  if (!result && ctx.owned_row_dirty)
    result = "crosscall-addrmod-owned-row-write";

  /* The target preheader must not already hold another hoist's typed
     programming: two hoists into one preheader have no defined order
     against each other's consumers.  */
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
      /* The caller's body was changed outside its own pipeline, so any
	 cached summary of it is stale.  */
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
