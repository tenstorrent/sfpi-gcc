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

   Admitted class: a fusion-enabling SFPADDI whose vector operand is a
   single-use SFPMUL in the same loop, plain-add mod, all-constant
   scalar operands, canonical instruction-buffer operand; the
   materialized SFPADD form of the same shape; and
   an SFPMAD the front-end already fused, per materialized-constant
   operand (mad_operand_candidates -- RECOGNITION-ONLY: this pass never
   fuses a MUL+ADD into a MAD itself; that rewrite collapses two
   roundings into one and is bit-changing.  Whether the final code is
   fused is decided by the pre-existing downstream mul+add->mad
   combine identically in the fired and unfired legs; this pass only
   changes which register a constant operand is read from).  The pure
   in-loop-loadi class (design D1 candidate (a)) refuses pending its
   own benefit discipline.

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
     (rvtt-mop-derive.cc).
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
     structured-CC model makes function entry all-lanes, and the proof is
     that no fn-local CC-writing statement can execute before the
     programming point -- a CC writer whose block can reach the point
     (fn-local CFG reachability, computed backwards from the candidate
     loop's header so it covers the programming point and every block
     between a cross-loop hoisted point and the loop) refuses by name
     (cc-region-unproven).  A CC writer the point can never be reached
     from -- post-loop epilogue code, a sibling branch -- leaves the
     entry state provably intact on every path to the point, so it no
     longer defeats the proof (the fn-entry-all-lanes
     model and the call-transparency assumption are unchanged from the
     function-granular version of this proof).

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

/* Defined with the rematerialization machinery below: the 32-bit
   constant image staged into a typed SFPCONFIG write, when its
   defining materialization has all-constant operands.  */
bool staged_config_value (tree staged, unsigned *value);

/* Fold one SFPCONFIG destination claim into the TU facts' unique-value
   table.  KNOWN false marks the destination's value underivable.  */

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

/* Claims folded inside the shared classifiers (raw words, template
   slots) carry no derivable staged value: every destination such a
   call claims -- including a REPEAT claim of an already-claimed
   destination -- loses value uniqueness.  The classifiers are given a
   zeroed local accumulator so repeat claims stay visible.  */

static void
tu_mark_claims_unknown (unsigned claims)
{
  tu_facts.value_known &= ~claims;
}

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
		    rvtt_mop_derive_state *st,
		    rvtt_mop_scan_ctx *ctx,
		    hash_set<function *> &active)
{
  /* Context-free scans are memoized as before (a body's facts fold
     exactly once); a context-BOUND scan re-reads the body under its
     call site's parameter bindings, so each driving call scans it
     afresh.  The active set breaks call cycles for both -- a cycle
     cannot be bound and refuses by name.  */
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
  /* Lane IV: a scan refusal also marks the TU-wide CC/lane-enable
     audit dirty (rvtt_mop_derive_state::cc_dirty), UNLESS the refusing
     statement is a canonical raw `.ttinsn' word whose decoded verdict
     is ambient-preserving -- the PRGM audit legitimately refuses words
     (SFPU load-macro template captures, non-allocatable SFPLOADI
     destinations, the canonical all-lanes SFPENCC itself) that provably
     cannot take the lane-enable state away from the all-lanes ambient.
     Refusals of any OTHER shape (unaudited words, unproven stores,
     unscannable calls, replay refusals) dirty the CC audit fail-closed:
     each could deliver a lane-enable write the walk cannot see.  */
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
  /* Census-escape discipline: a BARE ADDR_EXPR of an
     automatic aggregate in a value position is an address escape and
     poisons the object's field census -- direct field references
     (var.field in a store/load lvalue) are not escapes and are
     censused instead.  Call arguments are exempted only on the
     on-demand bound-scan path below, where the callee's own stores
     are censused under the binding.  */
  auto poison_bare_addr = [&] (tree op)
    {
      if (op && TREE_CODE (op) == ADDR_EXPR)
	{
	  tree var = TREE_OPERAND (op, 0);
	  if (VAR_P (var) && !TREE_STATIC (var) && !DECL_EXTERNAL (var)
	      && AGGREGATE_TYPE_P (TREE_TYPE (var)))
	    {
	      if (dump_file && ctx && ctx->census)
		{
		  fprintf (dump_file, "prgm-const: census poison in %s: ",
			   function_name (fn));
		  print_generic_expr (dump_file, var, TDF_NONE);
		  fprintf (dump_file, "\n");
		}
	      rvtt_mop_census_poison (ctx, var);
	    }
	}
    };

  /* Raw REPLAY record regions (flag-gated, default off):
     prepass every raw REPLAY word in this body through the record
     admission (rvtt-mop-derive.cc theorem).  An admitted exec=0
     record's swallowed words are architecturally never delivered and
     are EXCLUDED from the executed-word census below; the record word
     itself carries its admission (or its named refusal) into the main
     walk.  Flag off, this map stays empty and every REPLAY word keeps
     the established audited-table refusal byte-identically.  */
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
      /* A record word swallowed by an ENCLOSING admitted window is
	 data, not a window of its own -- but the region walk already
	 refuses nested expander words, so an admitted window can
	 never contain one (checked, not assumed).  */
      if (flag_checking)
	for (gimple *s : replay_suppressed)
	  gcc_checking_assert (!replay_verdict.get (s));
    }

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (ctx && ctx->census)
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
	      if (ctx && ctx->census)
		for (unsigned i = 0; i != gimple_asm_ninputs (a); ++i)
		  poison_bare_addr (TREE_VALUE (gimple_asm_input_op (a, i)));
	      /* Lane HS: a word swallowed by an admitted record window
		 is never delivered -- no executed-word audit applies;
		 the record word itself carries its prepass verdict.  */
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
	      /* Non-lvalue ADDR_EXPR operands escape (pointer
		 formation; the field store/load lvalues themselves
		 never carry a bare ADDR_EXPR operand).  */
	      if (ctx && ctx->census && is_gimple_assign (stmt))
		for (unsigned i = 1; i != gimple_num_ops (stmt); ++i)
		  poison_bare_addr (gimple_op (stmt, i));
	      /* Stores are first-class scan objects: template-slot
		 writes, instruction-FIFO pushes, and the FIFO-alias
		 proof for unresolved volatile addresses.  */
	      const char *w = nullptr;
	      unsigned local_claims = 0;
	      if (!rvtt_mop_derive_store (stmt, &local_claims, &w, st, ctx))
		refuse (w, stmt);
	      *claimed |= local_claims;
	      tu_mark_claims_unknown (local_claims);
	      continue;
	    }

	  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	  /* Aggregate addresses passed to a call escape the census
	     unless the callee's body is scanned UNDER this call's
	     bindings (the on-demand branch below); every other callee
	     class -- typed builtins, compiler builtins, internal
	     calls, top-level-scanned definitions (their context-free
	     scan cannot attribute stores to OUR object), refused calls
	     -- poisons.  */
	  auto poison_call_addr_args = [&] ()
	    {
	      if (!ctx || !ctx->census)
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
		  unsigned value = 0;
		  bool known
		    = staged_config_value (gimple_call_arg (call, 0), &value);
		  tu_fold_claim_value (d, known, value);
		  *claimed |= 1u << d;
		}
	      else if (insnd->id == rvtt_insn_data::sfpconfig_i)
		{
		  /* Immediate-form SFPCONFIG: the frontend check pins its
		     destination to LaneConfig (15) -- the same census class
		     as the value form's d == 15 arm above (LaneConfig bits
		     such as DISABLE_BACKDOOR_LOAD change how neighbouring
		     template-slot programming behaves), so it refuses
		     identically rather than falling through the transparent
		     default.  */
		  refuse ("user SFPCONFIG writes LaneConfig", stmt);
		  continue;
		}
	      /* X6 FPU face-transpose family (ttmovd2b/ttmovb2a/ttmovb2d/
		 ttmova2d/tttrnspsrcb/ttstallwait/ttrmwcib):
		 ADJUDICATED transparent for THIS census.  The census
		 tracks the SFPU unit's PRGM/LaneConfig state (SFPCONFIG
		 destinations 0..15); the X6 family programs the Matrix
		 Unit's world -- Dst rows, Src banks, thread/backend
		 configuration words -- and architecturally cannot write
		 any SFPCONFIG destination (SFPCONFIG.md vs RMWCIB.md/
		 SETC16.md config spaces).  Falling through transparent is
		 therefore a proof, not a default.  */
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
	    {
	      poison_call_addr_args ();
	      continue;		/* scalar compiler builtin */
	    }
	  cgraph_node *cn = cgraph_node::get (fndecl);
	  /* The callee decl's own node can already be gone when this
	     scan runs: IPA inlining consumes a fully-inlined comdat
	     (e.g. an implicitly-instantiated inline destructor in a
	     profiler zone scope) and the unreachable-node sweep removes
	     it before the late pipeline starts.  When the target
	     assembler supports comdat groups (HAVE_COMDAT_GROUP
	     toolchain builds) that decl is moreover the bodiless
	     complete-object cdtor SAME-BODY ALIAS (D1 -> D2), so the
	     decl-level body lookup below cannot see the code either.
	     The caller's own call edge survives both: it tracks the
	     alias redirection and the inline-clone bookkeeping, and its
	     ultimate callee's decl still holds the gimple body that
	     must stay alive until the last caller's inline transform
	     has run.  Resolve through it; what executes here is exactly
	     that body.  An unresolvable callee still refuses -- never
	     presume clean.  */
	  if (!cn || !cn->definition)
	    if (cgraph_node *caller = cgraph_node::get (fn->decl))
	      if (cgraph_edge *e = caller->get_edge (stmt))
		if (e->callee)
		  if (cgraph_node *target = e->callee->ultimate_alias_target ())
		    cn = target;
	  /* A defined alias or thunk in the symtab executes its ultimate
	     target's body, not its own (an alias has none; thunk glue
	     is compiler-generated scalar this-adjustment): resolve
	     before deciding whether a walkable body backs this call.
	     The bound is proof work, not semantics -- an unresolved
	     chain falls through to the refusal.  */
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
	      /* An inline clone shares its original's decl and body but
		 is not an ordinary TU-walk enumeration subject: scan the
		 shared body on demand rather than presume the walk got
		 it (the memoization makes the overlap transparent).  */
	      || cn->inlined_to)
	    {
	      /* Scan the resolved body on demand (memoized).  A
		 parameter-adjusted clone (IPA-SRA) carries no body of
		 its own until materialization: it materializes from its
		 clone_of origin's body, and clone transforms only
		 re-parameterize -- they never add effects -- so
		 scanning the origin body is a sound over-approximation
		 of what executes here.  A decl with no walkable body
		 anywhere on the chain still refuses -- never presume
		 clean.  */
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
		  /* Bind the callee's parameters to this call's actual
		     arguments, each read under THIS context, and scan
		     the body bound (unmemoized: another call site may
		     bind differently).  Aggregate-address arguments
		     are attributed rather than poisoned: the bound
		     callee's own stores and escapes census against the
		     caller-frame object through the binding.  */
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
		    = { ctx ? ctx->census : nullptr, &parm_map };
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
	  /* Defined in this TU: its body is scanned at the top level,
	     context-free -- it cannot attribute stores to our
	     caller-frame objects.  */
	  poison_call_addr_args ();
	}
    }
  active.remove (fn);
  return ok;
}

/* Compute (once) the TU facts.  Runs at the first execution of this
   pass, i.e. before any other function's gimple body has been released;
   functions synthesized after that point are compiler-generated scalar
   code and emit no Tensix instructions.  */

const prgm_tu_facts &
tu_prgm_facts ()
{
  if (tu_facts.computed)
    return tu_facts;
  tu_facts.computed = true;

  hash_set<function *> visited;
  hash_set<function *> active;
  rvtt_mop_obj_census census;
  rvtt_mop_scan_ctx root_ctx = { &census, nullptr };
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;		/* thunks/aliases carry no code */
      /* An inline clone is never emitted as standalone code: it exists
	 only as bookkeeping for one call site that WILL be inlined into
	 node->inlined_to (cgraph.h).  Its execution is therefore fully
	 covered by scanning that call in its caller's body -- which the
	 walk does, on demand and UNDER the call's parameter bindings.
	 Enumerating the shared generic body here context-free would
	 only re-read the same code with its parameters unbound (the
	 unclassifiable-composed-word refusals of the pre-binding scan).
	 A surviving master node (standalone emission possible: out-of-
	 line calls, address taken) has inlined_to == NULL and is still
	 scanned here context-free, refusing-default.  */
      if (node->inlined_to)
	continue;
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      const char *why = nullptr;
      /* Keep scanning after a refusal: later declarations must still
	 claim their destinations, and the complete blocker set is what
	 an unblocking header increment needs to know.  Only the first
	 blocker is reported.  */
      static char reason_buf[192];
      if (!ofn || !ofn->cfg)
	{
	  /* A parameter-adjusted clone (IPA-SRA) carries no body of its
	     own until materialization: it materializes from its
	     clone_of origin's body, and clone transforms only
	     re-parameterize -- they never add effects -- so scanning
	     the origin body is a sound over-approximation.  */
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
	      /* A defined body this pass cannot walk must refuse, never
		 be presumed clean.  */
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
      /* Constant-register reader census (the creg_read comment above):
	 every emitted body passes through this loop (inline clones'
	 statements appear inlined in their standalone callers by this
	 point), so a clear bit is a TU-wide no-reader fact.  */
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
  /* Deferred MOP admission: a delivered MOP word was provisionally
     admitted above; it stands only if every TU template-slot write
     audited (rvtt-mop-derive.cc).  */
  const char *mop_why = nullptr;
  if (!rvtt_mop_derive_finish (&tu_facts.mop, &mop_why))
    {
      tu_facts.refused = true;
      if (!tu_facts.reason)
	tu_facts.reason = mop_why;
      /* An unproven MOP expansion could deliver template
	 words the walk cannot see -- the CC audit dirties with it.  */
      if (!tu_facts.mop.cc_dirty)
	{
	  tu_facts.mop.cc_dirty = true;
	  snprintf (tu_facts.mop.cc_reason, sizeof tu_facts.mop.cc_reason,
		    "%s", mop_why);
	}
    }

  /* Item #15: the tu_facts dump/verify surface.  The snapshot's
     structural invariants are checked once at compute time; the
     kernel-single-TU / crt0-benign axioms' decl-level footing (the
     entry-root enumeration) is recorded and dumped as a CHECKED
     property (rvtt_ipa_tu_anchors; body-free, so consulting it moves
     no census snapshot point).  Dump spellings deliberately avoid the
     twin suite's pinned scan-dump-not stems.  */
  if (flag_checking)
    {
      /* A unique programmed value exists only for a claimed
	 destination; a blocked snapshot always names its blocker.  */
      gcc_assert ((tu_facts.value_known & ~tu_facts.claimed) == 0);
      gcc_assert (!tu_facts.refused || tu_facts.reason);
      gcc_assert (!tu_facts.mop.cc_dirty || tu_facts.mop.cc_reason[0]);
    }
  /* The checked decl-level anchor property computes with the snapshot
     whether or not anyone dumps.  */
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


/* Instantiate the pass for its rvtt-passes.def seat: after the CC
   analysis and immediately before the combiner, so a rewritten
   immediate pair is re-offered to the existing mul+add->mad rule.  */

gimple_opt_pass *
make_pass_rvtt_prgm_const (gcc::context *ctxt)
{
  return new pass_rvtt_prgm_const (ctxt);
}

/* Lane IV (typecast walk-transparency): the TU-wide CC/lane-enable
   audit, consumed by the macro-planner's entry-ambient walk
   (rtl-rvtt-macro-planner.cc entry_ambient_all_lanes_p).  True exactly
   when the memoized TU scan ran AND classified every opaque-delivery
   channel in the TU -- raw `.ttinsn' words, stores (the
   instruction-FIFO/aperture audit), MOP template expansions, replay
   records, scalar asm -- as unable to take the lane-enable state away
   from the all-lanes ambient (rvtt_mop_derive_state::cc_dirty, folded
   by the scan's refusal funnel).  The coverage is exactly the
   enumeration the PRGM freedom proof already stands on (tu_prgm_facts);
   typed instructions are NOT covered here -- the RTL walk classifies
   those itself, and calls stay dirty there regardless of this fact.

   READ-ONLY AT RTL: this accessor never triggers the computation --
   the scan iterates gimple bodies and is only sound at gimple time
   (the pass's own first execution).  An uncomputed state answers
   false, the refusing direction.  */

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
