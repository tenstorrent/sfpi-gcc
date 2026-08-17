/* Allocate programmable constant registers to loop-invariant SFPU
   immediates (M3).
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

/* SFPCONFIG destinations 12..14 (sfpi CREG_IDX_PRGM1..3, vConstFloatPrgm0..2)
   hold programmable constants readable as constant registers with zero
   allocatable-LREG pressure.  A loop-invariant float immediate that the
   invariant-loadi pass had to leave in a loop by LREG pressure -- by this
   point folded into a mad-family immediate operation (SFPADDI) -- can
   instead be programmed once into a free PRGM register on the loop entry
   edge and read back as a constant-register operand.  Rewriting the
   immediate form to the register form additionally re-offers the pair to
   the existing mul+add->mad combine (which runs after this pass), deleting
   one issue slot AND one result-latency stall per iteration on the exp
   shape.

   Admitted class (deliberately narrow): a fusion-enabling SFPADDI whose
   vector operand is a single-use SFPMUL in the same loop, plain-add mod,
   all-constant scalar operands, canonical instruction-buffer operand.
   The pure in-loop-loadi class (design D1 candidate (a)) refuses pending
   its own benefit discipline.

   Freedom proof for a PRGM register (the D2 region-scoped opacity
   extension).  PRGM registers are persistent global machine state, so the
   proof is TU-wide and cached at the first execution (when every function
   body in the translation unit is still in gimple):
   - every raw `.ttinsn' word in the TU must either sit inside a declared
     effects region (below) or decode through the audited raw-word table:
     TENSIX NOP, the sync family (0xA0-0xA7), the thread-config family
     (0xB0-0xB8), CLEARDVALID/SETRWC, SFPLOADI with an architecturally
     verified allocatable destination, and SFPCONFIG with a decoded
     constant destination (which CLAIMS that destination).  Anything else
     -- MOP (expands runtime-configured instruction words), TTREPLAY, any
     raw SFPU-class word, a non-literal operand, a non-.ttinsn template --
     refuses the whole TU byte-identically;
   - `__builtin_rvtt_ttregion_begin (config_write_mask, 0)' ...
     `__builtin_rvtt_ttregion_end ()' bracket a raw region with a TRUSTED
     typed effects declaration (the sfprawlreg_access discipline): the
     region writes exactly the SFPCONFIG destinations in the mask, never
     LaneConfig (bit 15 must be clear), and neither reads nor writes any
     other PRGM register.  Both markers must sit in the same basic block
     as the statements they cover.  CRAQ is the check;
   - user `vConstFloatPrgm' assignments (sfpwriteconfig_v) claim their
     constant destination; a non-constant destination refuses;
   - an indirect call or a call to a function with no body in this TU
     refuses (it could contain undeclared Tensix code); defined functions
     are scanned themselves and ordinary scalar compiler builtins are
     transparent;
   - the programming point must run under the all-lanes CC state: the sfpi
     structured-CC model makes function entry all-lanes, and any CC-writing
     statement anywhere in the function refuses (cc-region-unproven), so
     the entry state provably reaches the loop entry edge.

   Refusals never mutate the CFG: flag-off and every refusal path are
   byte-identical.  */

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
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-macro-ownership.h"

namespace {

/* ------------------------------------------------------------------ */
/* TU-wide PRGM freedom facts, computed once.			      */

struct prgm_tu_facts
{
  bool computed = false;
  bool refused = false;
  const char *reason = nullptr;
  /* SFPCONFIG destinations 0..15 written anywhere in the TU.  */
  unsigned claimed = 0;
};

static prgm_tu_facts tu_facts;

/* Audited raw-word capability table (BH/WH encodings).  Returns false
   for any word whose PRGM/LaneConfig/CC effect is not architecturally
   pinned; a decoded SFPCONFIG claims its destination in *CLAIMED.  */

static bool
audited_raw_word_p (uint32_t word, unsigned *claimed, const char **why)
{
  unsigned opcode = word >> 24;
  if (opcode == 0x00)		/* TENSIX NOP */
    return true;
  if (opcode >= 0xA0 && opcode <= 0xA7)	/* sync family */
    return true;
  if (opcode >= 0xB0 && opcode <= 0xB8)	/* thread-config family (SETC16) */
    return true;
  if (opcode == 0x36 || opcode == 0x37)	/* CLEARDVALID / SETRWC */
    return true;
  if (opcode == 0x71)		/* SFPLOADI: dest architecturally < 8 */
    {
      if (((word >> 20) & 0xf) < 8)
	return true;
      *why = "raw SFPLOADI with non-allocatable destination";
      return false;
    }
  if (opcode == 0x91)		/* SFPCONFIG: claim the decoded dest */
    {
      unsigned dest = (word >> 4) & 0xf;
      if (dest == 15)
	{
	  *why = "raw SFPCONFIG writes LaneConfig";
	  return false;
	}
      *claimed |= 1u << dest;
      return true;
    }
  *why = "unaudited raw opcode";
  return false;
}

/* A gimple_asm whose template is empty emits nothing; the single
   canonical raw form is one `.ttinsn' directive with one constant
   input.  Everything else refuses.  */

static bool
scan_raw_asm (gasm *stmt, unsigned *claimed, const char **why)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return true;		/* pure barrier, no instruction */
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
  return audited_raw_word_p ((uint32_t) TREE_INT_CST_LOW (op), claimed, why);
}

/* Scan one function body.  Returns false (with *WHY set) on refusal.  */

static bool
scan_function_body (function *fn, unsigned *claimed, const char **why)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      bool declared = false;
      unsigned decl_mask = 0;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;

	  if (gasm *a = dyn_cast <gasm *> (stmt))
	    {
	      if (declared)
		continue;	/* covered by the trusted declaration */
	      if (!scan_raw_asm (a, claimed, why))
		return false;
	      continue;
	    }

	  if (!is_gimple_call (stmt))
	    continue;

	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  if (insnd)
	    {
	      gcall *call = as_a <gcall *> (stmt);
	      if (insnd->id == rvtt_insn_data::ttregion_begin)
		{
		  tree mask = gimple_call_arg (call, 0);
		  if (declared || TREE_CODE (mask) != INTEGER_CST
		      || (TREE_INT_CST_LOW (mask) & (1u << 15)))
		    {
		      *why = "malformed effects declaration";
		      return false;
		    }
		  declared = true;
		  decl_mask = TREE_INT_CST_LOW (mask);
		  *claimed |= decl_mask;
		}
	      else if (insnd->id == rvtt_insn_data::ttregion_end)
		{
		  if (!declared)
		    {
		      *why = "unmatched effects declaration end";
		      return false;
		    }
		  declared = false;
		}
	      else if (insnd->id == rvtt_insn_data::sfpwriteconfig_v)
		{
		  tree dest = gimple_call_arg (call, 1);
		  if (TREE_CODE (dest) != INTEGER_CST)
		    {
		      *why = "non-constant user SFPCONFIG destination";
		      return false;
		    }
		  unsigned d = TREE_INT_CST_LOW (dest) & 0xf;
		  if (d == 15)
		    {
		      *why = "user SFPCONFIG writes LaneConfig";
		      return false;
		    }
		  *claimed |= 1u << d;
		}
	      continue;		/* typed builtins are transparent */
	    }

	  if (gimple_call_internal_p (stmt))
	    continue;
	  tree fndecl = gimple_call_fndecl (stmt);
	  if (!fndecl)
	    {
	      *why = "indirect call";
	      return false;
	    }
	  if (fndecl_built_in_p (fndecl))
	    continue;		/* scalar compiler builtin */
	  cgraph_node *cn = cgraph_node::get (fndecl);
	  if (!cn || !cn->definition)
	    {
	      *why = "call to a function outside this translation unit";
	      return false;
	    }
	  /* Defined in this TU: its body is scanned itself.  */
	}
      if (declared)
	{
	  *why = "effects declaration not closed in its block";
	  return false;
	}
    }
  return true;
}

/* Compute (once) the TU facts.  Runs at the first execution of this
   pass, i.e. before any other function's gimple body has been released;
   functions synthesized after that point are compiler-generated scalar
   code and emit no Tensix instructions.  */

static const prgm_tu_facts &
tu_prgm_facts ()
{
  if (tu_facts.computed)
    return tu_facts;
  tu_facts.computed = true;

  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;		/* thunks/aliases carry no code */
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      const char *why = nullptr;
      if (!ofn || !ofn->cfg)
	{
	  /* A defined body this pass cannot walk must refuse, never be
	     presumed clean.  */
	  tu_facts.refused = true;
	  tu_facts.reason = "function body unavailable to the scan";
	  break;
	}
      if (!scan_function_body (ofn, &tu_facts.claimed, &why))
	{
	  tu_facts.refused = true;
	  tu_facts.reason = why;
	  break;
	}
    }
  return tu_facts;
}

/* ------------------------------------------------------------------ */
/* Per-function transform.					      */

/* PRGM hard-LREG indices (sfpi CREG_IDX_PRGM1..3 == SFPCONFIG dests).
   Index 11 (PRGM0) is the architectural -1.0 special case and is never
   allocated.  */
static const unsigned prgm_regs[] = { 12, 13, 14 };

static bool
canonical_buffer_arg_p (tree addr)
{
  if (integer_zerop (addr))
    return true;
  STRIP_NOPS (addr);
  if (TREE_CODE (addr) != ADDR_EXPR)
    return false;
  tree decl = TREE_OPERAND (addr, 0);
  return VAR_P (decl)
    && DECL_EXTERNAL (decl)
    && TREE_PUBLIC (decl)
    && DECL_ASSEMBLER_NAME (decl)
    && !strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)),
		"__instrn_buffer");
}

/* The fusion-enabling candidate: LHS = sfpaddi (buf, MUL, imm, 0, 0, 0)
   where MUL = sfpmul (a, b, 0) in the same loop with the addi as its
   only use.  */

struct candidate
{
  gcall *addi;
  gcall *mul;
  unsigned value;		/* fp32 bits of the bf16 immediate */
  class loop *loop;
  edge entry;
};

static bool
single_nondebug_use_p (tree value, gimple *expected)
{
  imm_use_iterator iter;
  use_operand_p use_p;
  gimple *seen = nullptr;
  FOR_EACH_IMM_USE_FAST (use_p, iter, value)
    {
      gimple *use = USE_STMT (use_p);
      if (is_gimple_debug (use))
	continue;
      if (seen && use != seen)
	return false;
      seen = use;
    }
  return seen == expected;
}

static bool
fusion_candidate_p (gcall *call, class loop *loop, candidate *out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfpaddi)
    return false;
  if (!gimple_call_lhs (call)
      || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
      || !canonical_buffer_arg_p (gimple_call_arg (call, 0)))
    return false;
  for (unsigned ix = 2; ix != gimple_call_num_args (call); ++ix)
    if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
      return false;
  /* Plain-add form only: synthesized id/var fields and mod all zero.  */
  for (unsigned ix = 3; ix != gimple_call_num_args (call); ++ix)
    if (!integer_zerop (gimple_call_arg (call, ix)))
      return false;

  tree src = gimple_call_arg (call, 1);
  if (TREE_CODE (src) != SSA_NAME)
    return false;
  gcall *mul = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!mul)
    return false;
  const rvtt_insn_data *muld = rvtt_get_insn_data (mul);
  if (!muld || muld->id != rvtt_insn_data::sfpmul
      || !integer_zerop (gimple_call_arg (mul, 2))
      || !gimple_bb (mul)
      || !flow_bb_inside_loop_p (loop, gimple_bb (mul))
      || !single_nondebug_use_p (src, call))
    return false;

  out->addi = call;
  out->mul = mul;
  out->value = (TREE_INT_CST_LOW (gimple_call_arg (call, 2)) & 0xffff) << 16;
  out->loop = loop;
  return true;
}

/* Any CC-writing statement in FN defeats the all-lanes proof for the
   programming point.  */

static bool
function_writes_cc_p (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (gsi_stmt (gsi));
	if (insnd && insnd->sets_cc (as_a <gcall *> (gsi_stmt (gsi))))
	  return true;
      }
  return false;
}

static bool
transform (function *fn)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  auto_vec<candidate> candidates;
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (!loop->num)
	continue;
      /* No zero-trip proof is needed at this late pipeline position:
	 the programming point sits on the loop entry edge, whose
	 destination is the loop header, so control reaching it executes
	 the header (and every candidate block, by the
	 executes-every-entered-iteration proof below) at least once --
	 the SFPCONFIG write is never speculated relative to the loop.
	 (The invariant pass's first-header-test fold targets the
	 pre-rotation shape and cannot see the rotated do-while form
	 this pass runs on.)  */
      edge entry = rvtt_loop_entry_edge (loop);
      const char *why
	= !entry ? "no-single-entry"
	: rvtt_loop_hoist_region_opaque_p (loop, entry) ? "opaque-hoist-region"
	: rvtt_preheader_insertion_blocked_p (entry) ? "preheader-blocked"
	: rvtt_loop_has_sfpu_barrier_p (loop) ? "sfpu-barrier"
	: nullptr;
      if (why)
	{
	  if (dump_file)
	    fprintf (dump_file, "prgm-const: loop bb %d refused (%s)\n",
		     loop->header->index, why);
	  continue;
	}

      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
	    continue;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      candidate c;
	      if (is_a <gcall *> (gsi_stmt (gsi))
		  && fusion_candidate_p (as_a <gcall *> (gsi_stmt (gsi)),
					 loop, &c))
		{
		  c.entry = entry;
		  candidates.safe_push (c);
		}
	    }
	}
      free (body);
    }

  if (candidates.is_empty ())
    return false;

  /* The freedom proof gates every allocation.  */
  const prgm_tu_facts &facts = tu_prgm_facts ();
  if (facts.refused)
    {
      if (dump_file)
	fprintf (dump_file,
		 "prgm-const: refused (opaque-region-undeclared): %s\n",
		 facts.reason);
      return false;
    }

  if (function_writes_cc_p (fn))
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: refused (cc-region-unproven)\n");
      return false;
    }

  unsigned claimed = facts.claimed;
  bool changed = false;
  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *add_d = rvtt_get_insn_data (rvtt_insn_data::sfpadd);

  for (candidate &c : candidates)
    {
      unsigned prgm = 0;
      for (unsigned reg : prgm_regs)
	if (!(claimed & (1u << reg)))
	  {
	    prgm = reg;
	    break;
	  }
      if (!prgm)
	{
	  if (dump_file)
	    fprintf (dump_file, "prgm-const: refused (prgm-exhausted): ");
	  if (dump_file)
	    print_gimple_stmt (dump_file, c.addi, 0);
	  continue;
	}
      claimed |= 1u << prgm;

      /* Program the constant on the loop entry edge.  */
      basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
      tree vec_type = TREE_TYPE (gimple_call_lhs (c.addi));
      gcall *load = gimple_build_call
	(xloadi_d->decl, 5, null_pointer_node,
	 build_int_cst (unsigned_type_node, c.value),
	 build_int_cst (unsigned_type_node, 0),
	 build_int_cst (unsigned_type_node, 0),
	 build_int_cst (integer_type_node, -32));
      tree staged = make_ssa_name (vec_type);
      gimple_call_set_lhs (load, staged);
      gcall *wrcfg = gimple_build_call
	(wrcfg_d->decl, 2, staged, build_int_cst (unsigned_type_node, prgm));

      gimple_stmt_iterator phg = gsi_last_bb (preheader);
      if (gsi_end_p (phg) || !stmt_ends_bb_p (gsi_stmt (phg)))
	{
	  gsi_insert_after (&phg, wrcfg, GSI_NEW_STMT);
	  gsi_insert_before (&phg, load, GSI_SAME_STMT);
	}
      else
	{
	  gsi_insert_before (&phg, wrcfg, GSI_SAME_STMT);
	  gsi_insert_before (&phg, load, GSI_SAME_STMT);
	}

      /* Read it back as a constant register and re-offer the pair to
	 the mad combine (which runs after this pass).  */
      gimple_stmt_iterator gsi = gsi_for_stmt (c.addi);
      gcall *read = gimple_build_call
	(readlreg_d->decl, 1, build_int_cst (unsigned_type_node, prgm));
      tree creg = make_ssa_name (vec_type);
      gimple_call_set_lhs (read, creg);
      gsi_insert_before (&gsi, read, GSI_SAME_STMT);

      gcall *add = gimple_build_call
	(add_d->decl, 3, gimple_call_arg (c.addi, 1), creg,
	 build_int_cst (unsigned_type_node, 0));
      gimple_call_set_lhs (add, gimple_call_lhs (c.addi));
      gsi_replace (&gsi, add, false);

      changed = true;
      if (dump_file)
	fprintf (dump_file,
		 "prgm-const: allocated PRGM L%u for invariant immediate "
		 "0x%08x (loop header bb %d)\n",
		 prgm, c.value, c.loop->header->index);
    }
  return changed;
}

const pass_data pass_data_rvtt_prgm_const =
{
  GIMPLE_PASS, /* type */
  "rvtt_prgm_const", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
    return TARGET_XTT_TENSIX && riscv_tt_opt_prgm_const;
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "prgm-const: refused (qsr-unproven)\n");
	return 0;
      }
    /* Compute the TU facts EAGERLY on the first function through this
       pass: at that moment every other function body in the TU is
       still in gimple.  Waiting for a function with candidates would
       find earlier functions' bodies already released.  */
    tu_prgm_facts ();
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_prgm_const (gcc::context *ctxt)
{
  return new pass_rvtt_prgm_const (ctxt);
}
