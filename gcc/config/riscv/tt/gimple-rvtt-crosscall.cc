/* Cross-call hoist of call-invariant pinned-LREG materializations.
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

/* A noinline per-tile callee that loads call-invariant immediates into
   operands the target pins to specific hard LREGs (the SFPLUTFP32
   coefficients are the typical case) reloads them on every call.  This
   pass moves those loads into the enclosing loop preheader of every
   caller and carries the values across the call in the registers:

     callee:  c = sfpxloadi (CST)  ... consumer (c pinned to LREG k)
       ==>
     callee:  c = sfpreadlreg (k)  ... consumer
	      sfpwritelreg (c, k) before every return
     caller:  t = sfpxloadi (CST); sfpwritelreg (t, k)
	      on the entry edge of the loop containing the call.

   The zero-length read/write markers pin register allocation on both
   sides, so the value is in LREG k from callee entry to every return and
   is written to LREG k before the caller's loop.  What remains to be
   shown is that nothing else in the caller's loop writes LREG k:

   - in the callee, every use of each candidate is an operand whose md
     constraint names one hard LREG (pinned_lreg_operand); there is no CC
     write, call, unaudited asm or delivered word, no other access to a
     contract register, and no vector code outside the consumer loop; and
     the extended live ranges still fit in the eight LREGs;
   - the callee is not address-taken, aliased or cloned, and all its call
     sites are direct (one TU per TRISC image, kernel-single-TU); each
     caller has one call site, inside a loop with a unique entry edge;
   - every statement in that loop is shown not to write a contract LREG
     (scan_stmt): scalar code, audited asm, rvtt builtins without vector
     dataflow, and delivered instruction words whose opcode class cannot
     write an allocatable LREG.  MOP words are checked against the TU-wide
     template audit (mop_contract_ok_p); REPLAY words are refused;
   - no CC write in the loop, so the hoisted loads run under the same lane
     enables as the original ones, and no caller vector value is live in
     the loop, so the caller's allocator cannot use a contract register.

   The hoisted loads also run when the loop is entered but its body is
   not.  That is not observable: no caller vector value is live there,
   explicit reads of a contract register elsewhere in the caller are
   refused, and any execution that runs the body writes the same values
   anyway.  All callers are proven before anything is changed.

   With -mtt-tensix-optimize-crosscall-config-prefix, a load whose single
   use is an sfpwriteconfig_v to a programmable constant register 11..14
   is moved the same way (discover_config_prefix), and
   rvtt_crosscall_prgm_hoist moves the programming of registers claimed
   by constant residency.  This follows the idea of register promotion
   (Cooper and Lu, PLDI 1997), but across a call and into registers fixed
   by the machine description.  QSR is not supported.  */

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

/* Report the named refusal REASON in FN at STMT (either may be null) and
   return false.  */

bool
crosscall_refuse (const char *reason, tree fn, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "crosscall-hoist: refused (%s)", reason);
  if (dump_file)
    {
      if (fn)
	fprintf (dump_file, " in %s", IDENTIFIER_POINTER (DECL_NAME (fn)));
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

/* The insn code of each rvtt builtin, as in riscv-builtins.cc.  QSR
   overrides some entries, but this pass is disabled for QSR.  */

static const enum insn_code rvtt_builtin_icode[] = {
#define RVTT_FN(id, av, sfx, fmt, fl, ops) CODE_FOR_rvtt_##id##sfx,
#include "rvtt-insn.def"
#undef RVTT_FN
};

/* If argument ARGNO of CALL, a call to rvtt insn INSND, is constrained
   by the md pattern to exactly one hard LREG, return that register
   number, else -1.  The arguments of a plain builtin map positionally
   onto the pattern operands after the output; a builtin whose operand
   count differs is not handled.  */

static int
pinned_lreg_operand (const rvtt_insn_data *insnd, gcall *call, unsigned argno)
{
  if (insnd->id >= rvtt_insn_data::hwm)
    return -1;
  enum insn_code icode = rvtt_builtin_icode[insnd->id];
  if ((unsigned) icode >= NUM_INSN_CODES)
    return -1;
  const struct insn_data_d *d = &insn_data[(int) icode];
  unsigned nargs = gimple_call_num_args (call);
  unsigned offset = gimple_call_lhs (call) ? 1 : 0;
  if ((unsigned) d->n_operands != nargs + offset
      || argno + offset >= (unsigned) d->n_operands)
    return -1;
  const char *c = d->operand[argno + offset].constraint;
  if (c && c[0] == 'x' && c[1] >= '0' && c[1] <= '7' && c[2] == '\0')
    return c[1] - '0';
  return -1;
}

/* Return the SFPLOADI root of CALL if CALL is the SFPLOADI_LV tail of a
   chained pair, as emit_loadimm issues for a 32-bit constant whose
   halves are both significant; otherwise null.  */

static gcall *
prefix_load_root (gcall *call)
{
  return rvtt_chained_loadi_root (call);
}

/* Return true if CALL is a qualifying prefix load: sfpxloadi, sfploadi
   (after pass_rvtt_immload_shorten) or a chained sfploadi_lv tail, with
   the canonical buffer operand and constant scalar operands.  */

static bool
prefix_load_p (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return false;
  gcall *root = nullptr;
  if (insnd->id == rvtt_insn_data::sfploadi_lv)
    {
      root = prefix_load_root (call);
      if (!root)
	return false;
    }
  else if (insnd->id != rvtt_insn_data::sfpxloadi
	   && insnd->id != rvtt_insn_data::sfploadi)
    return false;
  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || !rvtt_canonical_buffer_arg_p (gimple_call_arg (call, 0)))
    return false;
  for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
    {
      /* On a chained tail argument 1 is the link to the root, which
	 prefix_load_root has already qualified.  */
      if (root && ix == 1)
	continue;
      if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
	return false;
    }
  return true;
}

/* Clone the prefix materialization LOAD -- and ROOT first, when LOAD is
   a chained tail -- appending each clone to OUT in placement order and
   returning the cloned value's SSA name.  */

static tree
clone_prefix_load (gcall *root, gcall *load, vec<gcall *> *out)
{
  tree link = NULL_TREE;
  if (root)
    {
      auto_vec<tree, 8> a;
      for (unsigned i = 0; i != gimple_call_num_args (root); ++i)
	a.safe_push (unshare_expr (gimple_call_arg (root, i)));
      gcall *c = gimple_build_call_vec (gimple_call_fndecl (root), a);
      link = make_ssa_name (TREE_TYPE (gimple_call_lhs (root)));
      gimple_call_set_lhs (c, link);
      out->safe_push (c);
    }
  auto_vec<tree, 8> a;
  for (unsigned i = 0; i != gimple_call_num_args (load); ++i)
    a.safe_push (unshare_expr (gimple_call_arg (load, i)));
  if (root)
    a[1] = link;		/* rewire to the cloned root */
  gcall *c = gimple_build_call_vec (gimple_call_fndecl (load), a);
  tree val = make_ssa_name (TREE_TYPE (gimple_call_lhs (load)));
  gimple_call_set_lhs (c, val);
  out->safe_push (c);
  return val;
}

/* Return true if T is a value of vector type (an SFPU vector datum).
   Null-safe: NULL_TREE (e.g. a missing lhs) is not.  */

bool
vector_typed_p (tree t)
{
  return t && TREE_TYPE (t) && VECTOR_TYPE_P (TREE_TYPE (t));
}

/* Return true if CALL has a vector-typed lhs or argument.  */

bool
call_has_vector_dataflow_p (gcall *call)
{
  if (vector_typed_p (gimple_call_lhs (call)))
    return true;
  for (unsigned i = 0; i != gimple_call_num_args (call); ++i)
    if (vector_typed_p (gimple_call_arg (call, i)))
      return true;
  return false;
}

/* Statement classification for the caller's loop and the callee's body:
   every statement must be shown unable to write a contract LREG.  */

/* Return true if the asm template S is one of the known scalar templates,
   which cannot deliver Tensix instructions.  Templates are compared
   exactly.  */

bool
audited_scalar_asm_p (const char *s)
{
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return true;		/* pure barrier */
  return !strcmp (s, "fence") || !strcmp (s, "ebreak")
    || !strcmp (s, "la sp, %0")
    || !strcmp (s, ".option push\n.option norelax\n"
		   "la gp, __global_pointer$\n.option pop");
}

/* Record the refusal WHY at STMT in CTX and return false.  The
   classifiers are shared by the caller, callee and region scans, so the
   name is translated to the one for the scan in progress.  */

static bool
scan_refuse (scan_ctx *ctx, const char *why, gimple *stmt)
{
  if (ctx->region && strncmp (why, "crosscall-caller-", 17) == 0)
    {
      /* Region-scan consumers get the crossloop taxonomy.  */
      const char *tail = why + 17;
      if (!strcmp (tail, "word-unproven"))
	why = "crossloop-word-unproven";
      else if (!strcmp (tail, "replay-unproven"))
	why = "crossloop-replay-unproven";
      else if (!strcmp (tail, "stmt-unproven"))
	why = "crossloop-stmt-unproven";
      else if (!strcmp (tail, "cc-unproven"))
	why = "crossloop-cc-unproven";
      else if (!strcmp (tail, "config-word-unproven"))
	why = "crossloop-config-word-unproven";
    }
  else if (ctx->region && strncmp (why, "crosscall-callee-", 17) == 0)
    {
      const char *tail = why + 17;
      if (!strcmp (tail, "clobber"))
	why = "crossloop-lreg-clobber";
      else if (!strcmp (tail, "cc-unproven"))
	why = "crossloop-cc-unproven";
      else if (!strcmp (tail, "stmt-unproven"))
	why = "crossloop-stmt-unproven";
    }
  else if (!ctx->in_caller && strncmp (why, "crosscall-caller-", 17) == 0)
    {
      const char *tail = why + 17;
      if (!strcmp (tail, "word-unproven"))
	why = "crosscall-callee-word-unproven";
      else if (!strcmp (tail, "replay-unproven"))
	why = "crosscall-callee-replay-unproven";
      else if (!strcmp (tail, "stmt-unproven"))
	why = "crosscall-callee-stmt-unproven";
    }
  ctx->why = why;
  ctx->why_stmt = stmt;
  return false;
}

/* Apply the delivered-word verdict V for STMT to CTX.  */

static bool
apply_word_verdict (scan_ctx *ctx, const word_verdict &v, gimple *stmt)
{
  if (v.is_mop)
    ctx->saw_mop = true;
  if (!v.ok)
    return scan_refuse (ctx, v.why, stmt);
  return true;
}

/* Scan the asm statement STMT in CTX.  */

static bool
scan_asm (scan_ctx *ctx, gasm *stmt)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strncmp (s, ".ttinsn", 7) == 0)
    {
      const char *t = s + 7;
      while (*t == ' ' || *t == '\t')
	++t;
      if (strcmp (t, "%0") != 0 || gimple_asm_ninputs (stmt) != 1
	  || gimple_asm_noutputs (stmt) != 0)
	return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
      return apply_word_verdict
	(ctx, classify_delivered_value
	   (TREE_VALUE (gimple_asm_input_op (stmt, 0)), ctx->contract_mask,
	    ctx->region, ctx->config_strict),
	 stmt);
    }
  tree value, ptr;
  if (blocking_store_asm_p (stmt, &value, &ptr))
    {
      /* The stored word only matters if it reaches an instruction
	 FIFO.  */
      unsigned HOST_WIDE_INT addr;
      if (pointer_constant_address (ptr, &addr))
	{
	  if (addr >= XTT_INSTRN_BUF_MMIO_BASE
	      && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	    return apply_word_verdict
	      (ctx, classify_delivered_value (value, ctx->contract_mask,
					      ctx->region,
					      ctx->config_strict),
	       stmt);
	  if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	    return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
	  return true;		/* sync/data aperture: delivers nothing */
	}
      return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
    }
  if (audited_scalar_asm_p (gimple_asm_string (stmt)))
    return true;
  return scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt);
}

/* Scan the store STMT in CTX.  */

static bool
scan_store (scan_ctx *ctx, gimple *stmt)
{
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return true;
  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    {
      if (addr >= XTT_INSTRN_BUF_MMIO_BASE && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
	return apply_word_verdict
	  (ctx, classify_delivered_value (gimple_assign_rhs1 (stmt),
					  ctx->contract_mask, ctx->region,
					  ctx->config_strict),
	   stmt);
      if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
	/* A MOP template slot write inside the scanned range is not
	   handled.  */
	return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
      return true;		/* other constant MMIO / L1: no LREG */
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
	return apply_word_verdict
	  (ctx, classify_delivered_value (gimple_assign_rhs1 (stmt),
					  ctx->contract_mask, ctx->region,
					  ctx->config_strict),
	   stmt);
      if (!DECL_EXTERNAL (base))
	return true;		/* TU data object */
    }
  return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
}

/* Return true if INSND is a CC-writing builtin whose only effects are
   the CC/lane-enable state and its SSA result, so it cannot touch a
   programmable constant register, deliver a word or write a hard LREG.
   This is an explicit list so that a new CC-writing builtin with other
   effects is not admitted by accident.  */

static bool
crossloop_cc_atom_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxcmp:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpsetcc:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
      return true;
    default:
      return false;
    }
}

/* Scan STMT in CTX.  With IN_CALLER, the contract call is allowed and
   vector dataflow is refused; the callee scan allows vector dataflow,
   which register allocation resolves around the pinned live ranges.
   Return false, recording the reason in CTX, if STMT may write a
   contract register.  */

bool
scan_stmt (scan_ctx *ctx, gimple *stmt, bool in_caller)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_COND
      || gimple_code (stmt) == GIMPLE_GOTO
      || gimple_code (stmt) == GIMPLE_NOP
      || gimple_code (stmt) == GIMPLE_PREDICT)
    return true;

  if (gasm *a = dyn_cast <gasm *> (stmt))
    return scan_asm (ctx, a);

  if (gcall *call = dyn_cast <gcall *> (stmt))
    {
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (insnd)
	{
	  if (insnd->sets_cc (call))
	    {
	      if (!ctx->cc_immaterial && !ctx->cc_ambient_ok)
		return scan_refuse (ctx,
				    (ctx->region
				     && riscv_tt_opt_cc_region_general > 0)
				    ? "crossloop-cc-ambient-unproven"
				    : in_caller
				    ? "crosscall-caller-cc-unproven"
				    : "crosscall-callee-cc-unproven", stmt);
	      /* With cc_immaterial, the hoisted programming runs before
		 this region and targets a constant register that no
		 CC write can reach.  With cc_ambient_ok, the loop only
		 preserves or narrows the entry lane enables, so the
		 hoisted all-lanes load writes a superset of the lanes.
		 Either way only known CC builtins are admitted.  */
	      if (!crossloop_cc_atom_p (insnd))
		return scan_refuse (ctx, "crossloop-cc-atom-unproven", stmt);
	      return true;
	    }
	  /* In a region scan, refuse builtins with side effects other
	     than Dst load/store and counter updates, as
	     allowed_dst_effect_p does in gimple-rvtt-invariant.cc.  */
	  if (ctx->region
	      && insnd->has_side_effects (call)
	      && insnd->id != rvtt_insn_data::sfpload
	      && insnd->id != rvtt_insn_data::sfpload_lv
	      && insnd->id != rvtt_insn_data::sfpstore
	      && insnd->id != rvtt_insn_data::ttincrwc
	      && insnd->id != rvtt_insn_data::ttdstface
		  /* sfppushc (0) does not set CC, so it is handled here;
		     it is allowed when cc_ambient_ok holds.  */
	      && !(ctx->cc_ambient_ok
		   && insnd->id == rvtt_insn_data::sfppushc)
		  /* These are handled by the switch below.  */
	      && insnd->id != rvtt_insn_data::ttreplay
	      && insnd->id != rvtt_insn_data::sfpreadlreg
	      && insnd->id != rvtt_insn_data::sfpwritelreg
	      && insnd->id != rvtt_insn_data::sfprawlreg_access)
	    return scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt);
	  switch (insnd->id)
	    {
	    case rvtt_insn_data::sfpreadlreg:
	    case rvtt_insn_data::sfpwritelreg:
	      {
		tree regno = gimple_call_arg
		  (call, insnd->id == rvtt_insn_data::sfpwritelreg ? 1 : 0);
		if (ctx->region)
		  {
		    /* Register allocation sees an explicit hard-LREG read
		       and works around it; only a write to a contract
		       register is a clobber.  */
		    if (insnd->id == rvtt_insn_data::sfpreadlreg
			&& TREE_CODE (regno) == INTEGER_CST)
		      return true;
		    if (TREE_CODE (regno) != INTEGER_CST
			|| ((ctx->contract_mask
			     >> (TREE_INT_CST_LOW (regno) & 0xf)) & 1))
		      return scan_refuse (ctx, "crosscall-callee-clobber",
					  stmt);
		    return true;
		  }
		if (TREE_CODE (regno) != INTEGER_CST
		    || ((ctx->contract_mask
			 >> (TREE_INT_CST_LOW (regno) & 0xf)) & 1))
		  return scan_refuse (ctx, "crosscall-callee-clobber", stmt);
		if (in_caller)
		  /* Any explicit LREG access in the caller's loop
		     implies a hand-written protocol that is not
		     analyzed.  */
		  return scan_refuse (ctx, "crosscall-caller-stmt-unproven",
				      stmt);
		return true;
	      }
	    case rvtt_insn_data::sfprawlreg_access:
	      {
		tree rel = gimple_call_arg (call, 0);
		tree wr = gimple_call_arg (call, 1);
		if (TREE_CODE (rel) != INTEGER_CST
		    || TREE_CODE (wr) != INTEGER_CST
		    || ((TREE_INT_CST_LOW (rel) | TREE_INT_CST_LOW (wr))
			& ctx->contract_mask))
		  return scan_refuse (ctx, "crosscall-callee-clobber", stmt);
		return true;
	      }
	    case rvtt_insn_data::ttreplay:
	      /* The replayed instructions are not known here.  */
	      return scan_refuse (ctx, "crosscall-caller-replay-unproven",
				  stmt);
	    default:
	      break;
	    }
	  if (in_caller && call_has_vector_dataflow_p (call))
	    /* The register of a caller vector value is chosen by the
	       allocator, which knows nothing of the contract.  */
	    return scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt);
	  /* Without vector dataflow, a builtin can only reach a hard
	     LREG through the explicit LREG builtins, the raw-access
	     marker or a replay launch, all handled above.  */
	  return true;
	}

      if (in_caller && ctx->callee_decl
	  && gimple_call_fndecl (call) == ctx->callee_decl)
	return true;		/* the contract call itself */
      if (gimple_call_internal_p (call))
	return gimple_vdef (call)
	  ? scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt) : true;
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	return true;		/* scalar compiler builtin */
      return scan_refuse (ctx, in_caller ? "crosscall-caller-stmt-unproven"
			  : "crosscall-callee-stmt-unproven", stmt);
    }

  if (is_gimple_assign (stmt))
    {
      if (in_caller && (vector_typed_p (gimple_assign_lhs (stmt))))
	return scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt);
      if (gimple_store_p (stmt))
	return scan_store (ctx, stmt);
      return true;
    }

  if (gimple_code (stmt) == GIMPLE_RETURN)
    return true;

  return scan_refuse (ctx, in_caller ? "crosscall-caller-stmt-unproven"
		      : "crosscall-callee-stmt-unproven", stmt);
}

/* Return true if a vector SSA value of FN is live anywhere in LOOP,
   where the caller's allocator could assign it a contract LREG.  A value
   is live at block B if some use is reachable from B without passing its
   definition.  */

static bool
vector_value_live_in_loop_p (function *fn, class loop *loop)
{
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, fn)
    {
      if (!vector_typed_p (name) || virtual_operand_p (name))
	continue;
      gimple *def = SSA_NAME_DEF_STMT (name);
      basic_block def_bb = def ? gimple_bb (def) : nullptr;

      /* Defined or used inside the loop?  */
      auto_bitmap reached;
      auto_vec<basic_block, 16> work;
      imm_use_iterator iter;
      gimple *use;
      bool hit = false;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	{
	  if (is_gimple_debug (use))
	    continue;
	  basic_block ub;
	  if (gphi *phi = dyn_cast <gphi *> (use))
	    {
	      ub = nullptr;
	      for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
		if (gimple_phi_arg_def (phi, i) == name)
		  {
		    basic_block src = gimple_phi_arg_edge (phi, i)->src;
		    if (flow_bb_inside_loop_p (loop, src))
		      hit = true;
		    else if (bitmap_set_bit (reached, src->index))
		      work.safe_push (src);
		  }
	    }
	  else
	    {
	      ub = gimple_bb (use);
	      if (!ub)
		continue;
	      if (flow_bb_inside_loop_p (loop, ub))
		hit = true;
	      else if (bitmap_set_bit (reached, ub->index))
		work.safe_push (ub);
	    }
	}
      if ((def_bb && flow_bb_inside_loop_p (loop, def_bb)) || hit)
	return true;
      /* Backward walk from the use blocks toward the def; touching a
	 loop block means the value is live through the loop.  */
      while (!work.is_empty ())
	{
	  basic_block b = work.pop ();
	  if (b == def_bb)
	    continue;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, b->preds)
	    {
	      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
		continue;
	      if (flow_bb_inside_loop_p (loop, e->src))
		return true;
	      if (bitmap_set_bit (reached, e->src->index))
		work.safe_push (e->src);
	    }
	}
    }
  return false;
}

/* A contract value discovered in the callee.  */

struct contract_entry
{
  gcall *load;			/* the prefix materialization	     */
  tree value;			/* its SSA lhs			     */
  int lreg;			/* the pinned hard LREG		     */
  gcall *root;			/* its SFPLOADI half, if chained     */
};

/* A config-prefix pair (-mtt-tensix-optimize-crosscall-config-prefix):
   a prefix load whose single use programs a programmable constant
   register 11..14 (never allocatable) with SFPCONFIG.  The pair is
   moved to the callers' loop preheaders, ahead of the contract loads
   because the SFPCONFIG source is pinned to L0, and deleted from the
   callee.  The caller scans then also refuse anything that may write
   the programmed register.  */

struct config_prefix_entry
{
  gcall *load;			/* the prefix materialization	     */
  gcall *write;			/* its single use: sfpwriteconfig_v  */
  unsigned dest;		/* the programmed register, 11..14   */
  gcall *root;			/* its SFPLOADI half, if chained     */
};

struct caller_plan
{
  cgraph_node *node;
  gcall *call_stmt;
  class loop *loop;		/* valid only while the caller's loop
				   state below is live		     */
  edge entry;
  unsigned lift_levels;		/* enclosing loops the placement walk
				   proved (config-prefix residency);
				   0 = the call's own loop entry     */
};

/* Discover the contract in FN.  Return true with CONTRACT filled (at
   least one entry) and the loop containing the uses in *CONSUMER_LOOP.  */

static bool
discover_contract (function *fn, auto_vec<contract_entry> *contract,
		   class loop **consumer_loop)
{
  class loop *uses_loop = nullptr;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (loop_outer (bb->loop_father))
	continue;		/* candidates live outside any loop */
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	  if (!load || !prefix_load_p (load))
	    continue;
	  tree lhs = gimple_call_lhs (load);
	  int lreg = -1;
	  bool pinned = true;
	  imm_use_iterator iter;
	  gimple *use;
	  FOR_EACH_IMM_USE_STMT (use, iter, lhs)
	    {
	      if (is_gimple_debug (use))
		continue;
	      gcall *ucall = dyn_cast <gcall *> (use);
	      const rvtt_insn_data *uinsnd
		= ucall ? rvtt_get_insn_data (ucall) : nullptr;
	      if (!uinsnd)
		{
		  pinned = false;
		  break;
		}
	      int this_reg = -1;
	      for (unsigned a = 0; a != gimple_call_num_args (ucall); ++a)
		if (gimple_call_arg (ucall, a) == lhs)
		  {
		    int r = pinned_lreg_operand (uinsnd, ucall, a);
		    if (r < 0 || (this_reg >= 0 && r != this_reg))
		      {
			this_reg = -1;
			break;
		      }
		    this_reg = r;
		  }
	      if (this_reg < 0 || (lreg >= 0 && this_reg != lreg))
		{
		  pinned = false;
		  break;
		}
	      lreg = this_reg;
	      basic_block ubb = gimple_bb (use);
	      class loop *ul = ubb ? ubb->loop_father : nullptr;
	      if (!ul || !loop_outer (ul)
		  || (uses_loop && ul != uses_loop))
		{
		  pinned = false;
		  break;
		}
	      uses_loop = ul;
	    }
	  if (!pinned || lreg < 0)
	    {
	      if (dump_file && lreg >= 0)
		crosscall_refuse ("crosscall-consumer-not-pinned", fn->decl,
				  load);
	      continue;
	    }
	  contract_entry e = { load, lhs, lreg, prefix_load_root (load) };
	  contract->safe_push (e);
	}
    }
  if (contract->is_empty ())
    return false;
  /* Register conflicts within the contract.  */
  unsigned mask = 0;
  for (const contract_entry &e : *contract)
    {
      if ((mask >> e.lreg) & 1)
	return crosscall_refuse ("crosscall-consumer-conflict",
				 fn->decl, e.load);
      mask |= 1u << e.lreg;
    }
  *consumer_loop = uses_loop;
  return true;
}

/* Collect the config-prefix pairs of FN that are not in CONTRACT into
   PAIRS.  A pair that does not qualify is not collected, and the callee
   check later refuses it as vector code outside the loop.  No other
   writer of a pair register may exist in the callee, or the hoisted
   programming would differ from the original after the first call.  */

static void
discover_config_prefix (function *fn,
			const auto_vec<contract_entry> &contract,
			auto_vec<config_prefix_entry> *pairs)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (loop_outer (bb->loop_father))
	continue;		/* pairs live outside any loop */
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	  if (!load || !prefix_load_p (load))
	    continue;
	  bool in_contract = false;
	  for (const contract_entry &e : contract)
	    if (e.load == load)
	      in_contract = true;
	  if (in_contract)
	    continue;
	  tree lhs = gimple_call_lhs (load);
	  use_operand_p use_p;
	  gimple *use_stmt;
	  if (!single_imm_use (lhs, &use_p, &use_stmt))
	    continue;
	  gcall *write = dyn_cast <gcall *> (use_stmt);
	  const rvtt_insn_data *uinsnd
	    = write ? rvtt_get_insn_data (write) : nullptr;
	  if (!uinsnd || uinsnd->id != rvtt_insn_data::sfpwriteconfig_v
	      || gimple_call_num_args (write) < 3
	      || gimple_call_arg (write, 0) != lhs
	      || gimple_call_lhs (write))
	    continue;
	  basic_block wbb = gimple_bb (write);
	  if (!wbb || wbb != bb)
	    continue;		/* pair stays block-local */
	  tree dest = gimple_call_arg (write, 2);
	  if (TREE_CODE (dest) != INTEGER_CST)
	    continue;
	  unsigned d = TREE_INT_CST_LOW (dest) & 0xf;
	  if (d < 11 || d > 14 || TREE_INT_CST_LOW (dest) > 14)
	    {
	      if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_CROSSCALL_CONFIG_DEST_UNPROVEN,
			       dump_file,
			       "crosscall-hoist: config pair unqualified "
			       "(crosscall-config-dest-unproven): ");
		  print_gimple_stmt (dump_file, write, 0, TDF_NONE);
		}
	      continue;
	    }
	  /* Chained materializations are not supported for config
	     pairs.  */
	  if (prefix_load_root (load))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "crosscall-hoist: config pair skipped "
			 "(crosscall-config-chained-materialization)\n");
	      continue;
	    }
	  config_prefix_entry p = { load, write, d, nullptr };
	  pairs->safe_push (p);
	}
    }
  if (pairs->is_empty ())
    return;

  /* No other writer of a pair register, including another pair.  */
  unsigned dest_mask = 0;
  bool unique = true;
  for (const config_prefix_entry &p : *pairs)
    {
      if ((dest_mask >> p.dest) & 1)
	unique = false;
      dest_mask |= 1u << p.dest;
    }
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 unique && !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	const rvtt_insn_data *insnd = call ? rvtt_get_insn_data (call)
	  : nullptr;
	if (!insnd)
	  continue;
	if (insnd->id == rvtt_insn_data::sfpwriteconfig_v)
	  {
	    bool is_pair = false;
	    for (const config_prefix_entry &p : *pairs)
	      if (p.write == call)
		is_pair = true;
	    if (is_pair)
	      continue;
	    tree dest = gimple_call_arg (call, 2);
	    if (TREE_CODE (dest) != INTEGER_CST
		|| ((dest_mask >> (TREE_INT_CST_LOW (dest) & 0xf)) & 1))
	      unique = false;
	  }
	else if (insnd->id == rvtt_insn_data::sfprawlreg_access)
	  {
	    tree rel = gimple_call_arg (call, 0);
	    tree wr = gimple_call_arg (call, 1);
	    if (TREE_CODE (rel) != INTEGER_CST
		|| TREE_CODE (wr) != INTEGER_CST
		|| ((TREE_INT_CST_LOW (rel) | TREE_INT_CST_LOW (wr))
		    & dest_mask))
	      unique = false;
	  }
      }
  if (!unique)
    {
      rvtt_refuse (RVTT_REF_CROSSCALL_CONFIG_WRITER_UNPROVEN, dump_file,
		   "crosscall-hoist: config pairs dropped "
		   "(crosscall-config-writer-unproven)\n");
      pairs->truncate (0);
      return;
    }

  /* Every pair must dominate every return, so the original write was
     unconditional.  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    for (const config_prefix_entry &p : *pairs)
      if (!dominated_by_p (CDI_DOMINATORS, e->src, gimple_bb (p.write)))
	{
	  rvtt_refuse (RVTT_REF_CROSSCALL_CONFIG_SHAPE_UNPROVEN, dump_file,
		       "crosscall-hoist: config pairs dropped "
		       "(crosscall-config-shape-unproven)\n");
	  pairs->truncate (0);
	  return;
	}

  if (dump_file)
    for (const config_prefix_entry &p : *pairs)
      {
	fprintf (dump_file,
		 "crosscall-hoist: config pair (creg %u) joins the "
		 "contract: ", p.dest);
	print_gimple_stmt (dump_file, p.load, 0, TDF_NONE);
      }
}

/* Return true if no recorded MOP template slot holds an SFPCONFIG word,
   which could rewrite a programmed constant register at a MOP launch;
   otherwise set *WHY.  */

static bool
mop_config_ok_p (const char **why)
{
  for (uint32_t word : tu_facts.slot_words)
    if ((word >> 24) == 0x91)
      {
	*why = "mop-template-config-word-unproven";
	return false;
      }
  return true;
}

/* Check the callee FN outside CONSUMER_LOOP with CTX: only CONTRACT and
   CONFIG may be vector statements there, every statement must pass
   scan_stmt, the extended live ranges must fit, and every contract load
   must dominate every return.  */

static bool
callee_body_ok_p (function *fn, const auto_vec<contract_entry> &contract,
		  const auto_vec<config_prefix_entry> &config,
		  class loop *consumer_loop, scan_ctx *ctx)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	if (vector_typed_p (gimple_phi_result (psi.phi ()))
	    && !flow_bb_inside_loop_p (consumer_loop, bb))
	  return crosscall_refuse ("crosscall-callee-vector-outside-loop",
				   fn->decl,
			 psi.phi ());
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;
	  bool is_contract_load = false;
	  for (const contract_entry &e : contract)
	    if (stmt == e.load || stmt == e.root)
	      is_contract_load = true;
	  for (const config_prefix_entry &p : config)
	    if (stmt == p.load || stmt == p.write || stmt == p.root)
	      is_contract_load = true;
	  if (!is_contract_load
	      && !flow_bb_inside_loop_p (consumer_loop, bb))
	    {
	      bool vec = false;
	      if (gcall *c = dyn_cast <gcall *> (stmt))
		vec = call_has_vector_dataflow_p (c);
	      else if (is_gimple_assign (stmt))
		vec = vector_typed_p (gimple_assign_lhs (stmt));
	      if (vec)
		return crosscall_refuse ("crosscall-callee-vector-outside-loop",
			       fn->decl, stmt);
	    }
	  if (!is_contract_load && !scan_stmt (ctx, stmt, /*in_caller=*/false))
	    return crosscall_refuse (ctx->why, fn->decl, ctx->why_stmt);
	}
    }

  /* The contract values must fit in the eight LREGs across the consumer
     loop.  With a config pair, reads of the programmed constant register
     do not occupy an allocatable LREG and are not counted; if that is
     ever wrong, the spill diagnosis reports it.  */
  auto_vec<gcall *> loads;
  for (const contract_entry &e : contract)
    loads.safe_push (e.load);
  if (!rvtt_pressure_loop_legal_p (consumer_loop, loads,
				   /*report=*/false,
				   /*cc_transients=*/false,
				   /*exempt_creg_reads=*/
				   !config.is_empty ()))
    return crosscall_refuse ("crosscall-callee-pressure", fn->decl, nullptr);

  /* The write-back at each return uses the load's value.  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    for (const contract_entry &c : contract)
      if (!dominated_by_p (CDI_DOMINATORS, e->src, gimple_bb (c.load)))
	return crosscall_refuse ("crosscall-callee-shape-unproven", fn->decl,
				 c.load);

  return true;
}

/* Prove the contract for CALLER, whose call to CALLEE_DECL is CALL_STMT,
   with contract registers CONTRACT_MASK and programmed constant registers
   CONFIG_MASK; PRGM_ONLY is set for rvtt_crosscall_prgm_hoist.  On success
   store the placement edge in *ENTRY_OUT and the number of enclosing loops
   it was lifted across in *LIFT_LEVELS_OUT.  Runs with CALLER as cfun.  */

static bool
prove_caller (cgraph_node *caller, gcall *call_stmt, tree callee_decl,
	      unsigned contract_mask, unsigned config_mask, edge *entry_out,
	      unsigned *lift_levels_out)
{
  *lift_levels_out = 0;
  function *fn = DECL_STRUCT_FUNCTION (caller->decl);
  basic_block bb = gimple_bb (call_stmt);
  if (!bb)
    return crosscall_refuse ("crosscall-caller-body-unavailable", caller->decl,
		   call_stmt);
  class loop *loop = bb->loop_father;
  if (!loop || !loop_outer (loop))
    return crosscall_refuse ("crosscall-caller-no-loop", caller->decl,
			     call_stmt);

  edge entry = rvtt_loop_entry_edge (loop);
  if (!entry || rvtt_preheader_insertion_blocked_p (entry))
    return crosscall_refuse ("crosscall-caller-preheader-unproven",
			     caller->decl,
		   call_stmt);

  scan_ctx ctx;
  ctx.contract_mask = contract_mask | config_mask;
  ctx.callee_decl = callee_decl;
  ctx.in_caller = true;
  ctx.config_strict = config_mask != 0;

  basic_block *body = get_loop_body (loop);
  bool ok = true;
  for (unsigned ix = 0; ok && ix != loop->num_nodes; ++ix)
    {
      for (gphi_iterator psi = gsi_start_phis (body[ix]);
	   ok && !gsi_end_p (psi); gsi_next (&psi))
	if (vector_typed_p (gimple_phi_result (psi.phi ()))
	    && !virtual_operand_p (gimple_phi_result (psi.phi ())))
	  ok = crosscall_refuse ("crosscall-caller-lreg-live", caller->decl,
		       psi.phi ());
      for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	   ok && !gsi_end_p (gsi); gsi_next (&gsi))
	if (!scan_stmt (&ctx, gsi_stmt (gsi), /*in_caller=*/true))
	  ok = crosscall_refuse (ctx.why, caller->decl, ctx.why_stmt);
    }
  free (body);
  if (!ok)
    return false;

  /* For config pairs and rvtt_crosscall_prgm_hoist, lift the placement
     across enclosing loops whose bodies pass the same scan; the argument
     for an unexecuted body applies to them as well.  The walk stops at
     the first level that fails.  */
  class loop *place_loop = loop;
  if (riscv_tt_opt_crosscall_config_prefix)
    for (class loop *outer = loop_outer (loop); outer && outer->num;
	 outer = loop_outer (outer))
      {
	edge oentry = rvtt_loop_entry_edge (outer);
	if (!oentry || rvtt_preheader_insertion_blocked_p (oentry))
	  break;
	basic_block *obody = get_loop_body (outer);
	bool level_ok = true;
	  bool saved_mop = ctx.saw_mop;	/* restored if the level fails */
	for (unsigned ix = 0; level_ok && ix != outer->num_nodes; ++ix)
	  {
	    if (flow_bb_inside_loop_p (place_loop, obody[ix]))
		continue;		/* already scanned */
	    for (gphi_iterator psi = gsi_start_phis (obody[ix]);
		 level_ok && !gsi_end_p (psi); gsi_next (&psi))
	      if (vector_typed_p (gimple_phi_result (psi.phi ()))
		  && !virtual_operand_p (gimple_phi_result (psi.phi ())))
		level_ok = false;
	    for (gimple_stmt_iterator gsi = gsi_start_bb (obody[ix]);
		 level_ok && !gsi_end_p (gsi); gsi_next (&gsi))
	      if (!scan_stmt (&ctx, gsi_stmt (gsi), /*in_caller=*/true))
		level_ok = false;
	  }
	free (obody);
	if (!level_ok || vector_value_live_in_loop_p (fn, outer))
	  {
	    ctx.saw_mop = saved_mop;
	    if (dump_file)
	      fprintf (dump_file,
		       "crosscall-hoist: residency walk stops at loop bb %d"
		       " (%s)\n", outer->header->index,
		       level_ok ? "crosscall-caller-lreg-live"
		       : (ctx.why ? ctx.why : "?"));
	    break;
	  }
	place_loop = outer;
	entry = oentry;
	++*lift_levels_out;
	if (dump_file)
	  fprintf (dump_file,
		   "crosscall-hoist: contract placement lifted to enclosing"
		   " loop bb %d entry\n", outer->header->index);
      }

  if (ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_contract_ok_p (contract_mask | config_mask, &why)
	  || (config_mask && !mop_config_ok_p (&why)))
	{
	  crosscall_refuse ("crosscall-caller-mop-slot-unproven", caller->decl,
		  call_stmt);
	  if (dump_file && why)
	    fprintf (dump_file, "crosscall-hoist:   (%s)\n", why);
	  return false;
	}
    }

  if (vector_value_live_in_loop_p (fn, place_loop))
    return crosscall_refuse ("crosscall-caller-lreg-live", caller->decl,
			     call_stmt);

  /* An explicit read of a contract register outside the loop could
     observe the hoisted write when the loop body does not run.  */
  basic_block obb;
  FOR_EACH_BB_FN (obb, fn)
    {
      if (flow_bb_inside_loop_p (place_loop, obb))
	continue;
      for (gimple_stmt_iterator gsi = gsi_start_bb (obb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	  const rvtt_insn_data *insnd
	    = call ? rvtt_get_insn_data (call) : nullptr;
	  if (!insnd)
	    continue;
	  if (insnd->id == rvtt_insn_data::sfpreadlreg)
	    {
	      tree regno = gimple_call_arg (call, 0);
	      if (TREE_CODE (regno) != INTEGER_CST
		  || (((contract_mask | config_mask)
		       >> (TREE_INT_CST_LOW (regno) & 0xf)) & 1))
		return crosscall_refuse ("crosscall-caller-foreign-contract",
			       caller->decl, call);
	    }
	  else if (insnd->id == rvtt_insn_data::sfprawlreg_access)
	    {
	      tree rel = gimple_call_arg (call, 0);
	      if (TREE_CODE (rel) != INTEGER_CST
		  || (TREE_INT_CST_LOW (rel)
		      & (contract_mask | config_mask)))
		return crosscall_refuse ("crosscall-caller-foreign-contract",
			       caller->decl, call);
	    }
	}
    }

  *entry_out = entry;
  return true;
}

/* Insert STMT at the end of the preheader block PH, before a
   block-terminating statement if there is one.  */

void
insert_in_preheader (basic_block ph, gimple *stmt)
{
  gimple_stmt_iterator gsi = gsi_last_bb (ph);
  if (gsi_end_p (gsi) || !stmt_ends_bb_p (gsi_stmt (gsi)))
    gsi_insert_after (&gsi, stmt, GSI_NEW_STMT);
  else
    gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
}

/* Emit the CONFIG pairs and then the CONTRACT loads with their
   sfpwritelreg in the preheader of CALLER on ENTRY, creating the cgraph
   edges for the new calls.  */

static void
commit_caller (cgraph_node *caller, edge entry,
	       const auto_vec<contract_entry> &contract,
	       const auto_vec<config_prefix_entry> &config)
{
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);
  basic_block ph = rvtt_commit_hoist_preheader (entry);
  /* Config pairs first, so their L0 temporary does not overlap the
     pinned contract ranges.  */
  for (const config_prefix_entry &p : config)
    {
      auto_vec<gcall *, 4> clones;
      tree val = clone_prefix_load (p.root, p.load, &clones);
      gcall *write = gimple_build_call
	(gimple_call_fndecl (p.write), 3, val, build_int_cst (unsigned_type_node, 0),
	 build_int_cst (integer_type_node, (int) p.dest));
      for (gcall *load : clones)
	{
	  insert_in_preheader (ph, load);
	  caller->create_edge (cgraph_node::get_create
				 (gimple_call_fndecl (load)), load, ph->count);
	}
      insert_in_preheader (ph, write);
      caller->create_edge (cgraph_node::get_create
			     (gimple_call_fndecl (p.write)), write,
			   ph->count);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: placed config pair (creg %u) in %s "
		   "preheader bb %d: ",
		   p.dest, caller->dump_name (), ph->index);
	  print_gimple_stmt (dump_file, clones.last (), 0, TDF_NONE);
	}
    }
  for (const contract_entry &e : contract)
    {
      auto_vec<gcall *, 4> clones;
      tree val = clone_prefix_load (e.root, e.load, &clones);
      /* No location: the original belongs to the callee's BLOCK
	 tree.  */
      gcall *write = gimple_build_call
	(write_d->decl, 2, val, build_int_cst (integer_type_node, e.lreg));
      /* The caller's inline transform has not run yet and expects a
	 cgraph edge for every call.  */
      for (gcall *load : clones)
	{
	  insert_in_preheader (ph, load);
	  caller->create_edge (cgraph_node::get_create
				 (gimple_call_fndecl (load)), load, ph->count);
	}
      insert_in_preheader (ph, write);
      caller->create_edge (cgraph_node::get_create (write_d->decl), write,
			   ph->count);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: placed contract materialization "
		   "(L%d) in %s preheader bb %d: ",
		   e.lreg, caller->dump_name (), ph->index);
	  print_gimple_stmt (dump_file, clones.last (), 0, TDF_NONE);
	}
    }
  update_ssa (TODO_update_ssa_only_virtuals);
}

/* In the callee FN, delete each CONFIG pair, replace each CONTRACT load
   with an sfpreadlreg of its register, and write each contract value back
   to its register before every return.  */

static void
commit_callee (function *fn, const auto_vec<contract_entry> &contract,
	       const auto_vec<config_prefix_entry> &config)
{
  const rvtt_insn_data *read_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);

  for (const config_prefix_entry &p : config)
    {
      gimple_stmt_iterator wsi = gsi_for_stmt (p.write);
      if (tree vdef = gimple_vdef (p.write))
	if (TREE_CODE (vdef) == SSA_NAME)
	  unlink_stmt_vdef (p.write);
      gsi_remove (&wsi, true);
      gimple_stmt_iterator lsi = gsi_for_stmt (p.load);
      if (tree vdef = gimple_vdef (p.load))
	if (TREE_CODE (vdef) == SSA_NAME)
	  unlink_stmt_vdef (p.load);
      tree lhs = gimple_call_lhs (p.load);
      gsi_remove (&lsi, true);
      if (lhs && TREE_CODE (lhs) == SSA_NAME)
	release_ssa_name (lhs);
      if (dump_file)
	fprintf (dump_file,
		 "crosscall-hoist: config pair (creg %u) removed from %s "
		 "(programmed in the caller preheaders)\n",
		 p.dest, IDENTIFIER_POINTER (DECL_NAME (fn->decl)));
    }

  for (const contract_entry &e : contract)
    {
      gcall *read = gimple_build_call
	(read_d->decl, 1, build_int_cst (integer_type_node, e.lreg));
      gimple_call_set_lhs (read, e.value);
      gimple_set_location (read, gimple_location (e.load));
      gimple_stmt_iterator gsi = gsi_for_stmt (e.load);
      if (tree vdef = gimple_vdef (e.load))
	if (TREE_CODE (vdef) == SSA_NAME)
	  unlink_stmt_vdef (e.load);
      gsi_replace (&gsi, read, false);
      /* Remove the SFPLOADI half of a chained load too.  */
      if (e.root)
	{
	  gimple_stmt_iterator rgsi = gsi_for_stmt (e.root);
	  if (tree vdef = gimple_vdef (e.root))
	    if (TREE_CODE (vdef) == SSA_NAME)
	      unlink_stmt_vdef (e.root);
	  gsi_remove (&rgsi, true);
	  release_defs (e.root);
	}
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: contract read (L%d) replaces prefix "
		   "materialization in %s: ",
		   e.lreg, IDENTIFIER_POINTER (DECL_NAME (fn->decl)));
	  print_gimple_stmt (dump_file, read, 0, TDF_NONE);
	}
    }

  /* Keep every contract value live in its register to every return.  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    {
      gimple_stmt_iterator gsi = gsi_last_bb (e->src);
      if (gsi_end_p (gsi) || gimple_code (gsi_stmt (gsi)) != GIMPLE_RETURN)
	continue;
      for (const contract_entry &c : contract)
	{
	  gcall *write = gimple_build_call
	    (write_d->decl, 2, c.value,
	     build_int_cst (integer_type_node, c.lreg));
	  gsi_insert_before (&gsi, write, GSI_SAME_STMT);
	}
    }
}

/* Hoist the contract of the callee FN into all its callers.  Return
   true if anything changed.  */

static bool
transform (function *fn)
{
  cgraph_node *cn = cgraph_node::get (fn->decl);
  if (!cn)
    return false;

  auto_vec<contract_entry> contract;
  class loop *consumer_loop = nullptr;
  if (!discover_contract (fn, &contract, &consumer_loop))
    return false;		/* no candidate: silent */

  unsigned contract_mask = 0;
  for (const contract_entry &e : contract)
    contract_mask |= 1u << e.lreg;

  if (dump_file)
    fprintf (dump_file,
	     "crosscall-hoist: %s: contract candidate, %u values, "
	     "LREG mask %#x\n",
	     IDENTIFIER_POINTER (DECL_NAME (fn->decl)),
	     contract.length (), contract_mask);

  auto_vec<config_prefix_entry> config;
  if (riscv_tt_opt_crosscall_config_prefix)
    discover_config_prefix (fn, contract, &config);
  unsigned config_mask = 0;
  for (const config_prefix_entry &p : config)
    config_mask |= 1u << p.dest;

  /* Callee-side proofs.  */
  scan_ctx callee_ctx;
  callee_ctx.contract_mask = contract_mask;
  callee_ctx.callee_decl = NULL_TREE;
  callee_ctx.in_caller = false;
  if (!callee_body_ok_p (fn, contract, config, consumer_loop, &callee_ctx))
    return false;
  if (callee_ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_contract_ok_p (contract_mask | config_mask, &why)
	  || (config_mask && !mop_config_ok_p (&why)))
	return crosscall_refuse ("crosscall-caller-mop-slot-unproven", fn->decl,
		       nullptr);
    }

  /* The callee must be a non-address-taken definition with no aliases,
     thunks or clones, and with callers.  */
  if (!cn->definition || cn->address_taken || cn->alias || cn->thunk
      || cn->clones || !cn->callers)
    return crosscall_refuse ("crosscall-caller-body-unavailable", fn->decl,
			     nullptr);

  /* One call site per caller; collect and prove each caller.  */
  auto_vec<caller_plan> plans;
  for (cgraph_edge *e = cn->callers; e; e = e->next_caller)
    {
      if (e->caller == cn)
	return crosscall_refuse ("crosscall-caller-body-unavailable", fn->decl,
		       nullptr);
      for (const caller_plan &p : plans)
	if (p.node == e->caller)
	  return crosscall_refuse ("crosscall-caller-multi-site",
				   e->caller->decl,
			 nullptr);
      if (!e->caller->definition || !e->caller->has_gimple_body_p ()
	  || !e->call_stmt)
	return crosscall_refuse ("crosscall-caller-body-unavailable",
		       e->caller->decl, nullptr);
      /* The MOP template audit only covers callers in the executable
	 closure of the TU.  */
      cgraph_node *ccheck = e->caller->inlined_to
	? e->caller->inlined_to : e->caller;
      if (tu_facts.executable && !tu_facts.executable->contains (ccheck))
	return crosscall_refuse ("crosscall-caller-unrooted", ccheck->decl,
				 nullptr);
      function *cfn = DECL_STRUCT_FUNCTION (e->caller->decl);
      if (!cfn || !cfn->cfg)
	return crosscall_refuse ("crosscall-caller-body-unavailable",
		       e->caller->decl, nullptr);
      caller_plan p = { e->caller, e->call_stmt, nullptr, nullptr, 0 };
      plans.safe_push (p);
    }

  /* Prove every caller before changing anything.  Loop structures are
     recomputed at commit time; the bodies have not changed since.  */
  for (caller_plan &p : plans)
    {
      push_cfun (DECL_STRUCT_FUNCTION (p.node->decl));
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
      bool dom = dom_info_available_p (CDI_DOMINATORS);
      if (!dom)
	calculate_dominance_info (CDI_DOMINATORS);
      bool ok = prove_caller (p.node, p.call_stmt, fn->decl, contract_mask,
			      config_mask, &p.entry, &p.lift_levels);
      if (!dom)
	free_dominance_info (CDI_DOMINATORS);
      loop_optimizer_finalize ();
      pop_cfun ();
      if (!ok)
	return false;
    }

  /* Commit the callers, then the callee.  */
  for (caller_plan &p : plans)
    {
      push_cfun (DECL_STRUCT_FUNCTION (p.node->decl));
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
      class loop *loop = gimple_bb (p.call_stmt)->loop_father;
      for (unsigned l = 0; l != p.lift_levels; ++l)
	loop = loop_outer (loop);
      edge entry = rvtt_loop_entry_edge (loop);
      gcc_assert (entry);
      commit_caller (p.node, entry, contract, config);
      rvtt_ipa_summary_invalidate (DECL_STRUCT_FUNCTION (p.node->decl));
      loop_optimizer_finalize ();
      pop_cfun ();
    }

  commit_callee (fn, contract, config);
  rvtt_ipa_summary_invalidate (fn);

  if (dump_file)
    fprintf (dump_file,
	     "crosscall-hoist: hoisted %u contract materializations%s from "
	     "%s into %u caller(s)\n",
	     contract.length (),
	     config.is_empty () ? "" : " (+config prefix)",
	     IDENTIFIER_POINTER (DECL_NAME (fn->decl)), plans.length ());
  return true;
}

const pass_data pass_data_rvtt_crosscall =
{
  GIMPLE_PASS, /* type */
  "rvtt_crosscall", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa | PROP_cfg, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_crosscall : public gimple_opt_pass
{
public:
  pass_rvtt_crosscall (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_crosscall, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_crosscall_hoist;
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	rvtt_refuse (RVTT_REF_QSR_UNPROVEN, dump_file,
		     "crosscall-hoist: refused (qsr-unproven)\n");
	return 0;
      }
    /* Compute the TU facts while every caller body is still
       GIMPLE.  */
    compute_tu_facts ();
    /* Dump the cross-call CC carry fact from the IPA summary; it
       does not affect this pass.  */
    if (dump_file)
      {
	cgraph_node *self = cgraph_node::get (fn->decl);
	fprintf (dump_file, "ipa-summary: cc-carry %s: %s\n",
		 self ? self->dump_name () : "?",
		 self && rvtt_ipa_cc_ambient_preserving_p (self)
		 ? "ambient-preserving" : "unproven");
      }
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    if (!dom_info_available_p (CDI_DOMINATORS))
      calculate_dominance_info (CDI_DOMINATORS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

/* Create the cross-call hoist pass for CTXT.  The init and ADDR_MOD
   hoist services are in gimple-rvtt-crosscall-hoist.cc.  */

gimple_opt_pass *
make_pass_rvtt_crosscall (gcc::context *ctxt)
{
  return new pass_rvtt_crosscall (ctxt);
}
