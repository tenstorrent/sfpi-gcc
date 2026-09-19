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

/* A noinline per-tile callee that consumes a set of call-invariant
   immediate materializations through instruction operands the target
   pins to specific hard LREGs (the SFPLUTFP32 coefficient file is the
   canonical case) re-executes that whole prefix on EVERY call, although
   the values provably cannot change between calls.  A hand kernel loads
   such coefficients once at kernel init and carries them across calls
   in the architectural registers -- the production `l_reg' write/read
   idiom (sfpwritelreg/sfpreadlreg), whose cross-call semantics this
   translation unit's own init code already relies on.

   This pass automates exactly that idiom, PROVING every link the hand
   author asserts:

     callee:  c = sfpxloadi (CST)  ... consumer (c pinned to LREG k)
       ==>
     callee:  c = sfpreadlreg (k)  ... consumer
	      sfpwritelreg (c, k) before every return
     caller:  t = sfpxloadi (CST); sfpwritelreg (t, k)
	      in the enclosing loop's dedicated preheader
	      (executed exactly when the loop is entered).

   The zero-length read/write markers pin register allocation on both
   sides: in the callee the value is live -- in hard LREG k at every
   architecturally pinned point -- from function entry to every return,
   so no register-allocation decision can leave a call with the
   register clobbered; in the caller the write marker forces the loaded
   value into LREG k before the loop.  What the markers cannot pin is
   the CALLER'S code between the loads and the calls (and around the
   loop's backedge): that epoch must be proven LREG-clean, the same
   epoch discipline rvtt-macro-epoch.cc established for the SFPCONFIG
   descriptor words, extended across the call edge.

   Pipeline position: right after pass_rvtt_lut_select has formed the
   coefficient prefix in the callee and before pass_rvtt_expand lowers
   the builtin forms.  Callees run the late pipeline before their
   callers (cgraph topological order -- the ordering fact the MOP
   outward-ownership proof in rtl-rvtt-mop-form.cc relies on), so every
   caller body is still gimple here, and both sides of the transform
   are committed together or not at all.

   Proof obligations (each refuses by name; a refusal never edits
   anything):

   [callee]
   - every hoist candidate is an SFPU immediate materialization with
     all-constant operands and the canonical instruction-buffer operand
     (the same qualification the invariant/LUT placement proofs use),
     defined outside any loop;
   - every use of the candidate's value is an operand of a typed rvtt
     call whose RTL pattern constrains that operand to one specific
     hard LREG; the constraint is read from the pattern's own
     insn_data operand constraint (the md is the authority; nothing is
     keyed on operation identity), and all uses agree on the register;
   - the callee carries no CC-writing statement (so its exit CC state
     equals its entry CC state and the per-call load's lane predicate
     is the same on every call -- the same structured-CC entry model
     gimple-rvtt-prgm-const.cc documents);
   - no explicit lreg read/write builtin or raw-LREG access marker
     touches a contract register (a conflicting hand contract), no
     call, no unaudited assembly or delivered word, and no vector
     statement outside the candidate loads and loop bodies (the
     liveness-extension tail must be vector-free);
   - keeping the contract values live across the consumer loop holds
     the loop within the eight-LREG file (the shared pressure proof).

   [caller, for EVERY cgraph caller -- all-or-nothing]
   - the callee's cgraph node is a definition, not address-taken, not
     aliased/thunked/cloned, and every call site is a direct edge.
     Under AXIOM kernel-single-TU (rtl-rvtt-mop-form.cc: one
     translation unit per TRISC image, the harness link model) the TU's
     edges are ALL the calls that can execute;
   - the (single, v1) call site sits in a natural loop with a unique
     entry edge; the hoisted loads land on that edge (dedicated
     preheader or commit-time split), so they execute exactly when the
     loop is ENTERED.  Entering the loop does NOT imply the body runs:
     on a zero-trip path (guard in the header) the hoisted loads
     execute although the original per-call prefix never would, so the
     contract registers ARE written where the original left them
     untouched.  The actual soundness argument is that this zero-trip
     clobber is unobservable: (a) no vector SSA value of the caller is
     live anywhere in the loop (proven below), and every path from the
     insertion point reaches the exit only through the loop header, so
     the caller's register allocation cannot hold any live value in a
     contract register across the clobber; (b) the explicit
     architectural LREG interfaces -- the only contract-carrying
     readers of residual register state under the no-residual-contents
     model (rtl-rvtt-replay.cc: SFPU register state is not an implicit
     cross-function interface; an explicit hand-off is an lreg
     builtin) -- are refused inside the loop wholesale and, for reads
     of a contract register, anywhere else in the caller
     (crosscall-caller-foreign-contract); (c) any execution that DID
     enter the loop body already had the callee's per-call
     materializations write the same constants to the same registers,
     so no downstream reader can distinguish the hoisted write from
     the state every trip-taking execution always produced;
   - every statement of the loop body is proven LREG-inert for the
     contract registers: scalar code, audited scalar asm, typed rvtt
     calls with no vector dataflow (at gimple, hard LREGs are touched
     only by register allocation over vector values, by the explicit
     lreg builtins, and by delivered instruction words -- each class
     checked here; the only typed builtins that expand OTHER
     instructions are the replay launches, which refuse), and
     delivered words (raw `.ttinsn', instruction-FIFO stores, the
     blocking-store asm idiom) whose audited opcode class cannot write
     an allocatable LREG.  A MOP word defers to the TU-wide template
     audit below; a REPLAY word refuses (recorded content is not
     derivable here);
   - no CC-writing statement in the loop (the hoisted lane-predicated
     loads must execute under the same lane-enable state as the
     original per-call loads: caller-entry state reaches the preheader
     and every call site unchanged);
   - no vector SSA value of the caller is live anywhere in the loop
     (the caller's own register allocation knows nothing of the
     contract, so any caller vector value overlapping the loop could
     be allocated to a contract register).

   [TU, consulted only when a MOP word is delivered in a scanned range]
   - the census walks every body in the TU's EXECUTABLE CLOSURE,
     rooted at everything the link image can enter from outside the
     TU under the link model (AXIOM extern-fixed-surface): the entry
     anchor -- `_start' when the TU carries it, else `main' (the crt0
     entry; the wave-8 production shape this census used to unroot),
     else every externally-visible non-comdat definition (firmware ->
     run_kernel) -- plus asm/vector-callable forced definitions,
     static constructors/destructors, address-taken definitions, and
     every function a variable initializer references; membership
     propagates through call edges and references.  A TU with defined
     bodies and NO root fails closed (crosscall-census-unrooted): a
     census that can see no entry can vouch for nothing;
   - every store anywhere in the executable closure that can reach the
     MOP template file (constant-address stores into the architected
     nine words; volatile stores whose address cannot be proven
     elsewhere fail closed) programs an instruction slot with a
     constant word whose
     audited class cannot write an allocatable contract LREG.  Address
     facts and slot semantics are the recorded facts of
     rvtt-mop-tables.h; the word classes mirror the audited raw-word
     table of rvtt-mop-derive.cc (provenance recorded there);
     SFPLOADI's destination field (bits 23:20) is the one audited
     class that writes an allocatable LREG and is checked against the
     contract.

   Refusal taxonomy (dump-stable names):
     crosscall-callee-shape-unproven    no qualifying prefix load set
     crosscall-consumer-not-pinned      a value use is not a pinned
					constraint operand
     crosscall-consumer-conflict	two values pin the same LREG (or
					one value two different LREGs)
     crosscall-callee-cc-unproven	CC-writing statement in the callee
     crosscall-callee-clobber		explicit lreg builtin / raw-access
					marker on a contract register
     crosscall-callee-stmt-unproven	call/asm in the callee not proven
					contract-inert
     crosscall-callee-word-unproven	delivered word in the callee not
					audited contract-inert
     crosscall-callee-replay-unproven	REPLAY word delivered in the
					callee
     crosscall-callee-vector-outside-loop vector statement in the
					liveness-extension tail
     crosscall-callee-pressure		eight-LREG file exceeded
     crosscall-caller-body-unavailable	caller not analyzable (no gimple
					body, address-taken, alias/clone,
					recursion)
     crosscall-caller-multi-site	more than one call site in one
					caller (v1 scope)
     crosscall-caller-no-loop		call site not inside a loop
					(nothing to amortize)
     crosscall-caller-preheader-unproven no unique loop entry edge or
					blocked insertion point
     crosscall-caller-stmt-unproven	loop statement not proven
					contract-inert
     crosscall-caller-cc-unproven	CC-writing statement in the loop
     crosscall-caller-lreg-live		caller vector value live in the
					loop
     crosscall-caller-word-unproven	delivered word not audited
					contract-inert
     crosscall-caller-replay-unproven	REPLAY word delivered in the loop
     crosscall-caller-mop-slot-unproven	MOP delivered in the loop but the
					TU template audit failed
     crosscall-caller-foreign-contract	explicit lreg read (or raw-access
					marker naming a read) of a contract
					register in the caller outside the
					loop: a residual-contents observer
					the zero-trip clobber cannot be
					ordered against
     crosscall-caller-unrooted		caller body outside the TU
					executable closure: the census
					cannot vouch for it
     crosscall-census-unrooted		defined bodies but no census root
					(no entry / constructor /
					externally-visible symbol): the
					whole template audit fails closed

   Config-prefix widening (-mtt-tensix-optimize-crosscall-config-prefix):
   a callee prefix pair -- a qualifying materialization whose
   SINGLE consumer is sfpwriteconfig_v to a programmable-constant
   register 11..14 (never allocatable; audited-table provenance)
   -- joins the contract: re-materialized in every proven caller's loop
   preheader AHEAD of the contract loads (the SFPCONFIG source operand
   is md-pinned to L0) and deleted from the callee.  The caller proofs
   widen: the scan mask gains the programmed register (SFPLOADI
   destinations, explicit lreg reads/writes, raw-access markers),
   delivered SFPCONFIG-class words refuse outright (config_strict), and
   the MOP template slots must be config-word-free.  Zero-trip
   soundness is the coefficient contract's own argument with the
   register file swapped: the destination is not allocatable, explicit
   reads outside the loop refuse (crosscall-caller-foreign-contract),
   and every trip-taking execution already wrote this constant to this
   register.  Pair disqualifications (dump notes, behavior falls back
   to the pre-flag refusals byte-identically):
     crosscall-config-dest-unproven	destination not a constant 11..14
     crosscall-config-writer-unproven	another writer of a pair register
					in the callee
     crosscall-config-shape-unproven	pair does not dominate every
					return
     mop-template-config-word-unproven	an audited MOP template slot
					holds an SFPCONFIG-class word
   QSR refuses by pass gate (no validated capability).  */

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

/* ------------------------------------------------------------------ */
/* Refusal plumbing.						      */

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

/* ------------------------------------------------------------------ */
/* Builtin -> RTL pattern bridge.  The rvtt builtins map 1:1 onto
   their md patterns (riscv-builtins.cc builds the same table); the
   pattern's operand constraints are the authority for hard-LREG
   pinning.  QSR overrides re-point some entries, but this pass gates
   QSR off entirely.  */

static const enum insn_code rvtt_builtin_icode[] = {
#define RVTT_FN(id, av, sfx, fmt, fl, ops) CODE_FOR_rvtt_##id##sfx,
#include "rvtt-insn.def"
#undef RVTT_FN
};

/* If the (gimple) argument index ARGNO of a call to rvtt insn INSND is
   constrained by the md pattern to exactly one hard LREG, return that
   register number, else -1.  The gimple arguments of a plain rvtt
   builtin map positionally onto the pattern operands after the output
   (riscv_expand_builtin's direct expansion); anything whose operand
   count disagrees is not a plain direct builtin and refuses.  */

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

/* ------------------------------------------------------------------ */
/* Shared small predicates.					      */
/* A qualifying prefix load: the canonical sfpxloadi form or the
   shortened single-issue sfploadi form (this pass runs after
   pass_rvtt_immload_shorten, like the LUT coefficient placement), with
   the canonical buffer operand and all-constant scalar operands.  */

static bool
prefix_load_p (gcall *call)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpxloadi
	  && insnd->id != rvtt_insn_data::sfploadi))
    return false;
  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME
      || !rvtt_canonical_buffer_arg_p (gimple_call_arg (call, 0)))
    return false;
  for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
    if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
      return false;
  return true;
}

/* Return true if T is a value of vector type (an SFPU vector datum).
   Null-safe: NULL_TREE (e.g. a missing lhs) is not.  */

bool
vector_typed_p (tree t)
{
  return t && TREE_TYPE (t) && VECTOR_TYPE_P (TREE_TYPE (t));
}

/* Any vector-typed operand (lhs or argument) on a call.  */

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

/* ------------------------------------------------------------------ */
/* Statement classification for the caller's loop epoch (and the
   callee's own body): every statement must be proven unable to write
   a contract LREG.  */

/* The audited scalar asm templates: base-ISA instructions with no Tensix
   encoding space.

   WARNING: this is one of FOUR copies of this soundness allowlist --
   rvtt-macro-epoch.cc, gimple-rvtt-prgm-const.cc and rtl-rvtt-mop-form.cc
   carry their own.  They are NOT identical and must not be assumed so.
   This copy is the strictest: it omits the pcbuf/mailbox
   store-load-consume idiom that the epoch copy admits, and it compares
   with strcmp where the epoch copy uses whitespace-tolerant
   asm_template_eq.  Being strictest, it fails closed, which is the safe
   direction -- but adding a template here does NOT add it elsewhere, and
   adding one elsewhere does not add it here.  Consolidating these into one
   shared predicate is filed work; until then, change all four or none.  */

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

/* Record a refusal.  The word/replay/statement classifiers share one
   code path for both scan sides; the dump name carries the side.  */

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

/* Apply a delivered-word verdict.  */

static bool
apply_word_verdict (scan_ctx *ctx, const word_verdict &v, gimple *stmt)
{
  if (v.is_mop)
    ctx->saw_mop = true;
  if (!v.ok)
    return scan_refuse (ctx, v.why, stmt);
  return true;
}

/* One asm statement.  */

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
	 FIFO; resolve the address where possible, else classify the
	 word itself (refusing default covers both).  */
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

/* One store statement.  */

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
	/* Re-programming template slots inside the epoch: the written
	   word joins the TU census anyway, but a slot write inside the
	   scanned range plus a MOP launch is exactly the re-arm case;
	   keep it simple and refuse (no wired row needs it).  */
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

/* Structured typed CC atom (the cc-immaterial region
   discipline): a typed RVTT call whose WHOLE architectural effect is
   the SFPU CC/lane-enable state plus its SSA-visible definition.  Such
   a statement cannot touch a programmable constant register, deliver a
   word, or write a hard LREG -- so it is immaterial to a
   programming-only placement that executes BEFORE the scanned region
   and whose parked state lives in a claimed constant register.
   Fail-closed whitelist by insn id (every CC()-marked entry of
   rvtt-insn.def today is such an atom, but a future CC-marked insn
   with additional effects must not ride this admission silently).  */

static bool
crossloop_cc_atom_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
      return true;
    default:
      return false;
    }
}

/* One statement of a scanned range.  IN_CALLER selects the caller-loop
   discipline (the contract call is admitted; vector dataflow refuses);
   the callee scan admits vector dataflow (register allocation resolves
   it against the pinned live range) but refuses the same delivered
   words, calls, and explicit-contract accesses.  */

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
				    /* The general-region widening was
				       live and the tree could not prove
				       the loop: its own name.  */
				    ? "crossloop-cc-ambient-unproven"
				    : in_caller
				    ? "crosscall-caller-cc-unproven"
				    : "crosscall-callee-cc-unproven", stmt);
	      /* Programming-only discipline (cc_immaterial):
		 a structured typed CC atom changes only the lane-enable
		 state and its own SSA definition; the consumer's lifted
		 placement executes before this region and parks state
		 in a claimed constant register no CC write can reach.
		 Tree-proven discipline (cc_ambient_ok): the loop's
		 CC activity is ambient-preserving-and-
		 narrowing, so the enable set at every in-loop consumer
		 stays a subset of the lifted entry's -- the hoisted
		 all-lanes materialization is a refinement.  Either way
		 admit the whole statement (its side effect IS the CC
		 write); a CC writer off the whitelist refuses by
		 name.  */
	      if (!crossloop_cc_atom_p (insnd))
		return scan_refuse (ctx, "crossloop-cc-atom-unproven", stmt);
	      return true;
	    }
	  /* The audited hoist-region discipline keeps the invariant
	     pass's side-effect boundary: a typed call with target side
	     effects beyond the explicit Dst load/store/counter set is
	     not proven inert for a hoisted live range crossing it
	     (mirrors allowed_dst_effect_p, gimple-rvtt-invariant.cc).  */
	  if (ctx->region
	      && insnd->has_side_effects (call)
	      && insnd->id != rvtt_insn_data::sfpload
	      && insnd->id != rvtt_insn_data::sfpload_lv
	      && insnd->id != rvtt_insn_data::sfpstore
	      && insnd->id != rvtt_insn_data::ttincrwc
	      && insnd->id != rvtt_insn_data::ttdstface
	      /* The plain sfppushc (0) carries no CC-write effect under
		 its mod encoding (sets_cc is false), so it lands here:
		 under the R2 tree-proven loop fact it is exactly the
		 save the proof leans on -- admit it there (the popc
		 side is sets_cc and goes through the CC arm above).
		 Without the fact the standing refusal is unchanged.  */
	      && !(ctx->cc_ambient_ok
		   && insnd->id == rvtt_insn_data::sfppushc)
	      /* These reach their dedicated arms in the switch below
		 (replay refusal; masked hard-LREG access checks).  */
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
		    /* Register allocation sees a typed hard-LREG read
		       and coordinates around it; only a WRITE into the
		       audited mask clobbers a hoisted live range.  */
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
		  /* An explicit foreign lreg contract in the epoch:
		     even off-contract registers signal a hand protocol
		     this proof does not order against.  */
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
	      /* Plays back recorded slots; recorded content is not
		 derivable here.  */
	      return scan_refuse (ctx, "crosscall-caller-replay-unproven",
				  stmt);
	    default:
	      break;
	    }
	  if (in_caller && call_has_vector_dataflow_p (call))
	    /* Vector dataflow in the caller's epoch: its LREG identity
	       is a register-allocation decision the contract cannot
	       see.  */
	    return scan_refuse (ctx, "crosscall-caller-stmt-unproven", stmt);
	  /* Typed rvtt calls with no vector dataflow cannot name an
	     LREG: at gimple, hard LREGs are reached only through
	     register allocation of vector values, the explicit lreg
	     builtins, the raw-access marker, and expanded/delivered
	     words -- each class handled above (rvtt-insn.def audit:
	     the replay launch is the only typed expander builtin).  */
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

/* ------------------------------------------------------------------ */
/* Caller-side vector liveness: a vector SSA value live anywhere in
   LOOP could be allocated a contract LREG by the caller's own
   register allocation.  Exact backward reachability per name: V is
   live at a block B iff some use of V is reachable from B without
   passing V's definition.  */

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

/* ------------------------------------------------------------------ */
/* The candidate contract discovered in the callee.		      */

struct contract_entry
{
  gcall *load;			/* the prefix materialization	     */
  tree value;			/* its SSA lhs			     */
  int lreg;			/* the pinned hard LREG		     */
};

/* A config-prefix pair (-mtt-tensix-optimize-crosscall-
   config-prefix): a qualifying prefix materialization whose SINGLE
   consumer programs a programmable-constant register (SFPCONFIG
   destinations 11..14 -- never allocatable, audited-table
   provenance in rvtt-lut-tables.cc).  The pair joins the contract:
   re-materialized once in every proven caller's loop preheader (ahead
   of the contract loads -- the SFPCONFIG source operand is pinned to
   the same L0 the coefficient contract may use) and deleted from the
   callee.  Soundness mirrors the coefficient contract's zero-trip
   argument with the register file swapped: the destination register
   is not allocatable, the widened caller proofs refuse every
   statement or delivered word able to write it inside the loop (and
   any explicit read of it outside the loop), and every trip-taking
   execution already produced exactly this value in this register.  */

struct config_prefix_entry
{
  gcall *load;			/* the prefix materialization	     */
  gcall *write;			/* its single use: sfpwriteconfig_v  */
  unsigned dest;		/* the programmed register, 11..14   */
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

/* Discover the contract in FN.  Returns true with CONTRACT filled (at
   least one entry) and the consumer loop through *CONSUMER_LOOP.  */

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
	  contract_entry e = { load, lhs, lreg };
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

/* Discover the callee's config-prefix pairs (flag-gated by the
   caller).  Refusing default: a statement pair that fails any
   qualification is simply not collected -- the callee body check then
   refuses it exactly as before the flag existed
   (crosscall-callee-vector-outside-loop), byte-identically.  A
   qualified pair additionally requires WRITER UNIQUENESS: no other
   sfpwriteconfig_v (any destination overlapping a pair's, or
   unresolvable) and no raw-access marker naming a pair register
   anywhere in the callee -- a second writer would make the once-only
   preheader programming diverge from the per-call original on
   iterations after the first.	*/

static void
discover_config_prefix (function *fn,
			const auto_vec<contract_entry> &contract,
			auto_vec<config_prefix_entry> *pairs)
{
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
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
	      || gimple_call_num_args (write) < 2
	      || gimple_call_arg (write, 0) != lhs
	      || gimple_call_lhs (write))
	    continue;
	  basic_block wbb = gimple_bb (write);
	  if (!wbb || wbb != bb)
	    continue;		/* pair stays block-local (v1 shape) */
	  tree dest = gimple_call_arg (write, 1);
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
	  config_prefix_entry p = { load, write, d };
	  pairs->safe_push (p);
	}
    }
  if (pairs->is_empty ())
    return;

  /* Writer uniqueness over the whole callee -- including among the
     pairs themselves (two pairs to one register: refuse rather than
     reason about ordering).  */
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
	    tree dest = gimple_call_arg (call, 1);
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

  /* Every return dominated by every pair (the per-call original
     executed on every path; the preheader re-materialization must
     replace an unconditional write).  */
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

/* Config-word audit over the recorded MOP template slots: a config
   contract additionally requires every audited slot word to be
   SFPCONFIG-free (a template-delivered config word could rewrite the
   programmed register at any MOP launch).  */

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

/* ------------------------------------------------------------------ */
/* Callee-wide checks beyond the loop scans.			      */

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
	  /* Vector statements outside the consumer loop must be
	     exactly the contract loads (the liveness-extension tail is
	     otherwise vector-free) -- plus the qualified config-prefix
	     pairs, which move with the contract.  */
	  bool is_contract_load = false;
	  for (const contract_entry &e : contract)
	    if (stmt == e.load)
	      is_contract_load = true;
	  for (const config_prefix_entry &p : config)
	    if (stmt == p.load || stmt == p.write)
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

  /* Keeping the contract live across the consumer loop must hold the
     eight-LREG file (the shared conservative pressure proof).  With a
     config pair riding the contract, the callee's reads of the
     programmed constant register are creg-file reads (LReg[8..14])
     which never occupy an allocatable LREG (the invariant pass's
     ratified exemption: every such operand position accepts the
     constant register class in place, reg_or_cstlreg_operand); a
     hypothetical non-capable use undercounts and is caught fail-closed
     by the named post-RA spill diagnosis, never as wrong code.
     Without a config pair the historical counting is byte-identical
     (exemption off).  */
  auto_vec<gcall *> loads;
  for (const contract_entry &e : contract)
    loads.safe_push (e.load);
  if (!rvtt_pressure_loop_legal_p (consumer_loop, loads,
				   /*report=*/false,
				   /*cc_transients=*/false,
				   /*exempt_creg_reads=*/
				   !config.is_empty ()))
    return crosscall_refuse ("crosscall-callee-pressure", fn->decl, nullptr);

  /* Every return must be dominated by every load (the exit write-back
     uses the load's SSA value).  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    for (const contract_entry &c : contract)
      if (!dominated_by_p (CDI_DOMINATORS, e->src, gimple_bb (c.load)))
	return crosscall_refuse ("crosscall-callee-shape-unproven", fn->decl,
				 c.load);

  return true;
}

/* ------------------------------------------------------------------ */
/* Caller-side proof for one cgraph caller.  Runs under the CALLER's
   cfun (push_cfun done by the caller of this function).	      */

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

  /* Placement residency walk (config-prefix knob): lift the contract's
     programming point across ENCLOSING loops whose bodies pass the
     same caller-epoch scan (the zero-trip clobber argument is
     loop-agnostic: nothing between the outer entry and the calls can
     write or observe the contract state, so entering the outer loop
     without reaching a call is as unobservable as entering the inner
     one).  A level that fails any proof simply stops the walk -- the
     inner placement stands, nothing refuses.  */
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
	bool saved_mop = ctx.saw_mop;	/* a rejected level's words must
					   not constrain the committed
					   placement's MOP audit */
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

  /* Explicit architectural READS of a contract register anywhere in
     the caller OUTSIDE the loop: the one contract-carrying observer of
     residual register state (no-residual-contents model) the zero-trip
     clobber argument cannot order against -- the hoisted loads execute
     on loop entry even when the body never runs, so a pre-loop
     hand-off read after the loop would observe the clobber (file
     header, [caller]).  In-loop markers were already refused by the
     scan above; writes cannot observe.  */
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

/* ------------------------------------------------------------------ */
/* Commit.							      */

/* Insert STMT at the tail of the preheader block PH (before a
   block-terminating statement if one ends the block -- the same
   insertion rule the prgm-const programming point uses).  */

void
insert_in_preheader (basic_block ph, gimple *stmt)
{
  gimple_stmt_iterator gsi = gsi_last_bb (ph);
  if (gsi_end_p (gsi) || !stmt_ends_bb_p (gsi_stmt (gsi)))
    gsi_insert_after (&gsi, stmt, GSI_NEW_STMT);
  else
    gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
}

/* Commit the caller side of a proven contract: rebuild each config
   pair (load + sfpwriteconfig_v) and then each contract load with
   its pinning sfpwritelreg in CALLER's loop preheader on ENTRY --
   config pairs first, so their L0 temporary's live range stays
   disjoint from the pinned contract ranges -- creating the cgraph
   edges the caller's later passes expect and updating virtual
   SSA.  */

static void
commit_caller (cgraph_node *caller, edge entry,
	       const auto_vec<contract_entry> &contract,
	       const auto_vec<config_prefix_entry> &config)
{
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);
  basic_block ph = rvtt_commit_hoist_preheader (entry);
  /* Config pairs first: the SFPCONFIG source operand is pinned (L0 by
     the md), and materializing the pair ahead of the contract loads
     keeps its temporary's live range disjoint from every pinned
     contract range (the callee's original prefix order).  */
  for (const config_prefix_entry &p : config)
    {
      unsigned nargs = gimple_call_num_args (p.load);
      auto_vec<tree, 8> args;
      for (unsigned i = 0; i != nargs; ++i)
	args.safe_push (unshare_expr (gimple_call_arg (p.load, i)));
      gcall *load = gimple_build_call_vec (gimple_call_fndecl (p.load), args);
      tree val = make_ssa_name (TREE_TYPE (gimple_call_lhs (p.load)));
      gimple_call_set_lhs (load, val);
      gcall *write = gimple_build_call
	(gimple_call_fndecl (p.write), 2, val,
	 build_int_cst (integer_type_node, (int) p.dest));
      insert_in_preheader (ph, load);
      insert_in_preheader (ph, write);
      caller->create_edge (cgraph_node::get_create
			     (gimple_call_fndecl (load)), load, ph->count);
      caller->create_edge (cgraph_node::get_create
			     (gimple_call_fndecl (p.write)), write,
			   ph->count);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: placed config pair (creg %u) in %s "
		   "preheader bb %d: ",
		   p.dest, caller->dump_name (), ph->index);
	  print_gimple_stmt (dump_file, load, 0, TDF_NONE);
	}
    }
  for (const contract_entry &e : contract)
    {
      /* Clone the materialization verbatim (same builtin, same
	 constant operands) and pin its value into the contract
	 register.  */
      unsigned nargs = gimple_call_num_args (e.load);
      auto_vec<tree, 8> args;
      for (unsigned i = 0; i != nargs; ++i)
	args.safe_push (unshare_expr (gimple_call_arg (e.load, i)));
      gcall *load = gimple_build_call_vec (gimple_call_fndecl (e.load), args);
      tree val = make_ssa_name (TREE_TYPE (e.value));
      gimple_call_set_lhs (load, val);
      /* No source location: the original's location (and its BLOCK
	 chain) belongs to the callee's lexical tree and must not leak
	 into another function.  */
      gcall *write = gimple_build_call
	(write_d->decl, 2, val,
	 build_int_cst (integer_type_node, e.lreg));
      insert_in_preheader (ph, load);
      insert_in_preheader (ph, write);
      /* The caller's own inline transform has not run yet (it runs at
	 the head of its late pipeline); every call statement it walks
	 must carry a cgraph edge.  */
      caller->create_edge (cgraph_node::get_create
			     (gimple_call_fndecl (load)), load, ph->count);
      caller->create_edge (cgraph_node::get_create (write_d->decl), write,
			   ph->count);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: placed contract materialization "
		   "(L%d) in %s preheader bb %d: ",
		   e.lreg, caller->dump_name (), ph->index);
	  print_gimple_stmt (dump_file, load, 0, TDF_NONE);
	}
    }
  update_ssa (TODO_update_ssa_only_virtuals);
}

/* Commit the callee side in FN: delete each config pair (the caller
   preheaders now program its register), replace each contract
   materialization with an sfpreadlreg of its pinned register, and
   write each contract value back to its register before every
   return, keeping the value live -- in that register -- across the
   whole body.  */

static void
commit_callee (function *fn, const auto_vec<contract_entry> &contract,
	       const auto_vec<config_prefix_entry> &config)
{
  const rvtt_insn_data *read_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);

  /* The config pairs move entirely: every caller preheader now
     programs the register once per loop entry; the callee's readers
     (explicit creg reads) observe the identical value on every call
     (the widened caller proofs).  */
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
      if (dump_file)
	{
	  fprintf (dump_file,
		   "crosscall-hoist: contract read (L%d) replaces prefix "
		   "materialization in %s: ",
		   e.lreg, IDENTIFIER_POINTER (DECL_NAME (fn->decl)));
	  print_gimple_stmt (dump_file, read, 0, TDF_NONE);
	}
    }

  /* Keep every contract value live -- in its register -- to every
     return, so no later decision in this function can leave a call
     with the register clobbered.  */
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

/* ------------------------------------------------------------------ */
/* Driver.							      */

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

  /* Config-prefix pairs (flag-gated widening): with the flag
     off, discovery never runs and every proof and refusal below is
     byte-identical to the pre-flag pass.  */
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

  /* The caller closure: a definition, no aliases/thunks/clones, not
     address-taken, at least one caller, no recursion.	*/
  if (!cn->definition || cn->address_taken || cn->alias || cn->thunk
      || cn->clones || !cn->callers)
    return crosscall_refuse ("crosscall-caller-body-unavailable", fn->decl,
			     nullptr);

  /* One call site per caller (v1); collect and prove each caller.  */
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
      /* A caller outside the TU executable closure: the census never
	 vouched for its stores, so no proof over it can consult the
	 template audit consistently (the wave-8 internal-inconsistency
	 shape: proving a caller epoch the census skipped).  */
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

  /* Prove every caller, then commit every side.  The caller's loop
     state is set up per caller and kept only long enough to prove and
     (on a complete proof of ALL callers) commit; between the two
     passes over a caller nothing changes its body, so re-running the
     structural lookups at commit time is sound and keeps refusals
     mutation-free.  */
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

  /* Commit: callers first (their loop state must be recomputed inside
     their own cfun), then the callee.	*/
  for (caller_plan &p : plans)
    {
      push_cfun (DECL_STRUCT_FUNCTION (p.node->decl));
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
      /* Recompute the entry edge in this context (structure is
	 unchanged since the proof above), walking outward the same
	 number of proven residency levels.  */
      class loop *loop = gimple_bb (p.call_stmt)->loop_father;
      for (unsigned l = 0; l != p.lift_levels; ++l)
	loop = loop_outer (loop);
      edge entry = rvtt_loop_entry_edge (loop);
      gcc_assert (entry);
      commit_caller (p.node, entry, contract, config);
      /* Item #15: the caller's body just mutated from outside its own
	 pipeline -- any cached summary of it is void.  */
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
    /* TU facts first, while every body is still gimple (the
       prgm-const timing argument).  */
    compute_tu_facts ();
    /* Surface the cross-call CC carry fact (rvtt-cc-region fold,
       cached in the IPA summary).  Dump-gated and verdict-inert by
       contract -- no consumer admission here widens on it; the
       general-region widening consumes it under its own flag and
       names.  */
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
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

/* Pass factory for rvtt_crosscall, referenced from
   rvtt-passes.def.  */

gimple_opt_pass *
make_pass_rvtt_crosscall (gcc::context *ctxt)
{
  return new pass_rvtt_crosscall (ctxt);
}
