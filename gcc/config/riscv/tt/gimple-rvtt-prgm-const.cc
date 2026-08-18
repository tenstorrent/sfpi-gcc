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
   - every raw `.ttinsn' word in the TU must decode through the audited
     raw-word table (rvtt-mop-derive.cc rvtt_mop_audited_word_p):
     TENSIX NOP, the sync family (0xA0-0xA7), the thread-config family
     (0xB0-0xB8), CLEARDVALID/SETRWC, SFPLOADI with an architecturally
     verified allocatable destination, and SFPCONFIG with a decoded
     constant destination (which CLAIMS that destination).  Anything
     else -- TTREPLAY, any raw SFPU-class word, a non-literal operand,
     a non-.ttinsn template -- refuses the whole TU byte-identically.
     MOP (expands runtime-configured instruction words) is DERIVED,
     never trusted: every mop_cfg template-slot write in the TU is
     itself a scanned store whose word must decode through the same
     table, and the MOP word is admitted exactly when all of them do
     (rvtt-mop-derive.cc; design NOTES-mop-effect-derivation-laneBC.md).
     The former `__builtin_rvtt_ttregion_begin/end' TRUSTED effects
     declarations are RETIRED (2026-08-18 ruling: the compiler proves
     region effects, it is not told them); the builtins are deprecated
     no-ops that declare nothing;
   - every store in the TU is a scan object (the volatile-push blind
     spot is closed): constant-address stores classify by target range
     (template slots, instruction-FIFO aperture, PC_BUF sync words,
     debug block, inert MMIO -- facts in rvtt-mop-tables.h);
     volatile stores at unresolved addresses must prove they cannot
     alias an instruction FIFO or refuse; the scalar blocking-store
     asm idiom classifies identically instead of being admitted blind;
   - user `vConstFloatPrgm' assignments (sfpwriteconfig_v) claim their
     constant destination; a non-constant destination refuses;
   - an indirect call or a call to a function with no body in this TU
     refuses (it could contain undeclared Tensix code) -- with one
     PROVEN exception: the crt0 init-array walk, whose callees are
     structurally this TU's own scanned static constructors
     (rvtt_mop_init_array_call_p); defined functions
     are scanned themselves (including, on demand, a body whose cgraph
     node IPA inlining has already consumed) and ordinary scalar
     compiler builtins are transparent;
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
#include "tree-ssa-loop-niter.h"
#include "tree-scalar-evolution.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-derive.h"

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
  /* The MOP template derivation facts (rvtt-mop-derive.h).  */
  rvtt_mop_derive_state mop;
};

static prgm_tu_facts tu_facts;

/* A gimple_asm whose template is empty emits nothing; the single
   canonical raw form is one `.ttinsn' directive with one constant
   input.  The scalar blocking-store idiom classifies as the store it
   is.  Everything else refuses.  */

static bool
scan_raw_asm (gasm *stmt, unsigned *claimed, const char **why,
	      rvtt_mop_derive_state *st)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return true;		/* pure barrier, no instruction */
  /* Audited scalar RISC-V templates: base-ISA instructions with no
     Tensix encoding space and no SFPU/PRGM/CC effect (fence = memory
     ordering; ebreak = debug trap; the startup stack-pointer load).  */
  if (!strcmp (s, "fence") || !strcmp (s, "ebreak")
      || !strcmp (s, "la sp, %0")
      /* The crt0 global-pointer initialization.  */
      || !strcmp (s, ".option push\n.option norelax\n"
		     "la gp, __global_pointer$\n.option pop"))
    return true;
  /* The scalar store-load-consume roundtrip idiom (pcbuf/mailbox
     handshakes): base-ISA memory operations only, but it STORES its
     value operand at its address operand, so it classifies like any
     other store (the former blanket admission was the asm face of the
     volatile-push blind spot).  */
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

/* Scan one function body.  Returns false (with *WHY set to the FIRST
   refusal) on refusal, but keeps scanning: later statements must still
   claim their destinations and record template-slot facts, and the
   complete blocker inventory is dumped for diagnosis (refused facts
   are only consumed when the TU is clean, so the extra folding is
   inert).  VISITED memoizes bodies across the TU walk and the
   on-demand callee scans below (a body's facts fold into *CLAIMED
   exactly once; a repeat visit is transparent).  */

static bool
scan_function_body (function *fn, unsigned *claimed, const char **why,
		    hash_set<function *> &visited,
		    rvtt_mop_derive_state *st)
{
  if (visited.add (fn))
    return true;
  bool ok = true;
  auto refuse = [&] (const char *w, gimple *stmt)
    {
      if (ok)
	{
	  *why = w;
	  ok = false;
	}
      if (dump_file)
	{
	  fprintf (dump_file, "prgm-const: blocker in %s: %s: ",
		   function_name (fn), w);
	  print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
	}
    };
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;

	  if (gasm *a = dyn_cast <gasm *> (stmt))
	    {
	      const char *w = nullptr;
	      if (!scan_raw_asm (a, claimed, &w, st))
		refuse (w, stmt);
	      continue;
	    }

	  if (!is_gimple_call (stmt))
	    {
	      /* Stores are first-class scan objects: template-slot
		 writes, instruction-FIFO pushes, and the FIFO-alias
		 proof for unresolved volatile addresses.  */
	      const char *w = nullptr;
	      if (!rvtt_mop_derive_store (stmt, claimed, &w, st))
		refuse (w, stmt);
	      continue;
	    }

	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  if (insnd)
	    {
	      gcall *call = as_a <gcall *> (stmt);
	      if (insnd->id == rvtt_insn_data::ttregion_begin
		  || insnd->id == rvtt_insn_data::ttregion_end)
		{
		  /* RETIRED trusted channel (2026-08-18 ruling): a
		     source-annotated effects claim is not a proof --
		     the markers are deprecated no-ops and declare
		     nothing.  Every raw word classifies through the
		     audited table on its own; MOP-expansion effects
		     are DERIVED from the TU's template-programming
		     stores (rvtt-mop-derive.cc).  */
		}
	      else if (insnd->id == rvtt_insn_data::sfpwriteconfig_v)
		{
		  tree dest = gimple_call_arg (call, 1);
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
		  *claimed |= 1u << d;
		}
	      continue;		/* typed builtins are transparent */
	    }

	  if (gimple_call_internal_p (stmt))
	    continue;
	  tree fndecl = gimple_call_fndecl (stmt);
	  if (!fndecl)
	    {
	      /* One proven exception: the crt0 init-array walk calls
		 only this TU's own registered static constructors --
		 every one a scanned definition of this same TU walk
		 (structural proof: rvtt_mop_init_array_call_p).  */
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
	    continue;		/* scalar compiler builtin */
	  cgraph_node *cn = cgraph_node::get (fndecl);
	  if (!cn || !cn->definition)
	    {
	      /* The cgraph node can already be gone when this scan runs:
		 IPA inlining consumes a fully-inlined comdat (e.g. an
		 implicitly-instantiated inline destructor in a profiler
		 zone scope) and the unreachable-node sweep removes it
		 before the late pipeline starts, while its gimple body
		 must stay alive until the last caller's inline transform
		 has run.  What executes here is exactly that body, so
		 scan it on demand (memoized).  A decl with no walkable
		 body still refuses -- never presume clean.  */
	      function *cfn = DECL_STRUCT_FUNCTION (fndecl);
	      if (cfn && cfn->cfg)
		{
		  const char *w = nullptr;
		  if (!scan_function_body (cfn, claimed, &w, visited, st))
		    refuse (w, stmt);
		  continue;
		}
	      refuse ("call to a function outside this translation unit",
		      stmt);
	      continue;
	    }
	  /* Defined in this TU: its body is scanned itself.  */
	}
    }
  return ok;
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

  hash_set<function *> visited;
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;		/* thunks/aliases carry no code */
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      const char *why = nullptr;
      /* Keep scanning after a refusal: later declarations must still
	 claim their destinations, and the complete blocker set is what
	 an unblocking header increment needs to know.  Only the first
	 blocker is reported.  */
      static char reason_buf[192];
      if (!ofn || !ofn->cfg)
	{
	  /* A defined body this pass cannot walk must refuse, never be
	     presumed clean.  */
	  tu_facts.refused = true;
	  if (!tu_facts.reason)
	    tu_facts.reason = "function body unavailable to the scan";
	  continue;
	}
      if (!scan_function_body (ofn, &tu_facts.claimed, &why, visited,
			       &tu_facts.mop))
	{
	  tu_facts.refused = true;
	  if (!tu_facts.reason)
	    {
	      snprintf (reason_buf, sizeof reason_buf, "%s in %s", why,
			node->dump_name ());
	      tu_facts.reason = reason_buf;
	    }
	}
    }
  /* Deferred MOP admission: a delivered MOP word was provisionally
     admitted above; it stands only if every TU template-slot write
     audited (rvtt-mop-derive.cc).  */
  const char *mop_why = nullptr;
  if (!rvtt_mop_derive_finish (&tu_facts.mop, &mop_why))
    {
      tu_facts.refused = true;
      if (!tu_facts.reason)
	tu_facts.reason = mop_why;
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
  gcall *addi;			/* the SFPADDI or SFPADD to rewrite */
  gcall *mul;
  gcall *loadi;			/* in-loop materialization, or null for
				   the immediate SFPADDI shape */
  unsigned value;		/* fp32 bits of the constant */
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

/* A single-use in-loop SFPMUL with the plain mod, defining SRC.  */

static gcall *
fusable_mul_p (tree src, class loop *loop, gimple *only_use)
{
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *mul = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!mul)
    return nullptr;
  const rvtt_insn_data *muld = rvtt_get_insn_data (mul);
  if (!muld || muld->id != rvtt_insn_data::sfpmul
      || !integer_zerop (gimple_call_arg (mul, 2))
      || !gimple_bb (mul)
      || !flow_bb_inside_loop_p (loop, gimple_bb (mul))
      || !single_nondebug_use_p (src, only_use))
    return nullptr;
  return mul;
}

/* An in-loop invariant float materialization defining SRC whose fp32
   value is recoverable: the canonical sfpxloadi 32-bit float form, or
   the shortened single-issue SFPLOADI FLOATB form.  Other encodings
   refuse (their value reconstruction is not on record here).  */

static gcall *
invariant_float_load_p (tree src, class loop *loop, gimple *only_use,
			unsigned *value)
{
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *load = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!load
      || !gimple_bb (load)
      || !flow_bb_inside_loop_p (loop, gimple_bb (load))
      || !rvtt_invariant_constant_load_p (load, loop,
					  /*allow_shortened=*/true)
      || !single_nondebug_use_p (src, only_use))
    return nullptr;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (load);
  unsigned imm = TREE_INT_CST_LOW (gimple_call_arg (load, 1));
  tree mod = gimple_call_arg (load, gimple_call_num_args (load) - 1);
  if (insnd->id == rvtt_insn_data::sfpxloadi)
    {
      /* The float-typed 32-bit form only.  */
      if (tree_to_shwi (mod) != -32)
	return nullptr;
      *value = imm;
    }
  else
    {
      /* Shortened SFPLOADI: FLOATB (mod0 0, imm16 << 16) only.  */
      if (!integer_zerop (mod))
	return nullptr;
      *value = (imm & 0xffff) << 16;
    }
  return load;
}

static bool
fusion_candidate_p (gcall *call, class loop *loop, candidate *out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return false;

  if (insnd->id == rvtt_insn_data::sfpaddi)
    {
      /* The immediate shape: LHS = sfpaddi (buf, MUL, imm, 0, 0, 0).  */
      if (!gimple_call_lhs (call)
	  || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
	  || !canonical_buffer_arg_p (gimple_call_arg (call, 0)))
	return false;
      for (unsigned ix = 2; ix != gimple_call_num_args (call); ++ix)
	if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
	  return false;
      /* Plain-add form only: synthesized id/var fields and mod all
	 zero.  */
      for (unsigned ix = 3; ix != gimple_call_num_args (call); ++ix)
	if (!integer_zerop (gimple_call_arg (call, ix)))
	  return false;

      gcall *mul = fusable_mul_p (gimple_call_arg (call, 1), loop, call);
      if (!mul)
	return false;
      out->addi = call;
      out->mul = mul;
      out->loadi = nullptr;
      out->value
	= (TREE_INT_CST_LOW (gimple_call_arg (call, 2)) & 0xffff) << 16;
      out->loop = loop;
      return true;
    }

  if (insnd->id == rvtt_insn_data::sfpadd)
    {
      /* The materialized shape the pressure refusal actually leaves in
	 a loop: LHS = sfpadd (MUL, LOADI, 0) (either operand order),
	 with the in-loop invariant materialization feeding only the
	 add.  */
      if (!gimple_call_lhs (call)
	  || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
	  || !integer_zerop (gimple_call_arg (call, 2)))
	return false;
      for (unsigned swap = 0; swap != 2; ++swap)
	{
	  tree mul_op = gimple_call_arg (call, swap);
	  tree load_op = gimple_call_arg (call, 1 - swap);
	  gcall *mul = fusable_mul_p (mul_op, loop, call);
	  unsigned value;
	  gcall *load = mul
	    ? invariant_float_load_p (load_op, loop, call, &value) : nullptr;
	  if (mul && load)
	    {
	      out->addi = call;
	      out->mul = mul;
	      out->loadi = load;
	      out->value = value;
	      out->loop = loop;
	      return true;
	    }
	}
      return false;
    }

  return false;
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

/* ==================================================================== */
/* Constant residency + rematerialization (lane BS).

   Motivation: the SFPU register file has eight allocatable LREGs
   (riscv.h SFPU_REG_NUM; L0..L7, hard regs 80..87) and NO spill path --
   the rvtt_sfpassign memory alternatives exist only so LRA's constraint
   matching succeeds, and selecting one is fatal at assembly output
   (rvtt.md rvtt_sfpassign; rvtt.cc rvtt_mov_error).  A function whose
   vector pressure exceeds the file therefore cannot compile at all.
   Production kernels work around this by hand: parking constants in the
   programmable constant registers (vConstFloatPrgm), or round-tripping
   values through Dst (ckernel_sfpu_trigonometry.h calculate_acosh).

   Two generic mechanisms, cheapest first:

   RESIDENCY (-mtt-tensix-optimize-const-residency): proven-constant
   values are parked in free programmable constant registers (SFPCONFIG
   dests 12..14; the same architectural facts as the M3 fusion class
   above: rvtt-mop-derive.cc LaneConfig-reset survival audit, dest-15
   word 0x910000F1 never touches LReg[11..14]).  Two admitted classes:
   - an in-loop invariant constant materialization (the loads the
     invariant pass left in a loop by LREG pressure) is programmed once
     on the loop entry edge and every use reads the constant register:
     saves two pushed SFPLOADI words per iteration for a one-time
     three-word programming cost (rvtt-cost.md delivery model: a
     RISC-pushed word ~ 1.23 replayed slots), so it pays for itself at
     two proven trips;
   - under LREG pressure (peak > SFPU_REG_NUM), an out-of-loop
     proven-constant value is reprogrammed in place: the programming
     writes replace the materialization and every use reads the
     constant register, which occupies NO allocatable LREG (the
     rvtt_sfpreadlreg expander emits a zero-cost cstlreg unspec for
     indices >= SFPU_CREG_IDX_LWM, folded into consumers by the unspec
     propagation passes).
   Selection is priced: in-loop candidates (per-iteration savings) rank
   above pressure-only candidates; within a class, more uses first.
   Refusals: prgm-exhausted, trip-count-unproven, cc-region-unproven,
   qsr-unproven, plus the TU freedom-proof refusals shared with the M3
   class.

   REMATERIALIZATION (-mtt-tensix-optimize-const-remat): when vector
   pressure still exceeds the file, a value materialized by an SFPLOADI
   chain from loop-invariant scalar inputs spills as nothing and
   reloads as its SFPLOADI chain: each use gets a fresh clone of the
   chain immediately before it, and the long-lived original dies.  The
   scalar inputs live in GPRs (spillable normally), so the reload is
   always available.

   Lane-predication soundness of a rematerialized load: SFPLOADI writes
   only CC-enabled lanes (craq-sim tensix.cpp:8546,8556-8568 [SIM];
   specs SFPLOADI.md:37-39 "if (VD < 8) lanewise if (LaneEnabled)"
   [SPEC]).  A clone placed immediately before its consumer executes
   under the consumer's CC state, so every lane the consumer reads AND
   commits is a lane the clone just wrote with the constant -- PROVIDED
   the consumer itself only commits CC-enabled lanes and reads operand
   lanes lane-locally.  That is the audited-consumer discipline below;
   consumers outside the audited class refuse by name
   (consumer-lane-discipline-unaudited), keeping the original
   materialization for that use.  Cross-lane readers (SFPTRANSP,
   SFPSHFT2) and all-lane writers (SFPMOV mod1==2, plain gimple vector
   copies) are structurally excluded.

   The pressure model is the gimple analogue of the invariant pass's
   loop proof (rvtt_loop_lreg_pressure_legal_p), generalized function
   wide: backward liveness of allocatable vector SSA values with a
   per-point peak.  It is a model, not the allocator: residual
   over-pressure after both mechanisms dumps a named refusal and the
   post-reload spill diagnosis (rtl-rvtt-spill-diag.cc) turns any
   actual spill into a named user error instead of an ICE.  */

/* A vector SSA value that will occupy an allocatable LREG.  Constant
   register reads (rvtt_sfpreadlreg with index >= SFPU_CREG_IDX_LWM)
   expand to a zero-cost cstlreg unspec folded into consumers
   (rvtt.md rvtt_sfpreadlreg expander; riscv.cc rtx cost 0) and are
   excluded.  */

/* LREG occupancy of a tracked value.  The multi-result classes carry
   2 or 4 registers (riscv-modes.def XTT64SI/XTT128SI; the
   sfpswap/sfptransp result types).  An unknown vector mode weighs as
   the whole file: over-counting only fires the relief tiers earlier,
   it never admits an unsound state.  */

static unsigned
lreg_width (tree name)
{
  switch (TYPE_MODE (TREE_TYPE (name)))
    {
    case E_XTT32SImode:
      return 1;
    case E_XTT64SImode:
      return 2;
    case E_XTT128SImode:
      return 4;
    default:
      return SFPU_REG_NUM;
    }
}

static bool
pressure_tracked_p (tree name)
{
  if (TREE_CODE (name) != SSA_NAME || !VECTOR_TYPE_P (TREE_TYPE (name)))
    return false;
  gimple *def = SSA_NAME_DEF_STMT (name);
  const rvtt_insn_data *insnd = def ? rvtt_get_insn_data (def) : nullptr;
  if (insnd && insnd->id == rvtt_insn_data::sfpreadlreg)
    {
      tree arg = gimple_call_arg (as_a <gcall *> (def), 0);
      if (TREE_CODE (arg) == INTEGER_CST
	  && TREE_INT_CST_LOW (arg) >= SFPU_CREG_IDX_LWM)
	return false;
    }
  return true;
}

/* Function-wide LREG pressure model: standard backward SSA liveness of
   pressure-tracked vector values, plus a per-block point-pressure
   maximum.  PHI results are defined at block entry; PHI arguments are
   live out of the corresponding predecessor.  Deliberately mirrors the
   conservative counting of rvtt_loop_lreg_pressure_legal_p
   (gimple-rvtt-invariant.cc).  */

struct lreg_pressure_model
{
  unsigned peak = 0;
  /* Per-BB (by index) live-in SSA-version bitmaps and the set of
     blocks whose point pressure exceeds the capacity.  */
  vec<bitmap> live_in = vNULL;
  bitmap over_bbs = nullptr;
  bitmap_obstack obstack;

  ~lreg_pressure_model ()
  {
    live_in.release ();
    bitmap_obstack_release (&obstack);
  }
};

static void
compute_lreg_pressure (function *fn, unsigned capacity,
		       lreg_pressure_model *m)
{
  bitmap_obstack_initialize (&m->obstack);
  unsigned nbb = last_basic_block_for_fn (fn);
  m->live_in.safe_grow_cleared (nbb, true);
  auto_vec<bitmap> live_out (nbb);
  live_out.safe_grow_cleared (nbb, true);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      m->live_in[bb->index] = BITMAP_ALLOC (&m->obstack);
      live_out[bb->index] = BITMAP_ALLOC (&m->obstack);
    }
  m->over_bbs = BITMAP_ALLOC (&m->obstack);

  /* Fixpoint over the may-live sets.  */
  bool changed = true;
  while (changed)
    {
      changed = false;
      FOR_EACH_BB_FN (bb, fn)
	{
	  bitmap out = live_out[bb->index];
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      if (e->dest == EXIT_BLOCK_PTR_FOR_FN (fn))
		continue;
	      changed |= bitmap_ior_into (out, m->live_in[e->dest->index]);
	      for (gphi_iterator psi = gsi_start_phis (e->dest);
		   !gsi_end_p (psi); gsi_next (&psi))
		{
		  tree arg = gimple_phi_arg_def (psi.phi (), e->dest_idx);
		  if (pressure_tracked_p (arg))
		    changed |= bitmap_set_bit (out, SSA_NAME_VERSION (arg));
		}
	    }

	  /* live_in = upward-exposed uses + (live_out - defs).  */
	  bitmap in = BITMAP_ALLOC (&m->obstack);
	  bitmap_copy (in, out);
	  for (gimple_stmt_iterator gsi = gsi_last_bb (bb); !gsi_end_p (gsi);
	       gsi_prev (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      if (is_gimple_debug (stmt))
		continue;
	      tree lhs = gimple_get_lhs (stmt);
	      if (lhs && pressure_tracked_p (lhs))
		bitmap_clear_bit (in, SSA_NAME_VERSION (lhs));
	      ssa_op_iter iter;
	      tree use;
	      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
		if (pressure_tracked_p (use))
		  bitmap_set_bit (in, SSA_NAME_VERSION (use));
	    }
	  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	       gsi_next (&psi))
	    {
	      tree res = gimple_phi_result (psi.phi ());
	      if (pressure_tracked_p (res))
		bitmap_clear_bit (in, SSA_NAME_VERSION (res));
	    }
	  changed |= bitmap_ior_into (m->live_in[bb->index], in);
	  BITMAP_FREE (in);
	}
    }

  /* Point-pressure maxima: walk each block backward from its live-out
     set.  A dead definition still occupies a register at its
     definition point.  */
  FOR_EACH_BB_FN (bb, fn)
    {
      bitmap live = BITMAP_ALLOC (&m->obstack);
      bitmap_copy (live, live_out[bb->index]);
      unsigned count = 0;
      {
	bitmap_iterator bi;
	unsigned v;
	EXECUTE_IF_SET_IN_BITMAP (live, 0, v, bi)
	  count += lreg_width (ssa_name (v));
      }
      unsigned bb_max = count;
      for (gimple_stmt_iterator gsi = gsi_last_bb (bb); !gsi_end_p (gsi);
	   gsi_prev (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;
	  tree lhs = gimple_get_lhs (stmt);
	  if (lhs && pressure_tracked_p (lhs))
	    {
	      if (bitmap_clear_bit (live, SSA_NAME_VERSION (lhs)))
		count -= lreg_width (lhs);
	      else
		/* Dead def: transiently occupies its registers here.  */
		bb_max = MAX (bb_max, count + lreg_width (lhs));
	    }
	  ssa_op_iter iter;
	  tree use;
	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	    if (pressure_tracked_p (use)
		&& bitmap_set_bit (live, SSA_NAME_VERSION (use)))
	      count += lreg_width (use);
	  bb_max = MAX (bb_max, count);
	}
      BITMAP_FREE (live);
      m->peak = MAX (m->peak, bb_max);
      if (bb_max > capacity)
	bitmap_set_bit (m->over_bbs, bb->index);
    }
}

/* An SFPLOADI materialization chain defining a vector value from
   scalar-only inputs: a single sfpxloadi/sfploadi, or an sfploadi
   followed by the sfploadi_lv upper-half merge whose live-value
   operand is the first load (the two-issue form the immvar expansion
   emits for 32-bit values, including runtime scalars synthesized
   through the instruction buffer).  Every non-link argument must be a
   scalar (constant or GPR-resident SSA value), so the chain can be
   re-issued anywhere its definition dominates.  */

struct remat_chain
{
  gcall *tail;			/* defines the candidate value */
  gcall *root;			/* == tail for single-issue chains */
};

/* The vector live-value link of an sfploadi_lv call (arg 1 by the
   builtin signature XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_..., rvtt-insn.def),
   or NULL_TREE.  */

static tree
loadi_lv_link (gcall *call)
{
  tree arg = gimple_call_arg (call, 1);
  return VECTOR_TYPE_P (TREE_TYPE (arg)) ? arg : NULL_TREE;
}

static bool
scalar_args_p (gcall *call, tree skip)
{
  for (unsigned ix = 0; ix != gimple_call_num_args (call); ++ix)
    {
      tree arg = gimple_call_arg (call, ix);
      if (arg == skip)
	continue;
      if (VECTOR_TYPE_P (TREE_TYPE (arg)))
	return false;
    }
  return true;
}

static bool
remat_chain_p (tree name, remat_chain *out)
{
  if (TREE_CODE (name) != SSA_NAME)
    return false;
  gcall *tail = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (name));
  if (!tail || !gimple_bb (tail) || gimple_call_lhs (tail) != name)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (tail);
  if (!insnd)
    return false;

  if (insnd->id == rvtt_insn_data::sfpxloadi
      || insnd->id == rvtt_insn_data::sfploadi)
    {
      if (!scalar_args_p (tail, NULL_TREE))
	return false;
      out->tail = out->root = tail;
      return true;
    }

  if (insnd->id == rvtt_insn_data::sfploadi_lv)
    {
      tree link = loadi_lv_link (tail);
      if (!link || TREE_CODE (link) != SSA_NAME
	  || !scalar_args_p (tail, link)
	  || !single_nondebug_use_p (link, tail))
	return false;
      gcall *root = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (link));
      if (!root || !gimple_bb (root))
	return false;
      const rvtt_insn_data *rootd = rvtt_get_insn_data (root);
      if (!rootd
	  || (rootd->id != rvtt_insn_data::sfploadi
	      && rootd->id != rvtt_insn_data::sfpxloadi)
	  || !scalar_args_p (root, NULL_TREE))
	return false;
      out->tail = tail;
      out->root = root;
      return true;
    }

  return false;
}

/* The full 32-bit lane image of a single-issue constant chain, for the
   residency dedup and programming write.  The 32-bit sfpxloadi forms
   carry the fp32/int32 pattern verbatim (gimple-rvtt-immvar.cc
   emit_loadimm bits 31/-32/32); the shortened SFPLOADI FLOATB form is
   imm16 << 16 (rvtt-protos.h SFPLOADI_MOD0_FLOATB; craq-sim
   tensix.cpp:8556-8558 [SIM]).  Other encodings refuse -- their value
   reconstruction is not on record here (they remain remat
   candidates, which re-issue verbatim and never interpret the
   value).  */

static bool
constant_chain_value_p (const remat_chain &c, unsigned *value)
{
  if (c.root != c.tail)
    return false;
  gcall *load = c.tail;
  tree imm = gimple_call_arg (load, 1);
  if (TREE_CODE (imm) != INTEGER_CST || !scalar_args_p (load, NULL_TREE))
    return false;
  for (unsigned ix = 1; ix != gimple_call_num_args (load); ++ix)
    if (TREE_CODE (gimple_call_arg (load, ix)) != INTEGER_CST)
      return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (load);
  tree mod = gimple_call_arg (load, gimple_call_num_args (load) - 1);
  if (insnd->id == rvtt_insn_data::sfpxloadi)
    {
      HOST_WIDE_INT bits = tree_to_shwi (mod);
      if (bits != 31 && bits != 32 && bits != -32)
	return false;
      *value = TREE_INT_CST_LOW (imm);
      return true;
    }
  /* Shortened SFPLOADI: FLOATB only.  */
  if (!integer_zerop (mod))
    return false;
  *value = (TREE_INT_CST_LOW (imm) & 0xffff) << 16;
  return true;
}

/* Audited remat consumers: instructions whose destination lanes are
   written only where CC-enabled and whose vector operand reads are
   lane-local, so a constant rematerialized immediately before them is
   observationally equivalent to the original long-lived value on every
   consumed lane.  Facts audited against the craq-sim executors
   (src/tensix.cpp @32489dda lineage) and the ISA functional models
   (tests/aristotle/mega-union/specs/*.md):
   - the canonical predicate is the shared mask idiom
     `cc_en ? cc : ALL` + for_each_lane (tensix.cpp:8304-8310) [SIM];
     representative spec form: SFPMAD.md:19-22 "lanewise if
     (LaneEnabled)" [SPEC];
   - per-op mask/write sites: SFPMAD :9199/:9202-9228; SFPMUL
     :9291/:9306-9309; SFPADD :9249/:9261; SFPMULI :8791/:8792-8795;
     SFPADDI :8808/:8809-8812; SFPIADD :8900/:8912-8923 (CC updates
     also enabled-lanes-only); SFPSTORE :8610-8615 (Dst rows written
     only for enabled lanes); SFPSETEXP :9142/:9153; SFPEXEXP
     :8851/:8856-8869; SFPSETMAN :9167/:9175; SFPEXMAN :8883/:8886;
     SFPSETSGN :9421/:9429; SFPABS :9035/:9047; SFPAND/OR/XOR/NOT via
     tensix_execute_sfpu_int32 :9059/:9063; SFPSHFT :8944/:8961;
     SFPCAST :9613/:9635; SFPLZ :9112/:9118-9125; SFPDIVP2
     :8826/:8837; SFPSTOCHRND :9515-9521 (write predicated; the PRNG
     side effect is lane-state, not an LREG value); SFPSWAP
     :9776/:9784-9797 (predicated, lane-local); SFPLUT :8761/:8774;
     SFPLUTFP32 :10152/:10196 [SIM].
   Structurally excluded (refuse by name):
   - SFPMOV mod1==2 copies all lanes (tensix.cpp:9008-9010;
     SFPMOV.md:29) -- and every plain gimple vector copy or PHI lowers
     to it;
   - SFPTRANSP predicates per DESTINATION lane while reading another
     lane (tensix.cpp:9488-9493; SFPTRANSP.md:44-45) and the SFPSHFT2
     family snapshots all 32 source lanes (tensix.cpp:9997-10063):
     cross-lane reads consume lanes the clone may not have written;
   - SFPCONFIG reads staging lanes 0..7 under its own Imm16 gating,
     not the CC mask (tensix.cpp:9665-9682).  */

static bool
remat_consumer_audited_p (gimple *stmt, tree name)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (stmt);
  /* A live-value operand is tied to the destination (rvtt.md _lv
     alternatives constrain it "0"): the consumer's CC-DISABLED result
     lanes ARE this operand's lanes, so a clone that wrote only the
     enabled lanes would leak garbage through the merge.  Refuse the
     whole use.  (SFPSWAP is excluded from the table below for the
     same reason: both of its operands are tied in/out,
     rvtt.md rvtt_sfpswap.)  */
  if (insnd->is_live ()
      && gimple_call_arg (call, insnd->live_arg ()) == name)
    return false;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpstore:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfpand:
    case rvtt_insn_data::sfpand_lv:
    case rvtt_insn_data::sfpor:
    case rvtt_insn_data::sfpor_lv:
    case rvtt_insn_data::sfpxor:
    case rvtt_insn_data::sfpxor_lv:
    case rvtt_insn_data::sfpnot:
    case rvtt_insn_data::sfpnot_lv:
    case rvtt_insn_data::sfpshft_v:
    case rvtt_insn_data::sfpshft_v_lv:
    case rvtt_insn_data::sfpshft_i:
    case rvtt_insn_data::sfpshft_i_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
      return true;

    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
      {
	/* Predicated for mod1 0/1/8; mod1 2 copies all lanes
	   (tensix.cpp:9007-9022) [SIM].  */
	tree mod = gimple_call_arg (call, gimple_call_num_args (call) - 1);
	return TREE_CODE (mod) == INTEGER_CST
	  && TREE_INT_CST_LOW (mod) != SFPMOV_MOD1_ALL;
      }

    default:
      (void) name;
      return false;
    }
}

/* Clone CHAIN immediately before the statement at *GSI and return the
   fresh SSA value.  Scalar arguments are reused (their definitions
   dominate the chain's, which dominates every use); virtual operands
   are renumbered by the pass-level TODO_update_ssa_only_virtuals.  */

static tree
clone_chain_before (const remat_chain &c, gimple_stmt_iterator *gsi)
{
  tree link_value = NULL_TREE;
  if (c.root != c.tail)
    {
      gcall *root = as_a <gcall *> (gimple_copy (c.root));
      tree fresh = make_ssa_name (TREE_TYPE (gimple_call_lhs (c.root)));
      gimple_call_set_lhs (root, fresh);
      gimple_set_vdef (root, NULL_TREE);
      gimple_set_vuse (root, NULL_TREE);
      gsi_insert_before (gsi, root, GSI_SAME_STMT);
      link_value = fresh;
    }
  gcall *tail = as_a <gcall *> (gimple_copy (c.tail));
  tree fresh = make_ssa_name (TREE_TYPE (gimple_call_lhs (c.tail)));
  gimple_call_set_lhs (tail, fresh);
  gimple_set_vdef (tail, NULL_TREE);
  gimple_set_vuse (tail, NULL_TREE);
  if (link_value)
    gimple_call_set_arg (tail, 1, link_value);
  gsi_insert_before (gsi, tail, GSI_SAME_STMT);
  return fresh;
}

/* Delete an original chain whose value has been fully rematerialized.  */

static void
delete_chain (const remat_chain &c)
{
  reset_debug_uses (c.tail);
  if (c.root != c.tail)
    reset_debug_uses (c.root);
  gimple_stmt_iterator tgsi = gsi_for_stmt (c.tail);
  unlink_stmt_vdef (c.tail);
  gsi_remove (&tgsi, true);
  release_defs (c.tail);
  if (c.root != c.tail)
    {
      gimple_stmt_iterator rgsi = gsi_for_stmt (c.root);
      unlink_stmt_vdef (c.root);
      gsi_remove (&rgsi, true);
      release_defs (c.root);
    }
}

/* Rematerialize loadi-chain values at their audited uses while the
   pressure model exceeds the LREG file.  Only values live through an
   over-pressure block are touched, in SSA version order (deterministic
   and source-stable).  */

static bool
remat_transform (function *fn)
{
  const unsigned capacity = SFPU_REG_NUM;
  lreg_pressure_model model;
  compute_lreg_pressure (fn, capacity, &model);
  if (model.peak <= capacity)
    {
      if (dump_file)
	fprintf (dump_file,
		 "const-remat: pressure %u within the %u-LREG file; "
		 "nothing to do\n", model.peak, capacity);
      return false;
    }
  if (dump_file)
    fprintf (dump_file, "const-remat: pressure %u exceeds the %u-LREG "
	     "file\n", model.peak, capacity);

  /* Candidates in SSA version order.  A two-issue chain's first-load
     name is itself a well-formed single-issue chain; it is the tail's
     private link, not a candidate (deleting the tail's chain releases
     it).  */
  auto_vec<tree> names;
  hash_set<tree> chain_links;
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, fn)
    {
      remat_chain chain;
      if (!pressure_tracked_p (name) || !remat_chain_p (name, &chain))
	continue;
      if (chain.root != chain.tail)
	chain_links.add (gimple_call_lhs (chain.root));
      /* Live through (or defined in) an over-pressure block?  */
      bool relevant = bitmap_bit_p (model.over_bbs,
				    gimple_bb (chain.tail)->index);
      if (!relevant)
	{
	  bitmap_iterator bi;
	  unsigned bbi;
	  EXECUTE_IF_SET_IN_BITMAP (model.over_bbs, 0, bbi, bi)
	    if (bbi < model.live_in.length ()
		&& model.live_in[bbi]
		&& bitmap_bit_p (model.live_in[bbi], SSA_NAME_VERSION (name)))
	      {
		relevant = true;
		break;
	      }
	}
      if (!relevant)
	continue;
      names.safe_push (name);
    }

  bool changed = false;
  unsigned last_peak = model.peak;
  for (tree cand : names)
    {
      /* Re-validate: an earlier candidate's chain deletion may have
	 released this name (its version is then in the free list and
	 its definition statement cleared).  */
      if (TREE_CODE (cand) != SSA_NAME
	  || SSA_NAME_IN_FREE_LIST (cand)
	  || !SSA_NAME_DEF_STMT (cand)
	  || chain_links.contains (cand))
	continue;
      remat_chain chain;
      if (!remat_chain_p (cand, &chain))
	continue;

      /* Gather the real uses first: cloning mutates the use list.  */
      auto_vec<gimple *> uses;
      bool kept_any = false;
      imm_use_iterator iter;
      gimple *use_stmt;
      FOR_EACH_IMM_USE_STMT (use_stmt, iter, cand)
	{
	  if (is_gimple_debug (use_stmt))
	    continue;
	  if (gimple_code (use_stmt) == GIMPLE_PHI)
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "const-remat: use refused (phi-use-unclonable): ");
	      if (dump_file)
		print_gimple_stmt (dump_file, use_stmt, 0);
	      kept_any = true;
	      continue;
	    }
	  if (!remat_consumer_audited_p (use_stmt, cand))
	    {
	      if (dump_file)
		fprintf (dump_file, "const-remat: use refused "
			 "(consumer-lane-discipline-unaudited): ");
	      if (dump_file)
		print_gimple_stmt (dump_file, use_stmt, 0);
	      kept_any = true;
	      continue;
	    }
	  uses.safe_push (use_stmt);
	}
      if (uses.is_empty ())
	continue;

      for (gimple *u : uses)
	{
	  gimple_stmt_iterator ugsi = gsi_for_stmt (u);
	  tree fresh = clone_chain_before (chain, &ugsi);
	  gcall *ucall = as_a <gcall *> (u);
	  for (unsigned ix = 0; ix != gimple_call_num_args (ucall); ++ix)
	    if (gimple_call_arg (ucall, ix) == cand)
	      gimple_call_set_arg (ucall, ix, fresh);
	  update_stmt (u);
	  if (dump_file)
	    {
	      fprintf (dump_file, "const-remat: rematerialized %s before ",
		       print_generic_expr_to_str (cand));
	      print_gimple_stmt (dump_file, u, 0);
	    }
	}
      if (!kept_any)
	delete_chain (chain);
      changed = true;

      lreg_pressure_model next;
      compute_lreg_pressure (fn, capacity, &next);
      last_peak = next.peak;
      if (next.peak <= capacity)
	break;
    }

  if (dump_file)
    {
      if (last_peak > capacity)
	fprintf (dump_file, "const-remat: residual pressure %u exceeds %u "
		 "(lreg-pressure-unresolvable)\n", last_peak, capacity);
      else
	fprintf (dump_file, "const-remat: pressure resolved: %u -> %u\n",
		 model.peak, last_peak);
    }
  return changed;
}

/* ------------------------------------------------------------------ */
/* Residency allocation state shared between the M3 fusion class and
   the residency classes: SFPCONFIG destination claims and the
   identical-value allocation table.  */

struct prgm_alloc { unsigned value; unsigned reg; basic_block bb; };

struct prgm_state
{
  bool initialized = false;
  unsigned claimed = 0;
  auto_vec<prgm_alloc, 4> allocs;
};

static bool
transform (function *fn, prgm_state *st)
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

  if (!st->initialized)
    {
      st->claimed = facts.claimed;
      st->initialized = true;
    }
  unsigned &claimed = st->claimed;
  bool changed = false;
  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *add_d = rvtt_get_insn_data (rvtt_insn_data::sfpadd);

  /* Identical-immediate reuse: an earlier allocation of the SAME fp32
     value shares its PRGM register (the register is claimed by us and
     every programming write stores the same constant, so reuse is
     order-insensitive); when the earlier allocation's LOOP HEADER
     moreover DOMINATES the new loop's entry, the earlier programming
     provably executed first (any path to a loop header passes its
     entry edge by induction over the backedge) and no second
     programming write is emitted.  (The sdpa shape: three inlined exp
     bodies used to burn L12+L13+L14 on one immediate.)  Candidates
     are visited in block order so the dominating allocation is seen
     first.  */
  auto_vec<prgm_alloc, 4> &allocs = st->allocs;

  /* Sort candidates by the function's block order (an approximation
     of program order; correctness never depends on it -- the
     dominance test does the proving).  */
  {
    hash_map<basic_block, int> seq;
    int n = 0;
    basic_block obb;
    FOR_EACH_BB_FN (obb, fn)
      seq.put (obb, n++);
    auto key = [&seq] (const candidate &c) -> int
      {
	int *p = seq.get (c.entry->dest);
	return p ? *p : INT_MAX;
      };
    for (unsigned i = 1; i < candidates.length (); ++i)
      for (unsigned j = i; j > 0 && key (candidates[j - 1])
					> key (candidates[j]); --j)
	std::swap (candidates[j - 1], candidates[j]);
  }

  for (candidate &c : candidates)
    {
      unsigned prgm = 0;
      basic_block prior_bb = nullptr;
      for (prgm_alloc &a : allocs)
	if (a.value == c.value)
	  {
	    prgm = a.reg;
	    prior_bb = a.bb;
	    break;
	  }
      if (!prgm)
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

      tree vec_type = TREE_TYPE (gimple_call_lhs (c.addi));
      bool reprogram
	= !prior_bb
	  || !dominated_by_p (CDI_DOMINATORS, c.entry->dest, prior_bb);
      if (reprogram)
	{
	  /* Program the constant on the loop entry edge.  */
	  basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
	  gcall *load = gimple_build_call
	    (xloadi_d->decl, 5, null_pointer_node,
	     build_int_cst (unsigned_type_node, c.value),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (integer_type_node, -32));
	  tree staged = make_ssa_name (vec_type);
	  gimple_call_set_lhs (load, staged);
	  gcall *wrcfg = gimple_build_call
	    (wrcfg_d->decl, 2, staged,
	     build_int_cst (unsigned_type_node, prgm));

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
	  if (!prior_bb)
	    allocs.safe_push (prgm_alloc { c.value, prgm, c.entry->dest });
	}
      else if (dump_file)
	fprintf (dump_file,
		 "prgm-const: reused PRGM L%u for identical immediate "
		 "0x%08x (dominating programming point bb %d)\n",
		 prgm, c.value, prior_bb->index);

      /* Read it back as a constant register and re-offer the pair to
	 the mad combine (which runs after this pass).  */
      gimple_stmt_iterator gsi = gsi_for_stmt (c.addi);
      gcall *read = gimple_build_call
	(readlreg_d->decl, 1, build_int_cst (unsigned_type_node, prgm));
      tree creg = make_ssa_name (vec_type);
      gimple_call_set_lhs (read, creg);
      gsi_insert_before (&gsi, read, GSI_SAME_STMT);

      if (!c.loadi)
	{
	  /* Immediate shape: the SFPADDI becomes a plain SFPADD of the
	     constant register.  */
	  gcall *add = gimple_build_call
	    (add_d->decl, 3, gimple_call_arg (c.addi, 1), creg,
	     build_int_cst (unsigned_type_node, 0));
	  gimple_call_set_lhs (add, gimple_call_lhs (c.addi));
	  gsi_replace (&gsi, add, false);
	}
      else
	{
	  /* Materialized shape: the SFPADD keeps its form with the
	     constant-register operand; the in-loop materialization is
	     removed (its only use was this add).  */
	  tree load_lhs = gimple_call_lhs (c.loadi);
	  for (unsigned ix = 0; ix != 2; ++ix)
	    if (gimple_call_arg (c.addi, ix) == load_lhs)
	      gimple_call_set_arg (c.addi, ix, creg);
	  update_stmt (c.addi);
	  gimple_stmt_iterator lgsi = gsi_for_stmt (c.loadi);
	  if (tree vdef = gimple_vdef (c.loadi))
	    if (TREE_CODE (vdef) == SSA_NAME)
	      unlink_stmt_vdef (c.loadi);
	  gsi_remove (&lgsi, true);
	  release_defs (c.loadi);
	}

      changed = true;
      if (dump_file && reprogram)
	fprintf (dump_file,
		 "prgm-const: allocated PRGM L%u for invariant immediate "
		 "0x%08x (loop header bb %d)\n",
		 prgm, c.value, c.loop->header->index);
    }
  return changed;
}

/* ------------------------------------------------------------------ */
/* Residency allocation (lane BS): park proven-constant values in free
   PRGM registers by priced selection.  Class LOOP: an in-loop
   invariant constant materialization is programmed once on the loop
   entry edge (saves two pushed SFPLOADI words per proven iteration for
   a one-time three-word cost, so it needs proven trips >= 2:
   rvtt-cost.md delivery model).  Class PRESSURE: under LREG
   over-pressure, an out-of-loop proven-constant value is reprogrammed
   in place -- the constant register read occupies no allocatable LREG,
   which is the cheapest relief tier (ahead of rematerialization).  */

struct residency_candidate
{
  gcall *load;			/* the single-issue materialization */
  unsigned value;		/* full 32-bit lane image */
  class loop *loop;		/* LOOP class: the enclosing loop */
  edge entry;			/* LOOP class: its entry edge */
  unsigned uses;		/* non-debug uses (the ranking key) */
};

/* Fold VAL through the in-loop constant chain from a header PHI to OP:
   single-use assign statements whose other operands are invariant.
   Returns the folded value of OP on the first iteration, or NULL_TREE.
   (The same bounded-evaluation idea as the invariant pass's
   short-constant-loop proof, needed here because neither
   scalar-evolution nor loop_niter_by_eval is usable at this pipeline
   position: both assert canonical loop state -- LOOPS_NORMAL
   preheaders -- that AVOID_CFG_MODIFICATIONS deliberately does not
   establish.)  */

static tree
first_iteration_value (class loop *loop, edge entry, tree op)
{
  if (is_gimple_min_invariant (op))
    return op;
  /* Walk the definition chain backwards to a header PHI, then fold
     forward from the entry value.  The depth bound is proof work, not
     semantics (an unproven chain refuses; same discipline as the
     invariant pass's bounded exit-test evaluation): the rotated
     counted-loop exit tests this proof targets are one or two
     statements deep.  */
  auto_vec<gimple *, 8> chain;
  tree cur = op;
  for (unsigned depth = 0; depth != 8; ++depth)
    {
      if (TREE_CODE (cur) != SSA_NAME)
	return NULL_TREE;
      gimple *def = SSA_NAME_DEF_STMT (cur);
      if (gphi *phi = dyn_cast <gphi *> (def))
	{
	  if (gimple_bb (phi) != loop->header)
	    return NULL_TREE;
	  tree value = PHI_ARG_DEF_FROM_EDGE (phi, entry);
	  if (!is_gimple_min_invariant (value))
	    return NULL_TREE;
	  /* Fold the chain forward, substituting VALUE for each
	     statement's single non-invariant operand.  */
	  for (int ix = chain.length () - 1; ix >= 0; --ix)
	    {
	      gassign *a = as_a <gassign *> (chain[ix]);
	      tree_code code = gimple_assign_rhs_code (a);
	      tree type = TREE_TYPE (gimple_assign_lhs (a));
	      tree op1 = gimple_assign_rhs1 (a);
	      tree op2 = gimple_num_ops (a) > 2
		? gimple_assign_rhs2 (a) : NULL_TREE;
	      if (!is_gimple_min_invariant (op1))
		op1 = value;
	      if (op2 && !is_gimple_min_invariant (op2))
		op2 = value;
	      switch (get_gimple_rhs_class (code))
		{
		case GIMPLE_SINGLE_RHS:
		  value = op1;
		  break;
		case GIMPLE_UNARY_RHS:
		  value = fold_unary (code, type, op1);
		  break;
		case GIMPLE_BINARY_RHS:
		  value = fold_binary (code, type, op1, op2);
		  break;
		default:
		  return NULL_TREE;
		}
	      if (!value || !is_gimple_min_invariant (value))
		return NULL_TREE;
	    }
	  return value;
	}
      gassign *assign = dyn_cast <gassign *> (def);
      if (!assign || !gimple_bb (assign)
	  || !flow_bb_inside_loop_p (loop, gimple_bb (assign)))
	return NULL_TREE;
      /* Exactly one non-invariant operand continues the chain.  */
      tree next = NULL_TREE;
      for (unsigned i = 1; i < gimple_num_ops (assign); ++i)
	{
	  tree o = gimple_op (assign, i);
	  if (!o || is_gimple_min_invariant (o))
	    continue;
	  if (next)
	    return NULL_TREE;
	  next = o;
	}
      if (!next)
	return NULL_TREE;
      chain.safe_push (assign);
      cur = next;
    }
  return NULL_TREE;
}

/* Prove the loop's latch executes at least once (trips >= 2): evaluate
   the single exit test on the first iteration and require it to stay
   in the loop.  Refuses when anything fails to fold.  */

static bool
loop_second_trip_proven_p (class loop *loop, edge entry)
{
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1)
    return false;
  edge exit = exits[0];
  gimple_stmt_iterator last = gsi_last_bb (exit->src);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond)
    return false;
  tree lhs = first_iteration_value (loop, entry, gimple_cond_lhs (cond));
  tree rhs = first_iteration_value (loop, entry, gimple_cond_rhs (cond));
  if (!lhs || !rhs)
    return false;
  tree test = fold_binary (gimple_cond_code (cond), boolean_type_node,
			   lhs, rhs);
  if (!test || TREE_CODE (test) != INTEGER_CST)
    return false;
  edge true_edge, false_edge;
  extract_true_false_edges_from_block (exit->src, &true_edge, &false_edge);
  edge taken = integer_zerop (test) ? false_edge : true_edge;
  return taken && taken != exit;
}

static unsigned
count_nondebug_uses (tree name)
{
  unsigned n = 0;
  imm_use_iterator iter;
  gimple *use;
  FOR_EACH_IMM_USE_STMT (use, iter, name)
    if (!is_gimple_debug (use))
      ++n;
  return n;
}

static bool
residency_transform (function *fn, prgm_state *st)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  auto_vec<residency_candidate> loop_cands;
  auto_vec<residency_candidate> pressure_cands;
  hash_set<gimple *> taken;

  /* LOOP class.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (!loop->num)
	continue;
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
	    fprintf (dump_file,
		     "const-residency: loop bb %d refused (%s)\n",
		     loop->header->index, why);
	  continue;
	}

      /* Profitability: the entry-edge programming (three pushed words
	 once: two staging SFPLOADI + one SFPCONFIG) pays for itself
	 against the two pushed SFPLOADI words saved per iteration at
	 two proven trips (rvtt-cost.md delivery model).  The proof is
	 the structural first-iteration exit-test evaluation.  */
      if (!loop_second_trip_proven_p (loop, entry))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d refused "
		     "(trip-count-unproven: the two-trip break-even is "
		     "not proven)\n", loop->header->index);
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
	      gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	      if (!load || taken.contains (load)
		  || !rvtt_invariant_constant_load_p (load, loop,
						      /*allow_shortened=*/true))
		continue;
	      remat_chain chain { load, load };
	      unsigned value;
	      if (!constant_chain_value_p (chain, &value))
		continue;
	      residency_candidate c;
	      c.load = load;
	      c.value = value;
	      c.loop = loop;
	      c.entry = entry;
	      c.uses = count_nondebug_uses (gimple_call_lhs (load));
	      loop_cands.safe_push (c);
	      taken.add (load);
	    }
	}
      free (body);
    }

  /* PRESSURE class: only when the model exceeds the LREG file.  */
  {
    const unsigned capacity = SFPU_REG_NUM;
    lreg_pressure_model model;
    compute_lreg_pressure (fn, capacity, &model);
    if (model.peak > capacity)
      {
	unsigned version;
	tree name;
	FOR_EACH_SSA_NAME (version, name, fn)
	  {
	    remat_chain chain;
	    if (!pressure_tracked_p (name) || !remat_chain_p (name, &chain))
	      continue;
	    if (chain.root != chain.tail || taken.contains (chain.tail))
	      continue;
	    unsigned value;
	    if (!constant_chain_value_p (chain, &value))
	      continue;
	    /* Out-of-loop definitions only: replacing an in-loop
	       definition in place would reprogram every iteration.  */
	    basic_block def_bb = gimple_bb (chain.tail);
	    if (def_bb->loop_father && def_bb->loop_father->num != 0)
	      continue;
	    bool relevant = bitmap_bit_p (model.over_bbs, def_bb->index);
	    if (!relevant)
	      {
		bitmap_iterator bi;
		unsigned bbi;
		EXECUTE_IF_SET_IN_BITMAP (model.over_bbs, 0, bbi, bi)
		  if (bbi < model.live_in.length () && model.live_in[bbi]
		      && bitmap_bit_p (model.live_in[bbi],
				       SSA_NAME_VERSION (name)))
		    {
		      relevant = true;
		      break;
		    }
	      }
	    if (!relevant)
	      continue;
	    residency_candidate c;
	    c.load = chain.tail;
	    c.value = value;
	    c.loop = nullptr;
	    c.entry = nullptr;
	    c.uses = count_nondebug_uses (name);
	    pressure_cands.safe_push (c);
	    taken.add (chain.tail);
	  }
      }
    else if (dump_file)
      fprintf (dump_file, "const-residency: pressure %u within the %u-LREG "
	       "file; pressure class idle\n", model.peak, capacity);
  }

  if (loop_cands.is_empty () && pressure_cands.is_empty ())
    return false;

  /* The freedom proof and the all-lanes proof gate every allocation,
     exactly as for the M3 fusion class.  */
  const prgm_tu_facts &facts = tu_prgm_facts ();
  if (facts.refused)
    {
      if (dump_file)
	fprintf (dump_file,
		 "const-residency: refused (opaque-region-undeclared): %s\n",
		 facts.reason);
      return false;
    }
  if (function_writes_cc_p (fn))
    {
      if (dump_file)
	fprintf (dump_file, "const-residency: refused (cc-region-unproven)"
		 " -- in-function CC writes defeat the all-lanes programming"
		 " proof; cross-call ambient proof is not on record here\n");
      return false;
    }
  if (!st->initialized)
    {
      st->claimed = facts.claimed;
      st->initialized = true;
    }

  /* Priced selection: per-iteration savers first (higher proven
     benefit first), then pressure-relief candidates by use count.
     Deterministic tiebreak by value then use count.  */
  auto rank = [] (auto_vec<residency_candidate> &v)
    {
      for (unsigned i = 1; i < v.length (); ++i)
	for (unsigned j = i; j > 0
	     && (v[j - 1].uses < v[j].uses
		 || (v[j - 1].uses == v[j].uses
		     && v[j - 1].value > v[j].value)); --j)
	  std::swap (v[j - 1], v[j]);
    };
  rank (loop_cands);
  rank (pressure_cands);

  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);

  bool changed = false;
  auto place = [&] (residency_candidate &c) -> bool
    {
      unsigned prgm = 0;
      basic_block prior_bb = nullptr;
      for (prgm_alloc &a : st->allocs)
	if (a.value == c.value)
	  {
	    prgm = a.reg;
	    prior_bb = a.bb;
	    break;
	  }
      if (!prgm)
	for (unsigned reg : prgm_regs)
	  if (!(st->claimed & (1u << reg)))
	    {
	      prgm = reg;
	      break;
	    }
      if (!prgm)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "const-residency: refused "
		       "(prgm-exhausted): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      st->claimed |= 1u << prgm;

      basic_block point_bb = c.loop ? c.entry->dest : gimple_bb (c.load);
      /* Reuse without reprogramming needs the earlier programming to
	 provably execute first.  Block dominance is reflexive, and an
	 in-place (pressure class) programming point does NOT dominate
	 later statements of its own block -- so equality must
	 reprogram (same register, same value: always sound, merely
	 redundant).  */
      bool reprogram
	= !prior_bb
	  || point_bb == prior_bb
	  || !dominated_by_p (CDI_DOMINATORS, point_bb, prior_bb);
      tree vec_type = TREE_TYPE (gimple_call_lhs (c.load));
      if (reprogram)
	{
	  gcall *stage = gimple_build_call
	    (xloadi_d->decl, 5, null_pointer_node,
	     build_int_cst (unsigned_type_node, c.value),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (integer_type_node, -32));
	  tree staged = make_ssa_name (vec_type);
	  gimple_call_set_lhs (stage, staged);
	  gcall *wrcfg = gimple_build_call
	    (wrcfg_d->decl, 2, staged,
	     build_int_cst (unsigned_type_node, prgm));
	  if (c.loop)
	    {
	      basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
	      gimple_stmt_iterator phg = gsi_last_bb (preheader);
	      if (gsi_end_p (phg) || !stmt_ends_bb_p (gsi_stmt (phg)))
		{
		  gsi_insert_after (&phg, wrcfg, GSI_NEW_STMT);
		  gsi_insert_before (&phg, stage, GSI_SAME_STMT);
		}
	      else
		{
		  /* GSI_SAME_STMT keeps the iterator on the block
		     terminator: insert the definition first so the
		     SFPCONFIG lands after its staged operand.  */
		  gsi_insert_before (&phg, stage, GSI_SAME_STMT);
		  gsi_insert_before (&phg, wrcfg, GSI_SAME_STMT);
		}
	    }
	  else
	    {
	      gimple_stmt_iterator lgsi = gsi_for_stmt (c.load);
	      gsi_insert_before (&lgsi, stage, GSI_SAME_STMT);
	      gsi_insert_before (&lgsi, wrcfg, GSI_SAME_STMT);
	    }
	  if (!prior_bb)
	    st->allocs.safe_push (prgm_alloc { c.value, prgm, point_bb });
	}
      else if (dump_file)
	fprintf (dump_file,
		 "const-residency: reused PRGM L%u for identical constant "
		 "0x%08x (dominating programming point bb %d)\n",
		 prgm, c.value, prior_bb->index);

      /* The materialization becomes a constant-register read keeping
	 its SSA name: every use follows untouched, and the read
	 expands to a zero-pressure cstlreg unspec.  */
      gimple_stmt_iterator lgsi = gsi_for_stmt (c.load);
      gcall *read = gimple_build_call
	(readlreg_d->decl, 1, build_int_cst (unsigned_type_node, prgm));
      gimple_call_set_lhs (read, gimple_call_lhs (c.load));
      unlink_stmt_vdef (c.load);
      if (tree vdef = gimple_vdef (c.load))
	{
	  gimple_set_vdef (c.load, NULL_TREE);
	  if (TREE_CODE (vdef) == SSA_NAME)
	    release_ssa_name (vdef);
	}
      gsi_replace (&lgsi, read, false);

      if (dump_file && reprogram)
	fprintf (dump_file,
		 "const-residency: allocated PRGM L%u for constant 0x%08x "
		 "(%s class, %u uses, programming point bb %d)\n",
		 prgm, c.value, c.loop ? "loop" : "pressure", c.uses,
		 point_bb->index);
      return true;
    };

  for (residency_candidate &c : loop_cands)
    changed |= place (c);
  for (residency_candidate &c : pressure_cands)
    changed |= place (c);
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
    return TARGET_XTT_TENSIX
      && (riscv_tt_opt_prgm_const
	  || riscv_tt_opt_const_residency
	  || riscv_tt_opt_const_remat);
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
       find earlier functions' bodies already released.  (The remat
       phase does not touch PRGM state, but computing the facts is
       memoized and keeps the eager invariant for the phases that
       do.)  */
    if (riscv_tt_opt_prgm_const || riscv_tt_opt_const_residency)
      tu_prgm_facts ();
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    prgm_state st;
    bool changed = false;
    if (riscv_tt_opt_prgm_const)
      changed |= transform (fn, &st);
    if (riscv_tt_opt_const_residency)
      changed |= residency_transform (fn, &st);
    if (riscv_tt_opt_const_remat)
      changed |= remat_transform (fn);
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
