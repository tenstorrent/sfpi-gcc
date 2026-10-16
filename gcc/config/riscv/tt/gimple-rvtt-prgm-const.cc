/* Allocate programmable constant registers to loop-invariant SFPU
   immediates.
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

/* Programmable constant registers.

   SFPCONFIG destinations 12..14 (sfpi CREG_IDX_PRGM1..3, vConstFloatPrgm0..2)
   hold programmable constants that are read as constant-register operands
   and so occupy no allocatable LREG.  This pass drives three transforms
   that use them:
   - -mtt-tensix-optimize-prgm-const (gimple-rvtt-prgm-fuse.cc): a
     loop-invariant immediate that the invariant pass left in a loop, by now
     an SFPADDI/SFPADD fed by a single-use SFPMUL or an operand of an
     existing SFPMAD, is programmed once on the loop entry edge and read as
     a constant register.  This only changes which register an operand is
     read from; it never fuses a MUL+ADD into a MAD itself (that would
     change rounding), but the register form lets the later mul+add->mad
     combine fire;
   - -mtt-tensix-optimize-const-residency (gimple-rvtt-prgm-residency.cc);
   - -mtt-tensix-optimize-const-remat (gimple-rvtt-prgm-remat.cc).

   PRGM registers are persistent machine state, so a register may only be
   used if the whole translation unit is shown not to depend on it.  This
   file computes that proof once, at the pass's first execution, while
   every function body is still in gimple (tu_prgm_facts):
   - every raw `.ttinsn' word must decode through the known-word table
     (rvtt_mop_audited_word_p); SFPCONFIG words claim their destination;
   - every store is classified by target range (rvtt-mop-tables.h); MOP
     template-slot writes must themselves hold known words, so MOP
     expansions are derived rather than trusted (rvtt-mop-derive.cc);
   - user vConstFloatPrgm assignments claim their destination;
   - indirect calls and calls to functions without a body in the TU
     refuse, except the crt0 init-array walk (rvtt_mop_init_array_call_p);
     defined callees are scanned.
   Any refusal blocks PRGM use for the whole TU.  The programming point
   must also run with all lanes enabled: a CC writer that can reach it
   refuses the candidate (cc-region-unproven).

   The same scan records whether any opaque delivery channel can change
   the lane-enable state, for rvtt_tu_opaque_cc_ambient_preserving_p.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
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
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "tree-ssa-loop-niter.h"
#include "tree-scalar-evolution.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-placement.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-ipa-summary.h"
#include "gimple-rvtt-prgm-int.h"

/* ------------------------------------------------------------------ */
/* TU-wide PRGM freedom facts, computed once.			      */

static prgm_tu_facts tu_facts;

/* Defined in gimple-rvtt-prgm-remat.cc.  */
bool staged_config_value (tree staged, unsigned *value);

/* Record a claim of SFPCONFIG destination DEST with VALUE in the TU
   facts' unique-value table.  KNOWN false means the value is unknown.  */

static void
tu_fold_claim_value (unsigned dest, bool known, uint32_t value)
{
  unsigned bit = 1u << dest;
  if (!(tu_facts.claimed & bit) && !(tu_facts.value_known & bit))
    {
      /* First claim of this destination.  */
      if (known)
	{
	  tu_facts.value_known |= bit;
	  tu_facts.value[dest] = value;
	}
      return;
    }
  if ((tu_facts.value_known & bit)
      && (!known || tu_facts.value[dest] != value))
    tu_facts.value_known &= ~bit;
}

/* Claims made inside the shared classifiers (raw words, template slots)
   carry no known value, so every destination in CLAIMS loses value
   uniqueness.  Callers pass the classifiers a zeroed local mask so that
   repeat claims are visible here.  */

static void
tu_mark_claims_unknown (unsigned claims)
{
  tu_facts.value_known &= ~claims;
}

/* Classify asm STMT for the TU scan, accumulating claimed destinations
   in *CLAIMED and MOP facts in ST.  An empty template emits nothing; a few
   scalar RISC-V templates are known to be harmless; the scalar
   blocking-store idiom is classified as a store; the only accepted Tensix
   form is one `.ttinsn' directive with one constant input.  On refusal set
   *WHY and return false.  */

static bool
scan_raw_asm (gasm *stmt, unsigned *claimed, const char **why,
	      rvtt_mop_derive_state *st)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return true;		/* pure barrier, no instruction */
  /* Base-ISA instructions with no Tensix effect (fence, ebreak, the
     startup stack-pointer load).  */
  if (!strcmp (s, "fence") || !strcmp (s, "ebreak")
      || !strcmp (s, "la sp, %0")
      /* The crt0 global-pointer initialization.  */
      || !strcmp (s, ".option push\n.option norelax\n"
		     "la gp, __global_pointer$\n.option pop"))
    return true;
  /* The scalar store-load-consume idiom (pcbuf/mailbox handshakes)
     stores its value operand at its address operand, so classify it like
     any other store.  */
  if (rvtt_mop_blocking_store_asm_p (stmt))
    return rvtt_mop_derive_asm_store (stmt, claimed, why, st);
  if (strncmp (s, ".ttinsn", 7) != 0)
    {
      *why = "non-.ttinsn assembly";
      return false;
    }
  s += 7;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strcmp (s, "%0") != 0
      || gimple_asm_ninputs (stmt) != 1
      || gimple_asm_noutputs (stmt) != 0)
    {
      *why = "unrecognized .ttinsn shape";
      return false;
    }
  tree op = TREE_VALUE (gimple_asm_input_op (stmt, 0));
  if (TREE_CODE (op) != INTEGER_CST)
    {
      *why = "non-literal .ttinsn word";
      return false;
    }
  return rvtt_mop_audited_word_p ((uint32_t) TREE_INT_CST_LOW (op), claimed,
				  why, st);
}

/* Scan the body of FN, accumulating claimed destinations in *CLAIMED
   and MOP facts in ST.  On refusal set *WHY to the first reason and return
   false, but keep scanning so that later statements still record their
   claims and every blocker is dumped.  VISITED memoizes context-free
   scans; CTX carries the object scan and, for an on-demand callee scan,
   the parameter bindings of the call; ACTIVE detects recursion.  */

static bool
scan_function_body (function *fn, unsigned *claimed, const char **why,
		    hash_set<function *> &visited,
		    rvtt_mop_derive_state *st,
		    rvtt_mop_scan_ctx *ctx,
		    hash_set<function *> &active)
{
  /* A bound scan re-reads the body under its call site's parameter
     bindings, so it is not memoized.  Call cycles refuse.  */
  if ((!ctx || !ctx->parms) && visited.contains (fn))
    return true;
  if (active.contains (fn))
    {
      *why = "mop-scan-recursion-unproven";
      return false;
    }
  active.add (fn);
  if (!ctx || !ctx->parms)
    visited.add (fn);
  bool ok = true;
  /* A refusal also marks the TU-wide lane-enable fact dirty
     (rvtt_mop_derive_state::cc_dirty), unless the refused statement is a
     raw `.ttinsn' word that cannot change the all-lanes state (e.g. a
     load-macro template capture, a non-allocatable SFPLOADI destination,
     the all-lanes SFPENCC).  Any other refusal could hide a lane-enable
     write.  */
  auto cc_exempt_word_p = [] (gimple *stmt) -> bool
    {
      gasm *a = dyn_cast <gasm *> (stmt);
      uint32_t w;
      return a && rvtt_raw_ttinsn_word_p (a, &w)
	&& rvtt_raw_cc_word_ambient_preserving_p (w);
    };
  auto refuse = [&] (const char *w, gimple *stmt)
    {
      if (ok)
	{
	  *why = w;
	  ok = false;
	}
      if (st && !st->cc_dirty && !cc_exempt_word_p (stmt))
	{
	  st->cc_dirty = true;
	  snprintf (st->cc_reason, sizeof st->cc_reason, "%s in %s", w,
		    function_name (fn));
	}
      if (dump_file)
	{
	  fprintf (dump_file, "prgm-const: blocker in %s: %s: ",
		   function_name (fn), w);
	  print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
	}
    };
  /* The address of an automatic aggregate used as a value escapes and
     poisons that object's field scan; direct field references are not
     escapes.  Call arguments are exempt only for the bound callee scan
     below, which sees the callee's stores through the binding.  */
  auto poison_bare_addr = [&] (tree op)
    {
      if (op && TREE_CODE (op) == ADDR_EXPR)
	{
	  tree var = TREE_OPERAND (op, 0);
	  if (VAR_P (var) && !TREE_STATIC (var) && !DECL_EXTERNAL (var)
	      && AGGREGATE_TYPE_P (TREE_TYPE (var)))
	    {
	      if (dump_file && ctx && ctx->scan)
		{
		  fprintf (dump_file, "prgm-const: scan poison in %s: ",
			   function_name (fn));
		  print_generic_expr (dump_file, var, TDF_NONE);
		  fprintf (dump_file, "\n");
		}
	      rvtt_mop_scan_poison (ctx, var);
	    }
	}
    };

  /* Under -mtt-tensix-optimize-opaque-replay-record, first pass every
     raw REPLAY word through the record admission (rvtt-mop-derive.cc).
     The words recorded by an admitted exec=0 record are never executed, so
     the main walk skips them; the record word itself carries its verdict.
     Without the flag every REPLAY word refuses through the word table.  */
  hash_set<gimple *> replay_suppressed;
  hash_map<gimple *, const char *> replay_verdict;
  if (riscv_tt_opt_opaque_replay_record)
    {
      basic_block pbb;
      FOR_EACH_BB_FN (pbb, fn)
	for (gimple_stmt_iterator pgsi = gsi_start_bb (pbb);
	     !gsi_end_p (pgsi); gsi_next (&pgsi))
	  if (gasm *pa = dyn_cast <gasm *> (gsi_stmt (pgsi)))
	    {
	      uint32_t w;
	      if (!rvtt_raw_ttinsn_word_p (pa, &w)
		  || (w >> 24) != XTT_REPLAY_OPCODE)
		continue;
	      const char *rwhy = nullptr;
	      if (rvtt_mop_replay_record_admit (pa, w, &replay_suppressed,
						&rwhy))
		replay_verdict.put (pa, nullptr);
	      else
		replay_verdict.put (pa, rwhy);
	    }
      /* The region walk refuses nested record words, so an admitted
	 window never contains another record word.  */
      if (flag_checking)
	for (gimple *s : replay_suppressed)
	  gcc_checking_assert (!replay_verdict.get (s));
    }

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (ctx && ctx->scan)
	for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	     gsi_next (&psi))
	  for (unsigned i = 0; i != gimple_phi_num_args (psi.phi ()); ++i)
	    poison_bare_addr (gimple_phi_arg_def (psi.phi (), i));

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;

	  if (gasm *a = dyn_cast <gasm *> (stmt))
	    {
	      if (ctx && ctx->scan)
		for (unsigned i = 0; i != gimple_asm_ninputs (a); ++i)
		  poison_bare_addr (TREE_VALUE (gimple_asm_input_op (a, i)));
	      /* Recorded by an admitted exec=0 window: never executed.  */
	      if (replay_suppressed.contains (stmt))
		{
		  if (dump_file)
		    {
		      fprintf (dump_file, "prgm-const: record-window word "
			       "swallowed (never delivered) in %s: ",
			       function_name (fn));
		      print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
		    }
		  continue;
		}
	      if (const char **v = replay_verdict.get (a))
		{
		  if (*v)
		    refuse (*v, stmt);
		  else if (dump_file)
		    {
		      fprintf (dump_file, "prgm-const: replay record word "
			       "admitted (no playback path) in %s: ",
			       function_name (fn));
		      print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
		    }
		  continue;
		}
	      const char *w = nullptr;
	      unsigned local_claims = 0;
	      if (!scan_raw_asm (a, &local_claims, &w, st))
		refuse (w, stmt);
	      *claimed |= local_claims;
	      tu_mark_claims_unknown (local_claims);
	      continue;
	    }

	  if (!is_gimple_call (stmt))
	    {
	      /* ADDR_EXPR operands on the right-hand side escape.  */
	      if (ctx && ctx->scan && is_gimple_assign (stmt))
		for (unsigned i = 1; i != gimple_num_ops (stmt); ++i)
		  poison_bare_addr (gimple_op (stmt, i));
	      /* Classify stores: template-slot writes, instruction-FIFO pushes,
		 and volatile stores that must not alias a FIFO.  */
	      const char *w = nullptr;
	      unsigned local_claims = 0;
	      if (!rvtt_mop_derive_store (stmt, &local_claims, &w, st, ctx))
		refuse (w, stmt);
	      *claimed |= local_claims;
	      tu_mark_claims_unknown (local_claims);
	      continue;
	    }

	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  /* Aggregate addresses passed to a call escape unless the callee is
	     scanned under this call's bindings (the on-demand branch
	     below).  */
	  auto poison_call_addr_args = [&] ()
	    {
	      if (!ctx || !ctx->scan)
		return;
	      gcall *c = as_a <gcall *> (stmt);
	      for (unsigned i = 0; i != gimple_call_num_args (c); ++i)
		poison_bare_addr (gimple_call_arg (c, i));
	    };
	  if (insnd)
	    {
	      poison_call_addr_args ();
	      gcall *call = as_a <gcall *> (stmt);
	      if (insnd->id == rvtt_insn_data::ttregion_begin
		  || insnd->id == rvtt_insn_data::ttregion_end)
		{
		  /* The ttregion markers are deprecated no-ops; raw words and
		     MOP expansions are classified on their own.  */
		}
	      else if (insnd->id == rvtt_insn_data::sfpwriteconfig_v)
		{
		  tree dest = gimple_call_arg (call, 2);
		  if (TREE_CODE (dest) != INTEGER_CST)
		    {
		      refuse ("non-constant user SFPCONFIG destination", stmt);
		      continue;
		    }
		  unsigned d = TREE_INT_CST_LOW (dest) & 0xf;
		  if (d == 15)
		    {
		      refuse ("user SFPCONFIG writes LaneConfig", stmt);
		      continue;
		    }
		  unsigned value = 0;
		  bool known
		    = staged_config_value (gimple_call_arg (call, 0), &value);
		  tu_fold_claim_value (d, known, value);
		  *claimed |= 1u << d;
		}
	      else if (insnd->id == rvtt_insn_data::sfpconfig_i)
		{
		  /* The immediate form always writes LaneConfig (15); refuse
		     as for the value form above, since LaneConfig bits such as
		     DISABLE_BACKDOOR_LOAD change how template-slot programming
		     behaves.  */
		  refuse ("user SFPCONFIG writes LaneConfig", stmt);
		  continue;
		}
	      /* Other typed builtins, including the matrix-unit family
		 (ttmovd2b, ttmovb2a, ttmovb2d, ttmova2d, tttrnspsrcb,
		 ttstallwait, ttrmwcib), cannot write an SFPCONFIG destination
		 (SFPCONFIG.md vs RMWCIB.md/SETC16.md).  */
	      continue;		/* typed builtins are transparent */
	    }

	  if (gimple_call_internal_p (stmt))
	    {
	      poison_call_addr_args ();
	      continue;
	    }
	  tree fndecl = gimple_call_fndecl (stmt);
	  if (!fndecl)
	    {
	      poison_call_addr_args ();
	      /* The crt0 init-array walk calls only this TU's static
		 constructors, which the TU walk scans.  */
	      if (rvtt_mop_init_array_call_p (as_a <gcall *> (stmt)))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "prgm-const: init-array walk call in %s proven "
			     "(TU-registered constructors only)\n",
			     function_name (fn));
		  continue;
		}
	      refuse ("indirect call", stmt);
	      continue;
	    }
	  if (fndecl_built_in_p (fndecl))
	    {
	      poison_call_addr_args ();
	      continue;		/* scalar compiler builtin */
	    }
	  cgraph_node *cn = cgraph_node::get (fndecl);
	  /* The callee's node may already be gone (a fully inlined comdat
	     removed after IPA inlining), and with comdat groups the decl may be
	     a bodiless same-body cdtor alias.  The call edge still leads to the
	     body that executes, so resolve through it.  */
	  if (!cn || !cn->definition)
	    if (cgraph_node *caller = cgraph_node::get (fn->decl))
	      if (cgraph_edge *e = caller->get_edge (stmt))
		if (e->callee)
		  if (cgraph_node *target = e->callee->ultimate_alias_target ())
		    cn = target;
	  /* An alias or thunk executes its target's body (thunk glue is
	     scalar this-adjustment).  The depth bound only limits work; an
	     unresolved chain refuses below.  */
	  for (unsigned depth = 0;
	       cn && cn->definition && (cn->alias || cn->thunk) && depth != 8;
	       ++depth)
	    {
	      cgraph_node *target
		= cn->alias ? cn->ultimate_alias_target ()
		: cn->callees ? cn->callees->callee : nullptr;
	      if (!target || target == cn)
		break;
	      cn = target;
	    }
	  if (!cn || !cn->definition || !cn->has_gimple_body_p ()
	      /* The TU walk skips inline clones, so scan their body here.  */
	      || cn->inlined_to)
	    {
	      /* Scan the body on demand.  A parameter-adjusted clone (IPA-SRA)
		 has no body until materialization; clone transforms only
		 re-parameterize, so scanning the clone_of origin's body
		 over-approximates it.  */
	      function *cfn = DECL_STRUCT_FUNCTION (cn ? cn->decl : fndecl);
	      if (!cfn || !cfn->cfg)
		for (cgraph_node *origin = cn ? cn->clone_of : nullptr;
		     origin; origin = origin->clone_of)
		  {
		    function *ofn = DECL_STRUCT_FUNCTION (origin->decl);
		    if (ofn && ofn->cfg)
		      {
			cfn = ofn;
			break;
		      }
		  }
	      if (cfn && cfn->cfg)
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "prgm-const: on-demand scan of %s "
			     "for call in %s\n",
			     function_name (cfn), function_name (fn));
		  /* Bind the callee's parameters to this call's arguments and
		     scan the body under that binding, so the callee's stores
		     through an aggregate-address argument are attributed to
		     the caller's object.  */
		  hash_map<tree, rvtt_mop_bound_arg> parm_map;
		  {
		    gcall *c = as_a <gcall *> (stmt);
		    tree parm = DECL_ARGUMENTS (cfn->decl);
		    for (unsigned i = 0;
			 parm && i != gimple_call_num_args (c);
			 ++i, parm = DECL_CHAIN (parm))
		      parm_map.put (parm, rvtt_mop_bound_arg
				      { gimple_call_arg (c, i), ctx });
		  }
		  rvtt_mop_scan_ctx child
		    = { ctx ? ctx->scan : nullptr, &parm_map };
		  const char *w = nullptr;
		  if (!scan_function_body (cfn, claimed, &w, visited, st,
					   &child, active))
		    refuse (w, stmt);
		  continue;
		}
	      poison_call_addr_args ();
	      refuse ("call to a function outside this translation unit",
		      stmt);
	      continue;
	    }
	  /* Defined in this TU and scanned context-free by the TU walk,
	     which cannot attribute its stores to our objects.  */
	  poison_call_addr_args ();
	}
    }
  active.remove (fn);
  return ok;
}

/* Compute the TU facts once and return them.  This runs at the first
   execution of the pass, before any gimple body has been released;
   functions created after that point are compiler-generated scalar code
   with no Tensix instructions.  */

const prgm_tu_facts &
tu_prgm_facts ()
{
  if (tu_facts.computed)
    return tu_facts;
  tu_facts.computed = true;

  hash_set<function *> visited;
  hash_set<function *> active;
  rvtt_mop_obj_scan scan;
  rvtt_mop_scan_ctx root_ctx = { &scan, nullptr };
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;		/* thunks/aliases carry no code */
      /* An inline clone is never emitted on its own; its caller's scan
	 covers it under the call's parameter bindings.  */
      if (node->inlined_to)
	continue;
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      const char *why = nullptr;
      /* Keep scanning after a refusal so later functions still record
	 their claims; only the first blocker is reported.  */
      static char reason_buf[192];
      if (!ofn || !ofn->cfg)
	{
	  /* As above, scan an IPA-SRA clone through its origin's body.  */
	  for (cgraph_node *origin = node->clone_of; origin;
	       origin = origin->clone_of)
	    if (function *orig_fn = DECL_STRUCT_FUNCTION (origin->decl))
	      if (orig_fn->cfg)
		{
		  ofn = orig_fn;
		  break;
		}
	  if (!ofn || !ofn->cfg)
	    {
	      /* A defined body that cannot be walked refuses.  */
	      tu_facts.refused = true;
	      if (!tu_facts.reason)
		tu_facts.reason = "function body unavailable to the scan";
	      if (!tu_facts.mop.cc_dirty)
		{
		  tu_facts.mop.cc_dirty = true;
		  snprintf (tu_facts.mop.cc_reason,
			    sizeof tu_facts.mop.cc_reason,
			    "function body unavailable to the scan (%s)",
			    node->dump_name ());
		}
	      continue;
	    }
	}
      if (!scan_function_body (ofn, &tu_facts.claimed, &why, visited,
			       &tu_facts.mop, &root_ctx, active))
	{
	  tu_facts.refused = true;
	  if (!tu_facts.reason)
	    {
	      snprintf (reason_buf, sizeof reason_buf, "%s in %s", why,
			node->dump_name ());
	      tu_facts.reason = reason_buf;
	    }
	}
      /* Record constant-register readers (prgm_tu_facts::creg_read).
	 Every emitted body passes through this loop, so a clear bit means
	 no reader in the TU.  */
      basic_block rbb;
      FOR_EACH_BB_FN (rbb, ofn)
	for (gimple_stmt_iterator rgsi = gsi_start_bb (rbb);
	     !gsi_end_p (rgsi); gsi_next (&rgsi))
	  if (gcall *rc = dyn_cast <gcall *> (gsi_stmt (rgsi)))
	    {
	      const rvtt_insn_data *rd = rvtt_get_insn_data (rc);
	      if (!rd || rd->id != rvtt_insn_data::sfpreadlreg)
		continue;
	      tree a = gimple_call_arg (rc, 0);
	      if (TREE_CODE (a) == INTEGER_CST && tree_fits_uhwi_p (a)
		  && tree_to_uhwi (a) < 16)
		tu_facts.creg_read |= 1u << tree_to_uhwi (a);
	      else
		tu_facts.creg_read = 0xffff;
	    }
    }
  /* MOP words were admitted provisionally above; the admission stands
     only if every template-slot write in the TU was accepted.  */
  const char *mop_why = nullptr;
  if (!rvtt_mop_derive_finish (&tu_facts.mop, &mop_why))
    {
      tu_facts.refused = true;
      if (!tu_facts.reason)
	tu_facts.reason = mop_why;
      /* An unproven MOP expansion could also change the lane-enable
	 state.  */
      if (!tu_facts.mop.cc_dirty)
	{
	  tu_facts.mop.cc_dirty = true;
	  snprintf (tu_facts.mop.cc_reason, sizeof tu_facts.mop.cc_reason,
		    "%s", mop_why);
	}
    }

  /* Check the facts' invariants and dump them, with the TU's entry
     roots (rvtt_ipa_tu_anchors).  The dump wording avoids words that
     testsuite scan-dump-not patterns match.  */
  if (flag_checking)
    {
      /* A unique programmed value exists only for a claimed
	 destination; a blocked snapshot always names its blocker.  */
      gcc_assert ((tu_facts.value_known & ~tu_facts.claimed) == 0);
      gcc_assert (!tu_facts.refused || tu_facts.reason);
      gcc_assert (!tu_facts.mop.cc_dirty || tu_facts.mop.cc_reason[0]);
    }
  /* Compute the entry roots now, whether or not we dump.  */
  rvtt_ipa_tu_anchors ();
  if (dump_file)
    {
      const rvtt_ipa_tu_anchor_facts &anchors = rvtt_ipa_tu_anchors ();
      fprintf (dump_file,
	       "tu-facts: claimed %#x value-known %#x creg-read %#x%s\n",
	       tu_facts.claimed, tu_facts.value_known, tu_facts.creg_read,
	       tu_facts.refused ? " (blocked)" : "");
      if (tu_facts.refused && tu_facts.reason)
	fprintf (dump_file, "tu-facts: blocked by: %s\n", tu_facts.reason);
      for (unsigned d = 0; d != 16; ++d)
	if (tu_facts.value_known & (1u << d))
	  fprintf (dump_file, "tu-facts: dest %u unique value 0x%08x\n",
		   d, tu_facts.value[d]);
      if (tu_facts.mop.cc_dirty)
	fprintf (dump_file, "tu-facts: cc audit dirty: %s\n",
		 tu_facts.mop.cc_reason);
      else
	fprintf (dump_file, "tu-facts: cc audit clean\n");
      fprintf (dump_file,
	       "tu-facts: entry anchor %s, %u enumerable root(s), image %s\n",
	       anchors.has_start ? "_start"
	       : anchors.has_main ? "main" : "none",
	       anchors.n_entry_roots,
	       anchors.rooted ? "rooted" : "unrooted");
    }
  return tu_facts;
}

const pass_data pass_data_rvtt_prgm_const =
{
  GIMPLE_PASS, /* type */
  "rvtt_prgm_const", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa | PROP_cfg, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_prgm_const : public gimple_opt_pass
{
public:
  pass_rvtt_prgm_const (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_prgm_const, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
      && (riscv_tt_opt_prgm_const
	  || riscv_tt_opt_const_residency
	  || riscv_tt_opt_const_remat);
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	rvtt_refuse (RVTT_REF_QSR_UNPROVEN, dump_file,
		     "prgm-const: refused (qsr-unproven)\n");
	return 0;
      }
    /* Compute the TU facts on the first function, while every body in
       the TU is still in gimple.  Remat does not need them.  */
    if (riscv_tt_opt_prgm_const || riscv_tt_opt_const_residency)
      tu_prgm_facts ();
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    prgm_state st;
    bool changed = false;
    if (riscv_tt_opt_prgm_const)
      changed |= transform (fn, &st);
    if (riscv_tt_opt_const_residency)
      changed |= residency_transform (fn, &st);
    /* A callee's own-entry residency programming moves to its callers'
       loop entries; the moved registers then stay out of every later
       function's allocation (claimed, and read, so never reclaimed).  */
    if (riscv_tt_opt_crosscall_prgm > 0 && riscv_tt_opt_crosscall_hoist
	&& !tu_facts.refused)
      {
	unsigned regs = 0;
	for (const prgm_alloc &a : st.allocs)
	  if (!a.reclaimed || a.repeat)
	    regs |= 1u << a.reg;
	if (unsigned moved = rvtt_crosscall_prgm_hoist (fn, regs))
	  {
	    tu_facts.claimed |= moved;
	    tu_facts.creg_read |= moved;
	    changed = true;
	  }
      }
    if (riscv_tt_opt_const_remat)
      changed |= remat_transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

/* Create the PRGM constant pass.  rvtt-passes.def places it after the
   CC analysis and before the combiner, so that a rewritten immediate pair
   is re-offered to the mul+add->mad rule.  */

gimple_opt_pass *
make_pass_rvtt_prgm_const (gcc::context *ctxt)
{
  return new pass_rvtt_prgm_const (ctxt);
}

/* Return true if the TU scan found that no opaque delivery channel (raw
   `.ttinsn' words, stores to the instruction FIFO, MOP expansions, replay
   records, scalar asm) can change the all-lanes lane-enable state;
   otherwise set *REASON and return false.  Typed instructions are not
   covered; the caller (entry_ambient_all_lanes_p in
   rtl-rvtt-macro-planner.cc) classifies those itself.  This never
   computes the facts, since the scan is only valid at gimple time; if
   they were not computed, return false.  */

bool
rvtt_tu_opaque_cc_ambient_preserving_p (const char **reason)
{
  if (!tu_facts.computed)
    {
      *reason = "tu-audit-not-run";
      return false;
    }
  if (tu_facts.mop.cc_dirty)
    {
      *reason = tu_facts.mop.cc_reason;
      return false;
    }
  return true;
}
