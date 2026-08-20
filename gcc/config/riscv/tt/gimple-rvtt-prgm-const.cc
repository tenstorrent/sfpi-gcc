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
   materialized SFPADD form of the same shape; and (laneDM widening)
   the canonical fused SFPMAD form those shapes lower to, per
   materialized-constant operand (mad_operand_candidates).  The pure
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
     structured-CC model makes function entry all-lanes, and the proof is
     that no fn-local CC-writing statement can execute before the
     programming point -- a CC writer whose block can reach the point
     (fn-local CFG reachability, computed backwards from the candidate
     loop's header so it covers the programming point and every block
     between a cross-loop hoisted point and the loop) refuses by name
     (cc-region-unproven).  A CC writer the point can never be reached
     from -- post-loop epilogue code, a sibling branch -- leaves the
     entry state provably intact on every path to the point, so it no
     longer defeats the proof (laneDM widening; the fn-entry-all-lanes
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
  /* Per-destination unique programmed value, when every TU write to
     that destination derives to the SAME 32-bit constant image
     (typed staged SFPCONFIG writes only; any claim from a raw word, a
     template, or an underivable staging chain clears the bit).  A
     residency candidate whose value equals a destination's unique TU
     value may REUSE that claimed register: every write anywhere
     stores the same value, and the candidate's own all-lanes
     programming makes the register hold it in every lane at every
     later point regardless of write order or the other writes' lane
     masks (value idempotence -- no cross-function ordering proof is
     needed or used).  */
  unsigned value_known = 0;	/* bitmask over destinations */
  uint32_t value[16] = {};
  /* The MOP template derivation facts (rvtt-mop-derive.h).  */
  rvtt_mop_derive_state mop;
};

static prgm_tu_facts tu_facts;

/* Defined with the rematerialization machinery below: the 32-bit
   constant image staged into a typed SFPCONFIG write, when its
   defining materialization has all-constant operands.  */
static bool staged_config_value (tree staged, unsigned *value);

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
  /* Census-escape discipline (lane CF): a BARE ADDR_EXPR of an
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
			     "prgm-const: on-demand scan of %s for call in %s\n",
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

static const prgm_tu_facts &
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

/* An in-loop invariant constant materialization defining SRC whose
   full 32-bit lane image is recoverable through the audited
   single-issue-chain derivation (single_issue_constant_image_p below:
   the sfpxloadi 31/32/-32 verbatim-image forms and the shortened
   SFPLOADI FLOATB form -- the same recovery the residency classes
   use).  Other encodings refuse (their value reconstruction is not on
   record).  */

static bool single_issue_constant_image_p (gcall *load, unsigned *value);

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
      || !single_nondebug_use_p (src, only_use)
      || !single_issue_constant_image_p (load, value))
    return nullptr;
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

/* The fused-MAD admission (laneDM widening): the canonical form the
   mul->add shapes lower to.  LHS = sfpmad (A, B, C, 0) computes
   per-lane A*B + C from its operand VALUES alone -- the plain mod has
   no implicit register pairing or operand reinterpretation -- so an
   operand defined by an in-loop invariant single-issue constant
   materialization used only by this statement can be parked in a PRGM
   register and read back: the constant-register read yields the
   identical 32-bit image in every lane the materialization wrote (the
   all-lanes proof for both is the same cc-region proof every class
   passes).  Each qualifying operand is its own candidate (the sdpa exp
   leg carries two).  Non-plain mods refuse by name -- their operand
   semantics are not audited here; the _lv variant is excluded (its
   lane-victim operand is not value-only).  Like the materialized
   SFPADD shape, no trip proof is required: the entry-edge programming
   is never speculated and establishment/no-clobber is
   trip-independent.  Appends candidates (without entry edges -- the
   caller places them) and returns how many.  */

static unsigned
mad_operand_candidates (gcall *call, class loop *loop,
			auto_vec<candidate> *out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfpmad)
    return 0;
  if (!gimple_call_lhs (call)
      || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
      || gimple_call_num_args (call) != 4)
    return 0;

  gcall *loads[3] = { nullptr, nullptr, nullptr };
  unsigned values[3] = { 0, 0, 0 };
  bool any = false;
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      loads[ix] = invariant_float_load_p (gimple_call_arg (call, ix), loop,
					  call, &values[ix]);
      any |= loads[ix] != nullptr;
    }
  if (!any)
    return 0;

  tree mod = gimple_call_arg (call, 3);
  if (TREE_CODE (mod) != INTEGER_CST || !integer_zerop (mod))
    {
      if (dump_file)
	fprintf (dump_file,
		 "prgm-const: sfpmad refused (mad-mod-unproven): a non-plain "
		 "mod's operand semantics are not audited here\n");
      return 0;
    }

  unsigned n = 0;
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      if (!loads[ix])
	continue;
      /* One candidate per materialization: mad (x, k, k) carries the
	 same load in two operand slots.  */
      bool dup = false;
      for (unsigned jx = 0; jx != ix; ++jx)
	dup |= loads[jx] == loads[ix];
      if (dup)
	continue;
      candidate c;
      c.addi = call;
      c.mul = nullptr;
      c.loadi = loads[ix];
      c.value = values[ix];
      c.loop = loop;
      c.entry = nullptr;
      out->safe_push (c);
      ++n;
    }
  return n;
}

/* Every CC-writing statement in FN, collected once per function.  */

static void
collect_cc_writers (function *fn, auto_vec<gimple *> *out)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (gsi_stmt (gsi));
	if (insnd && insnd->sets_cc (as_a <gcall *> (gsi_stmt (gsi))))
	  out->safe_push (gsi_stmt (gsi));
      }
}

/* Whether any CC writer in WRITERS can execute before the programming
   point (POINT_BB, and POINT_STMT within it when the point is a
   statement rather than the block entry).  The all-lanes proof needs
   the function-entry lane state to reach the point on every path; a
   fn-local CC writer defeats it exactly when some CFG path runs the
   writer and then reaches the point.  Reachability is block-granular
   (fail-closed over-approximation of "can execute before"): the reach
   set is computed backwards from POINT_BB's predecessors, so POINT_BB
   itself is in the set only when it lies on a cycle; a writer in
   POINT_BB outside any cycle defeats the proof exactly when it
   precedes POINT_STMT in the block (a block-entry point is defeated by
   any writer in the block).  Everything else about the proof -- the
   fn-entry-all-lanes model and call transparency -- is unchanged from
   the function-granular version.  */

static bool
cc_write_reaches_point_p (const auto_vec<gimple *> &writers,
			  basic_block point_bb, gimple *point_stmt)
{
  if (writers.is_empty ())
    return false;

  hash_set<basic_block> reach;
  auto_vec<basic_block, 16> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, point_bb->preds)
    work.safe_push (e->src);
  while (!work.is_empty ())
    {
      basic_block b = work.pop ();
      if (reach.add (b))
	continue;
      FOR_EACH_EDGE (e, ei, b->preds)
	work.safe_push (e->src);
    }

  for (gimple *w : writers)
    {
      basic_block wbb = gimple_bb (w);
      if (!wbb)
	continue;
      if (reach.contains (wbb))
	return true;
      if (wbb == point_bb)
	{
	  /* POINT_BB is not on a cycle (the reach test above would have
	     caught it): the writer executes before the point exactly
	     when it textually precedes it.  */
	  if (!point_stmt)
	    return true;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (wbb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      if (gsi_stmt (gsi) == w)
		return true;
	      if (gsi_stmt (gsi) == point_stmt)
		break;
	    }
	}
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
     two trips; a loop PROVEN single-trip refuses (a proven loss), and
     a runtime trip count is admitted (correctness is trip-independent;
     worst case one extra pushed word on a single-trip entry);
   - under LREG pressure (peak > SFPU_REG_NUM), an out-of-loop
     proven-constant value is reprogrammed in place: the programming
     writes replace the materialization and every use reads the
     constant register, which occupies NO allocatable LREG (the
     rvtt_sfpreadlreg expander emits a zero-cost cstlreg unspec for
     indices >= SFPU_CREG_IDX_LWM, folded into consumers by the unspec
     propagation passes).
   Selection is priced: in-loop candidates (per-iteration savings) rank
   above pressure-only candidates; within a class, more uses first.
   Refusals: prgm-exhausted, trip-count-single-trip, cc-region-unproven,
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

/* Forward-declared above for the fusion classes: the single-issue
   constant image of one materialization, through the same audited
   derivation as the residency chains.  */

static bool
single_issue_constant_image_p (gcall *load, unsigned *value)
{
  remat_chain chain { load, load };
  return constant_chain_value_p (chain, value);
}

/* The 32-bit constant image staged into a typed SFPCONFIG write:
   STAGED's defining statement must be a single-issue admitted
   materialization with all-constant operands (the same derivation the
   residency candidates use).  Underivable staging refuses -- the
   destination's TU value stays unknown and no reuse is offered.  */

static bool
staged_config_value (tree staged, unsigned *value)
{
  if (!staged || TREE_CODE (staged) != SSA_NAME)
    return false;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (staged));
  if (!def)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpxloadi
	  && insnd->id != rvtt_insn_data::sfploadi))
    return false;
  remat_chain chain { def, def };
  return constant_chain_value_p (chain, value);
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
	      if (!is_a <gcall *> (gsi_stmt (gsi)))
		continue;
	      gcall *call = as_a <gcall *> (gsi_stmt (gsi));
	      candidate c;
	      unsigned pushed = 0;
	      if (fusion_candidate_p (call, loop, &c))
		{
		  candidates.safe_push (c);
		  pushed = 1;
		}
	      else
		pushed = mad_operand_candidates (call, loop, &candidates);
	      if (!pushed)
		continue;
	      /* Under the cross-loop hoist, lift the programming
		 point to the outermost enclosing entry edge whose
		 region is audited-inert for the whole LREG file
		 (allocatable staging register plus the
		 programmable-constant destinations); the walk
		 returns ENTRY unchanged when nothing is proven.  */
	      edge point = riscv_tt_opt_crossloop_hoist > 0
		? rvtt_crossloop_outermost_entry (loop, entry, 0x7fff)
		: entry;
	      for (unsigned kx = candidates.length () - pushed;
		   kx != candidates.length (); ++kx)
		candidates[kx].entry = point;
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

  /* The all-lanes proof, scoped by reachability: a candidate refuses
     exactly when some fn-local CC writer can execute before its
     programming point.  One reach set from the candidate loop's header
     covers the (possibly cross-loop hoisted) entry-edge point and every
     block between it and the loop: the point can reach the header, so
     each of the point's CFG ancestors is an ancestor of the header
     too.  */
  {
    auto_vec<gimple *> cc_writers;
    collect_cc_writers (fn, &cc_writers);
    if (!cc_writers.is_empty ())
      {
	unsigned kept = 0;
	for (candidate &c : candidates)
	  {
	    if (cc_write_reaches_point_p (cc_writers, c.loop->header,
					  nullptr))
	      {
		if (dump_file)
		  fprintf (dump_file,
			   "prgm-const: loop bb %d refused "
			   "(cc-region-unproven): a CC write reaches the "
			   "programming point\n", c.loop->header->index);
		continue;
	      }
	    candidates[kept++] = c;
	  }
	candidates.truncate (kept);
	if (candidates.is_empty ())
	  return false;
      }
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
	  /* Materialized shape: the SFPADD or SFPMAD keeps its form
	     with the constant-register operand (every vector operand
	     slot equal to the materialization is rewritten -- a MAD
	     can carry the same constant twice); the in-loop
	     materialization is removed (its only use was this
	     statement).  */
	  tree load_lhs = gimple_call_lhs (c.loadi);
	  for (unsigned ix = 0; ix != gimple_call_num_args (c.addi) - 1; ++ix)
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
   a one-time three-word cost, refusing only a proven single trip:
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
  bool peel = false;		/* LOOP class: CC-canonical body; the
				   programming point is created by a
				   first-iteration peel at placement */
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

/* Classify the loop's trip count for the LOOP-class break-even by
   evaluating the single exit test on the first iteration.

   The classification prices; it never licenses.  Correctness of the
   LOOP class is trip-independent: the programming point sits on the
   never-speculated entry edge of the rotated loop (control reaching it
   executes the header at least once), the programmed register is
   established there before any replaced use, and nothing in the
   admitted loop body clobbers it (the sfpu-barrier/opaque gates refuse
   bodies with foreign effects) -- so residency holds on every entered
   iteration whatever the trip count.  What the bounded first-iteration
   evaluation decides is only which side of the two-trip break-even the
   loop is on:

   TRIPS_AT_LEAST_2 -- the exit test provably stays in the loop after
   the first trip: the two-trip break-even is proven and the programming
   strictly pays.
   TRIPS_PROVEN_SINGLE -- the exit test provably leaves the loop after
   the first trip: the one-time programming can never recover its cost;
   the candidate refuses by name (a proven loss).  Defensive: this
   evaluator folds a strict subset of what scalar evolution folds, so a
   constant single-trip loop has normally been flattened by complete
   unrolling long before this pass; the branch keeps the pricing
   fail-closed rather than relying on that pipeline fact.
   TRIPS_UNKNOWN -- the test does not fold (runtime trip counts, the
   dominant LLK loop shape): admitted.  The worst case is a single-trip
   entry costing one extra pushed word per candidate (programming is
   W+1 words against the W it saves on the executed iteration,
   rvtt-cost.md delivery model); every second trip onward is pure
   saving.  (The CC-canonical peel class is different: its peel
   DUPLICATES a body unconditionally, so it genuinely needs proven
   trips and keeps its own named refusal.)  */

enum loop_trip_class
{
  TRIPS_AT_LEAST_2,
  TRIPS_PROVEN_SINGLE,
  TRIPS_UNKNOWN
};

static loop_trip_class
classify_second_trip (class loop *loop, edge entry)
{
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1)
    return TRIPS_UNKNOWN;
  edge exit = exits[0];
  gimple_stmt_iterator last = gsi_last_bb (exit->src);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond)
    return TRIPS_UNKNOWN;
  tree lhs = first_iteration_value (loop, entry, gimple_cond_lhs (cond));
  tree rhs = first_iteration_value (loop, entry, gimple_cond_rhs (cond));
  if (!lhs || !rhs)
    return TRIPS_UNKNOWN;
  tree test = fold_binary (gimple_cond_code (cond), boolean_type_node,
			   lhs, rhs);
  if (!test || TREE_CODE (test) != INTEGER_CST)
    return TRIPS_UNKNOWN;
  edge true_edge, false_edge;
  extract_true_false_edges_from_block (exit->src, &true_edge, &false_edge);
  edge taken = integer_zerop (test) ? false_edge : true_edge;
  if (!taken)
    return TRIPS_UNKNOWN;
  return taken == exit ? TRIPS_PROVEN_SINGLE : TRIPS_AT_LEAST_2;
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

/* ------------------------------------------------------------------ */
/* CC-canonical loops: first-iteration peel (lane CF).

   The LOOP class above requires a CC-write-free loop (sfpu-barrier)
   and a CC-write-free function (cc-region-unproven), because its
   entry-edge programming executes under the loop-entry lane state and
   every replaced in-loop materialization must have executed under that
   SAME state.  The fresh-body kernels the storm lanes generate violate
   both: their row loop carries a lowered v_if region
   (SFPSETCC/SFPXFCMP* ... all-lanes SFPENCC) and re-materializes the
   loop-invariant paired-SFPLOADI constants every row -- the exact
   structural gap adjudicated by lane CE (log 23 vs 17 replay slots,
   sqrt 27 vs 21, rsqrt 33 vs 25 crossing the 32-slot replay cliff).

   For the CC-canonical single-block body (rvtt_loop_cc_canonical_body:
   the LAST CC writer on the unique linear path is the word-exact
   all-lanes SFPENCC), a first-iteration PEEL makes the residency
   transformation exact without any ambient lane-state assumption:

   - iteration one is duplicated statement for statement onto the entry
     edge (same statements, same order, same operand values), so its
     behavior -- including under an arbitrary unknown ambient CC mask --
     is reproduced bit for bit, and its trailing all-lanes SFPENCC
     leaves the machine in the architectural all-lanes state;
   - the staging SFPLOADI + SFPCONFIG programming is appended AFTER the
     peeled copy: it executes exactly when the loop continues past
     iteration one, in the proven all-lanes state (satisfying the
     architectural all-lanes requirement on SFPCONFIG: craq-sim
     tensix.cpp TENSIX_EXECUTE_SFPCONFIG verifies every lane enabled,
     and its lanewise LReg[0][lane & 7] copy needs the staged constant
     present in ALL of L0's lanes);
   - every remaining iteration k >= 2 begins in that same all-lanes
     state (the body's last CC writer is the all-lanes SFPENCC), so a
     candidate materialization placed BEFORE the body's first CC writer
     executed all-lanes there -- writing every lane with the constant --
     and the constant-register read that replaces it yields the
     identical value in every lane.  Iterations 2..N are therefore
     bit-exact as well.

   Profitability (rvtt-cost.md, residency-peel model): the peel
   re-delivers one body as RISC-pushed words (a delivery-class change
   worth (PUSH - SLOT) per word against the replayed loop it came from)
   and the programming costs PUSH per staged word and per SFPCONFIG;
   the loop saves the candidates' materialization words every remaining
   iteration.  The required trip count is proven by bounded forward
   evaluation of the rotated loop's own scalar control -- never assumed
   from profile data.  */

/* Post-shortening issue words of one admitted materialization: the
   single-issue sfploadi form is one word; the sfpxloadi form models the
   target's immediate encodings exactly as the invariant pass's
   materialization_cost (gimple-rvtt-invariant.cc) -- values with a free
   half or a FLOATA-encodable image issue once, everything else twice.
   No value identity participates; this reads only encoding structure.  */

static unsigned
loadi_issue_words (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (insnd->id == rvtt_insn_data::sfploadi)
    return 1;
  uint32_t value = TREE_INT_CST_LOW (gimple_call_arg (call, 1));
  unsigned upper = value >> 16;
  unsigned lower = value & 0xffff;
  if (!lower || !upper || (upper == 0xffff && (lower >> 15)))
    return 1;
  unsigned exponent = (value >> 23) & 0xff;
  return !(value & 0x1fff)
    && exponent > 127 - 15 && exponent < (127 - 15) + 31 ? 1 : 2;
}

/* Prove LOOP's body executes at least NEED times, by bounded forward
   evaluation of the single-block body's scalar control from the entry
   values (the same discipline as loop_second_trip_proven_p and the
   invariant pass's short-constant-loop proof; scalar evolution is
   unusable at this pipeline position).  Statements that do not fold
   simply leave their results unknown; the proof fails -- refusing --
   only when the exit test itself does not fold to a constant, when a
   header PHI's next value is unknown, or when the loop provably exits
   before NEED iterations.  */

static bool
loop_trips_at_least_p (class loop *loop, edge entry, unsigned need)
{
  if (need <= 1)
    return true;

  basic_block bb = loop->header;
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1 || exits[0]->src != bb)
    return false;
  edge exit = exits[0];
  edge latch_e = loop_latch_edge (loop);
  gimple_stmt_iterator last = gsi_last_bb (bb);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond || !latch_e)
    return false;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return false;

  /* Current values of the header PHIs (and, within an iteration, of
     folded body definitions).  A PHI whose entry value does not fold
     (e.g. a loop-carried vector) simply stays unknown; the proof fails
     only when the exit test itself needs an unknown value.  */
  hash_map<tree, tree> vals;
  auto_vec<gphi *, 4> phis;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (virtual_operand_p (res))
	continue;
      tree init = PHI_ARG_DEF_FROM_EDGE (phi, entry);
      if (!is_gimple_min_invariant (init))
	continue;
      phis.safe_push (phi);
      vals.put (res, init);
    }

  auto lookup = [&vals] (tree op) -> tree
    {
      if (!op || is_gimple_min_invariant (op))
	return op;
      if (TREE_CODE (op) != SSA_NAME)
	return NULL_TREE;
      tree *v = vals.get (op);
      return v ? *v : NULL_TREE;
    };

  bool proven = true;
  fold_defer_overflow_warnings ();
  for (unsigned trips = 1; trips < need; ++trips)
    {
      /* One pass over the body: fold what folds.  */
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gassign *a = dyn_cast <gassign *> (gsi_stmt (gsi));
	  if (!a)
	    continue;
	  tree lhs = gimple_assign_lhs (a);
	  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
	    continue;
	  tree_code code = gimple_assign_rhs_code (a);
	  tree type = TREE_TYPE (lhs);
	  tree op1 = lookup (gimple_assign_rhs1 (a));
	  tree op2 = gimple_num_ops (a) > 2
	    ? lookup (gimple_assign_rhs2 (a)) : NULL_TREE;
	  tree value = NULL_TREE;
	  switch (get_gimple_rhs_class (code))
	    {
	    case GIMPLE_SINGLE_RHS:
	      value = op1;
	      break;
	    case GIMPLE_UNARY_RHS:
	      value = op1 ? fold_unary (code, type, op1) : NULL_TREE;
	      break;
	    case GIMPLE_BINARY_RHS:
	      value = op1 && op2
		? fold_binary (code, type, op1, op2) : NULL_TREE;
	      break;
	    default:
	      value = NULL_TREE;
	      break;
	    }
	  if (value && is_gimple_min_invariant (value))
	    vals.put (lhs, value);
	  else
	    vals.remove (lhs);
	}

      tree lhs = lookup (gimple_cond_lhs (cond));
      tree rhs = lookup (gimple_cond_rhs (cond));
      tree test = lhs && rhs
	? fold_binary (gimple_cond_code (cond), boolean_type_node, lhs, rhs)
	: NULL_TREE;
      if (!test || TREE_CODE (test) != INTEGER_CST)
	{
	  proven = false;
	  break;
	}
      edge taken = integer_zerop (test) ? false_edge : true_edge;
      if (taken == exit)
	{
	  proven = false;	/* provably exits before NEED trips */
	  break;
	}

      /* Advance the tracked header PHIs through the latch; one whose
	 next value does not fold merely stops being tracked.  */
      auto_vec<tree, 4> next;
      for (gphi *phi : phis)
	next.safe_push (lookup (PHI_ARG_DEF_FROM_EDGE (phi, latch_e)));
      /* Body definitions do not survive the backedge.  */
      vals.empty ();
      unsigned kept = 0;
      for (unsigned ix = 0; ix != phis.length (); ++ix)
	if (next[ix])
	  {
	    vals.put (gimple_phi_result (phis[ix]), next[ix]);
	    phis[kept++] = phis[ix];
	  }
      phis.truncate (kept);
    }
  fold_undefer_and_ignore_overflow_warnings ();
  return proven;
}

/* Duplicate LOOP's single-block body once onto its entry edge and
   return the loop's new entry edge (peeled block -> header), on which
   the caller places the constant programming.  Every proof has already
   passed: the body is CC-canonical (only typed RVTT calls, audited raw
   Dst/RWC words, pure assignments, PHIs, labels, debug statements and
   the loop condition), and the bounded trip evaluation proved the
   first iteration never exits -- the peeled copy therefore falls
   through to the loop unconditionally and the copied exit test is
   dropped (its scalar chain is still copied; the header PHIs consume
   it).  Header PHIs evaluate to their entry arguments inside the copy
   and are re-seeded with the copy's latch values.  Virtual operands on
   the copies are cleared for the pass-level virtual-SSA update.  */

static edge
peel_first_iteration (class loop *loop, edge entry)
{
  basic_block bb = loop->header;
  edge latch_e = loop_latch_edge (loop);

  hash_map<tree, tree> map;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (!virtual_operand_p (res))
	map.put (res, PHI_ARG_DEF_FROM_EDGE (phi, entry));
    }

  basic_block copy_bb = split_edge (entry);
  gimple_stmt_iterator at = gsi_start_bb (copy_bb);

  auto remap = [&map] (tree op) -> tree
    {
      if (op && TREE_CODE (op) == SSA_NAME)
	if (tree *found = map.get (op))
	  return *found;
      return op;
    };

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;

      gimple *cp = gimple_copy (stmt);

      /* Remap operands, then give each definition a fresh name.  */
      if (gcall *call = dyn_cast <gcall *> (cp))
	{
	  for (unsigned ix = 0; ix != gimple_call_num_args (call); ++ix)
	    gimple_call_set_arg (call, ix, remap (gimple_call_arg (call, ix)));
	}
      else if (gassign *a = dyn_cast <gassign *> (cp))
	{
	  for (unsigned ix = 1; ix != gimple_num_ops (a); ++ix)
	    gimple_set_op (a, ix, remap (gimple_op (a, ix)));
	}
      /* Audited raw Dst/RWC words carry one constant input: nothing to
	 remap and nothing defined.  */

      if (tree lhs = gimple_get_lhs (cp))
	{
	  gcc_assert (TREE_CODE (lhs) == SSA_NAME);
	  tree fresh = make_ssa_name (TREE_TYPE (lhs));
	  gimple_set_lhs (cp, fresh);
	  map.put (lhs, fresh);
	}

      if (gimple_vdef (cp))
	gimple_set_vdef (cp, NULL_TREE);
      if (gimple_vuse (cp))
	gimple_set_vuse (cp, NULL_TREE);

      if (gsi_end_p (at))
	{
	  gsi_insert_before (&at, cp, GSI_NEW_STMT);
	  at = gsi_for_stmt (cp);
	}
      else
	gsi_insert_after (&at, cp, GSI_NEW_STMT);
      update_stmt (cp);
    }

  /* The loop's entry values are now the peeled iteration's latch
     values.  */
  edge new_entry = single_succ_edge (copy_bb);
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      if (virtual_operand_p (gimple_phi_result (phi)))
	continue;
      tree larg = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
      SET_USE (PHI_ARG_DEF_PTR_FROM_EDGE (phi, new_entry), remap (larg));
    }

  if (dump_file)
    fprintf (dump_file,
	     "const-residency: peeled first iteration of loop bb %d into "
	     "bb %d (CC-canonical body; programming point follows the "
	     "peeled all-lanes SFPENCC)\n",
	     bb->index, copy_bb->index);
  return new_entry;
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
      bool peel = false;
      gimple *cc_limit = nullptr;
      const char *why
	= !entry ? "no-single-entry"
	: rvtt_loop_hoist_region_opaque_p (loop, entry) ? "opaque-hoist-region"
	: rvtt_preheader_insertion_blocked_p (entry) ? "preheader-blocked"
	: nullptr;
      if (!why && rvtt_loop_has_sfpu_barrier_p (loop))
	{
	  /* CC-canonical rescue (lane CF): a single-block body whose
	     only barrier statements are CC writers ending in the
	     word-exact all-lanes SFPENCC admits the first-iteration
	     peel below; candidates must precede the body's first CC
	     writer.  Anything else keeps the barrier refusal
	     byte-identically.  */
	  rvtt_cc_canonical_body canon = rvtt_loop_cc_canonical_body (loop);
	  if (canon.proven)
	    {
	      peel = true;
	      cc_limit = canon.first_cc_writer;
	    }
	  else
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "const-residency: loop bb %d cc-canonical proof "
			 "failed (%s)\n", loop->header->index, canon.why);
	      why = "sfpu-barrier";
	    }
	}
      if (why)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d refused (%s)\n",
		     loop->header->index, why);
	  continue;
	}

      /* Profitability: the entry-edge programming (W+1 pushed words
	 once) pays for itself against the W pushed SFPLOADI words
	 saved per iteration at two trips (rvtt-cost.md delivery
	 model).  Correctness is trip-independent -- see
	 classify_second_trip -- so only a PROVEN single-trip loop
	 refuses (a proven loss); a runtime trip count is admitted with
	 a worst case of one extra pushed word on a single-trip entry.
	 (The CC-canonical peel class prices its peel separately below
	 and genuinely needs proven trips for the peel itself.)  */
      if (!peel)
	switch (classify_second_trip (loop, entry))
	  {
	  case TRIPS_AT_LEAST_2:
	    break;
	  case TRIPS_PROVEN_SINGLE:
	    if (dump_file)
	      fprintf (dump_file,
		       "const-residency: loop bb %d refused "
		       "(trip-count-single-trip: the loop provably runs "
		       "one trip; the programming can never recover its "
		       "cost)\n", loop->header->index);
	    continue;
	  case TRIPS_UNKNOWN:
	    if (dump_file)
	      fprintf (dump_file,
		       "const-residency: loop bb %d admits runtime trips "
		       "(entry-edge programming is never speculated; "
		       "establishment and no-clobber are trip-independent; "
		       "worst case one extra pushed word per candidate on "
		       "a single-trip entry)\n", loop->header->index);
	    break;
	  }

      auto_vec<residency_candidate> this_loop;
      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
	    continue;
	  bool cc_reached = false;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      /* Peel class: a candidate at or after the body's first
		 CC writer executed under the v_if region's partial
		 lane state; only the pre-region prefix is proven
		 all-lanes on iterations 2..N.  */
	      if (cc_limit && gsi_stmt (gsi) == cc_limit)
		cc_reached = true;
	      if (cc_reached)
		break;
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
	      /* Same outermost audited placement as the fusion class.  */
	      c.entry = riscv_tt_opt_crossloop_hoist > 0
		? rvtt_crossloop_outermost_entry (loop, entry, 0x7fff)
		: entry;
	      c.uses = count_nondebug_uses (gimple_call_lhs (load));
	      c.peel = peel;
	      this_loop.safe_push (c);
	    }
	}
      free (body);

      /* Peel pricing (rvtt-cost.md, residency-peel model): the loop
	 saves the candidates' materialization words at SLOT each on
	 every iteration after the first; the programming costs PUSH
	 per staged word plus PUSH per SFPCONFIG; the peeled body's
	 words change delivery class from replayed SLOT to pushed PUSH
	 once.  All constants are the established delivery-economics
	 table values; the required trip count is proven by bounded
	 evaluation of the loop's own scalar control.  */
      if (peel && !this_loop.is_empty ())
	{
	  unsigned sum_w = 0;
	  for (residency_candidate &c : this_loop)
	    sum_w += loadi_issue_words (c.load);
	  unsigned nprog = this_loop.length ();
	  unsigned body_w = 0;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (loop->header);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	      if (!insnd)
		body_w += gimple_code (stmt) == GIMPLE_ASM;
	      else if (insnd->id == rvtt_insn_data::sfpxloadi
		       || insnd->id == rvtt_insn_data::sfploadi)
		body_w += loadi_issue_words (as_a <gcall *> (stmt));
	      else
		++body_w;
	    }
	  unsigned push = XTT_REPLAY_COST_RISC_PUSH_X100;
	  unsigned slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
	  unsigned cost = push * (sum_w + nprog) + (push - slot) * body_w;
	  unsigned need = 1 + (cost + slot * sum_w - 1) / (slot * sum_w);
	  if (need > 64 || !loop_trips_at_least_p (loop, entry, need))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "const-residency: loop bb %d refused "
			 "(peel-trip-count-unproven: break-even needs %u "
			 "proven trips; %u candidate words, %u programming "
			 "words, %u-word body)\n",
			 loop->header->index, need, sum_w, sum_w + nprog,
			 body_w);
	      continue;
	    }
	  if (dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d admits the CC-canonical "
		     "peel (%u candidate words/iteration, break-even %u "
		     "trips proven)\n",
		     loop->header->index, sum_w, need);
	}

      for (residency_candidate &c : this_loop)
	{
	  loop_cands.safe_push (c);
	  taken.add (c.load);
	}
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
  {
    auto_vec<gimple *> cc_writers;
    collect_cc_writers (fn, &cc_writers);
    if (!cc_writers.is_empty ())
      {
	/* The all-lanes proof, scoped by reachability: a candidate
	   refuses exactly when some fn-local CC writer can execute
	   before its programming point (the same reach-set cover as
	   the fusion class: the loop header's CFG ancestors include
	   the hoisted entry point's).  The CC-canonical peel class is
	   exempt: its programming point is placed after the peeled
	   iteration's own all-lanes SFPENCC, and every replaced
	   materialization is proven to have executed in that same
	   architectural state -- both facts are local to the peeled
	   loop and independent of other CC writes in the function.
	   The pressure class's point is its own in-place statement.  */
	unsigned kept = 0;
	for (residency_candidate &c : loop_cands)
	  {
	    if (!c.peel
		&& cc_write_reaches_point_p (cc_writers, c.loop->header,
					     nullptr))
	      {
		if (dump_file)
		  fprintf (dump_file,
			   "const-residency: loop bb %d refused "
			   "(cc-region-unproven): a CC write reaches the "
			   "programming point\n", c.loop->header->index);
		continue;
	      }
	    loop_cands[kept++] = c;
	  }
	loop_cands.truncate (kept);
	kept = 0;
	for (residency_candidate &c : pressure_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		if (dump_file)
		  fprintf (dump_file,
			   "const-residency: pressure candidate in bb %d "
			   "refused (cc-region-unproven): a CC write reaches "
			   "the in-place programming point\n",
			   gimple_bb (c.load)->index);
		continue;
	      }
	    pressure_cands[kept++] = c;
	  }
	pressure_cands.truncate (kept);
	if (loop_cands.is_empty () && pressure_cands.is_empty ())
	  {
	    if (dump_file)
	      fprintf (dump_file, "const-residency: refused (cc-region-unproven)"
		       " -- in-function CC writes reach every candidate"
		       " programming point; cross-call ambient proof is not on"
		       " record here\n");
	    return false;
	  }
      }
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
  hash_map<class loop *, edge> peeled;
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
      /* TU value-identical reuse: a claimed destination whose EVERY
	 TU write derives to this candidate's exact 32-bit value may
	 be reused.  Soundness is value idempotence, not ordering:
	 every write anywhere stores the same value, and the
	 candidate's own all-lanes programming (still emitted below)
	 puts that value in every lane; any interleaved lane-predicated
	 write of the same value preserves it.  No cross-function
	 ordering or dominance is used.  */
      bool tu_reuse = false;
      if (!prgm)
	{
	  const prgm_tu_facts &tu = tu_prgm_facts ();
	  for (unsigned reg : prgm_regs)
	    if ((tu.value_known & (1u << reg)) && tu.value[reg] == c.value)
	      {
		prgm = reg;
		tu_reuse = true;
		break;
	      }
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
      if (tu_reuse && dump_file)
	fprintf (dump_file,
		 "const-residency: reusing TU-programmed PRGM L%u (every "
		 "TU write stores 0x%08x; programming is value-idempotent)\n",
		 prgm, c.value);

      /* CC-canonical class: the programming point is the fall-through
	 edge of the peeled first iteration (one peel per loop; later
	 candidates of the same loop share it).  Peeling happens only
	 here, after a register has actually been allocated, so refused
	 candidates never mutate the CFG.  */
      if (c.peel)
	{
	  if (edge *found = peeled.get (c.loop))
	    c.entry = *found;
	  else
	    {
	      c.entry = peel_first_iteration (c.loop, c.entry);
	      peeled.put (c.loop, c.entry);
	    }
	}

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
