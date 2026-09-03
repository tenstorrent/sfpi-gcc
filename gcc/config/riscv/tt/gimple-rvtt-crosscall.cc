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

static bool
refuse (const char *reason, tree fn, gimple *stmt)
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

/* The canonical instruction-buffer operand of the loadi builtins
   (mirrors gimple-rvtt-prgm-const.cc's qualification; the
   "__instrn_buffer" name is the recorded ABI anchor).  */

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
      || !canonical_buffer_arg_p (gimple_call_arg (call, 0)))
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
/* Audited 32-bit word classification (LREG face).

   The classifier now lives in THE unified audited word-fact table
   (rvtt-raw-boundary.cc rvtt_word_facts_classify);
   rvtt_word_lreg_class is this face's query accessor
   -- same question (can this word write an ALLOCATABLE hard LREG in
   the contract set?), same verdicts, same refusal names, refusing
   default for every class not on record.  The verdict struct keeps
   its local spelling.  */

typedef rvtt_wf_lreg_verdict word_verdict;

/* Constant-base extraction for a composed pushed word, per AXIOM
   tt-op-field-discipline (rtl-rvtt-mop-form.cc file header: runtime
   operands of a TT_OP composition stay inside their bit fields, the
   discipline the TT_OP macro family itself encodes).  Returns the
   constant base word through *BASE, or false when no constant base
   pins the opcode byte.  Mirrors rtl-rvtt-mop-form.cc's
   mop_pushed_word_base, additionally reporting the full base so field
   checks below the opcode can be applied where the class needs them.  */

bool
pushed_word_base (tree val, uint32_t *base, unsigned depth)
{
  if (depth > 12 || !val)
    return false;
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	return false;
      *base = (uint32_t) (TREE_INT_CST_LOW (val) & 0xffffffff);
      return true;
    }
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !is_gimple_assign (def))
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  if (code == PLUS_EXPR || code == BIT_IOR_EXPR)
    {
      uint32_t a = 0, b = 0;
      bool has_a = pushed_word_base (gimple_assign_rhs1 (def), &a, depth + 1);
      bool has_b = pushed_word_base (gimple_assign_rhs2 (def), &b, depth + 1);
      if (has_a && has_b)
	{
	  *base = code == PLUS_EXPR ? a + b : (a | b);
	  return true;
	}
      /* Exactly one side carries the constant base; the other is an
	 in-field runtime operand (the axiom).  A side with an opcode
	 byte of zero contributes no opcode either way.  */
      if (has_a && (a >> 24) != 0)
	{
	  *base = a;
	  return true;
	}
      if (has_b && (b >> 24) != 0)
	{
	  *base = b;
	  return true;
	}
      if (has_a || has_b)
	{
	  /* Constant base without an opcode byte: the composition's
	     opcode, if any, is on the unresolved side -- unpinned.  */
	  return false;
	}
      return false;
    }
  if (CONVERT_EXPR_CODE_P (code) || code == NOP_EXPR || code == SSA_NAME)
    return pushed_word_base (gimple_assign_rhs1 (def), base, depth + 1);
  /* Shifted/masked single fields cannot construct an opcode byte under
     the discipline axiom -- they contribute a zero base.  */
  if (code == LSHIFT_EXPR || code == BIT_AND_EXPR || code == RSHIFT_EXPR)
    {
      *base = 0;
      return true;
    }
  return false;
}

/* Exact 32-bit resolution of a fully-constant composition (SSA chase
   over constant arithmetic only; any unresolved leaf fails).  The
   census uses this for template-slot words, which are compile-time
   TT_OP compositions not yet folded at the post-IPA form this pass
   reads: the production ckernel_template::program() shape stores the
   constant words into a local template object and reloads them at the
   slot stores, so a memory load resolves through a bounded
   virtual-operand walk to its dominating same-lvalue constant store
   (refusing on any statement that may clobber the lvalue and is not
   that store; the audited blocking-store asm idiom at a proven MMIO
   address bypasses, by the link-image disjointness fact).  */

static bool resolve_exact_word (tree val, uint32_t *word, unsigned depth);
bool blocking_store_asm_p (const gasm *stmt, tree *value, tree *addr);

/* Resolve into *WORD the 32-bit value LOAD reads from REF: walk the
   virtual-operand chain backward to the dominating store of the same
   lvalue and resolve its stored value (resolve_exact_word, bounded
   by DEPTH).  Any statement that may clobber REF fails closed; the
   audited blocking-store asm at a proven constant MMIO address is
   stepped over (link-image disjointness).  */

static bool
resolve_field_load (gimple *load, tree ref, uint32_t *word, unsigned depth)
{
  if (depth > 12)
    return false;
  ao_ref r;
  ao_ref_init (&r, ref);
  tree vuse = gimple_vuse (load);
  for (unsigned steps = 0; vuse && steps < 128; ++steps)
    {
      gimple *def = SSA_NAME_DEF_STMT (vuse);
      if (!def || gimple_nop_p (def) || gimple_code (def) == GIMPLE_PHI)
	return false;
      if (gasm *a = dyn_cast <gasm *> (def))
	{
	  /* The blocking-store idiom at a resolved non-link-image MMIO
	     address cannot alias a link-image object
	     (XTT_LINK_IMAGE_DISJOINT); everything else fails.  */
	  tree value, ptr;
	  unsigned HOST_WIDE_INT addr;
	  if (blocking_store_asm_p (a, &value, &ptr)
	      && pointer_constant_address (ptr, &addr))
	    {
	      vuse = gimple_vuse (def);
	      continue;
	    }
	  return false;
	}
      if (is_gimple_assign (def) && gimple_store_p (def)
	  && !gimple_clobber_p (def)
	  && operand_equal_p (gimple_get_lhs (def), ref, 0))
	return resolve_exact_word (gimple_assign_rhs1 (def), word, depth + 1);
      if (!stmt_may_clobber_ref_p_1 (def, &r))
	{
	  vuse = gimple_vuse (def);
	  continue;
	}
      return false;
    }
  return false;
}

/* Exact 32-bit resolution of VAL into *WORD: constants, conversions,
   constant arithmetic (a DEPTH-bounded SSA chase), and memory loads
   resolvable to a dominating constant store of the same lvalue
   (resolve_field_load).  Any unresolved leaf fails; the contract
   comment above the forward declarations gives the census use.  */

static bool
resolve_exact_word (tree val, uint32_t *word, unsigned depth = 0)
{
  if (depth > 12 || !val)
    return false;
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	return false;
      *word = (uint32_t) (TREE_INT_CST_LOW (val) & 0xffffffff);
      return true;
    }
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !is_gimple_assign (def))
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  uint32_t a, b;
  switch (code)
    {
    CASE_CONVERT:
      return resolve_exact_word (gimple_assign_rhs1 (def), word, depth + 1);
    case SSA_NAME:
    case INTEGER_CST:
      return resolve_exact_word (gimple_assign_rhs1 (def), word, depth + 1);
    case PLUS_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
    case MULT_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      if (!resolve_exact_word (gimple_assign_rhs1 (def), &a, depth + 1)
	  || !resolve_exact_word (gimple_assign_rhs2 (def), &b, depth + 1))
	return false;
      switch (code)
	{
	case PLUS_EXPR: *word = a + b; return true;
	case BIT_IOR_EXPR: *word = a | b; return true;
	case BIT_XOR_EXPR: *word = a ^ b; return true;
	case BIT_AND_EXPR: *word = a & b; return true;
	case MULT_EXPR: *word = a * b; return true;
	case LSHIFT_EXPR: *word = b < 32 ? a << b : 0; return true;
	case RSHIFT_EXPR: *word = b < 32 ? a >> b : 0; return true;
	default: return false;
	}
    default:
      if (gimple_assign_load_p (def))
	return resolve_field_load (def, gimple_assign_rhs1 (def), word,
				   depth);
      return false;
    }
}

/* Classify a composed (possibly runtime-completed) delivered word.
   A constant-resolved SFPLOADI word checks its literal destination
   field; a base-only SFPLOADI (runtime completion) cannot pin the
   destination and refuses.  */

static word_verdict
classify_delivered_value (tree val, unsigned contract_mask,
			  bool region_strict = false,
			  bool config_strict = false, unsigned phi_depth = 0)
{
  word_verdict v = { false, false, false, "crosscall-caller-word-unproven" };
  if (TREE_CODE (val) == INTEGER_CST)
    return rvtt_word_lreg_class ((uint32_t) (TREE_INT_CST_LOW (val)
					   & 0xffffffff), contract_mask,
			       region_strict, config_strict);
  uint32_t base;
  if (!pushed_word_base (val, &base))
    {
      /* Region discipline only: a PHI-joined delivered word (one push
	 site fed by branch-selected compositions) is inert exactly
	 when EVERY argument's composition is audited inert; MOP and
	 REPLAY classifications aggregate.  Bounded, refusing
	 default.  */
      if (region_strict && phi_depth < 2 && TREE_CODE (val) == SSA_NAME)
	if (gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (val)))
	  {
	    word_verdict agg = { true, false, false, nullptr };
	    for (unsigned ix = 0; ix != gimple_phi_num_args (phi); ++ix)
	      {
		word_verdict a = classify_delivered_value
		  (gimple_phi_arg_def (phi, ix), contract_mask,
		   region_strict, config_strict, phi_depth + 1);
		if (!a.ok)
		  return a;
		agg.is_mop |= a.is_mop;
	      }
	    return agg;
	  }
      return v;
    }
  unsigned opcode = base >> 24;
  if (opcode == 0x71)
    /* Runtime-completed SFPLOADI: the destination field is not pinned
       by the base under the field axiom alone.  */
    return v;
  return rvtt_word_lreg_class (base, contract_mask, region_strict,
			     config_strict);
}

/* ------------------------------------------------------------------ */
/* Constant address resolution (stores).  Mirrors the pointer folding
   of rtl-rvtt-mop-form.cc (constant int-to-pointer chases) plus a
   refusing-default load-of-foldable-global step for the LLK aperture
   globals (TU-defined, never address-taken, constant initializer,
   never stored differently -- the census below verifies the last
   condition over the same whole-TU walk).  */


crosscall_tu_facts tu_facts;

/* Context for the TU census: the function being scanned (the walk does
   not switch cfun).  */
static const char *census_fname;
static tree census_fndecl;

/* A global whose loads may fold to its initializer: TU-defined scalar,
   never address-taken, with a constant-integer initializer.  */

static bool
foldable_global_p (tree decl, unsigned HOST_WIDE_INT *value)
{
  if (!VAR_P (decl) || DECL_EXTERNAL (decl) || TREE_ADDRESSABLE (decl)
      || !DECL_INITIAL (decl)
      /* Only the aperture pointer globals (pc_buf_base, regfile, ...):
	 scalar state globals (counters, indices) are ordinary mutable
	 data the assume+verify discipline is not for.  */
      || !POINTER_TYPE_P (TREE_TYPE (decl)))
    return false;
  tree init = DECL_INITIAL (decl);
  STRIP_NOPS (init);
  if (TREE_CODE (init) == INTEGER_CST && tree_fits_uhwi_p (init))
    {
      *value = tree_to_uhwi (init) & 0xffffffff;
      return true;
    }
  /* Pointer initializers of the reinterpret_cast<...>(CONSTANT) shape
     fold through the conversion.  */
  if (CONVERT_EXPR_P (init)
      && TREE_CODE (TREE_OPERAND (init, 0)) == INTEGER_CST
      && tree_fits_uhwi_p (TREE_OPERAND (init, 0)))
    {
      *value = tree_to_uhwi (TREE_OPERAND (init, 0)) & 0xffffffff;
      return true;
    }
  return false;
}

/* Fold PTR to a constant byte address in *ADDR: integer constants,
   conversions, constant-offset pointer arithmetic, and loads of a
   foldable aperture global (the initializer is assumed and the
   assumption recorded for the census's verify step).  DEPTH bounds
   the SSA chase; anything else fails closed.  */

bool
pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
			  unsigned depth)
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
  tree rhs1 = gimple_assign_rhs1 (def);
  if (CONVERT_EXPR_CODE_P (code) || code == INTEGER_CST || code == SSA_NAME)
    return pointer_constant_address (rhs1, addr, depth + 1);
  if (code == POINTER_PLUS_EXPR || code == PLUS_EXPR)
    {
      tree off = gimple_assign_rhs2 (def);
      unsigned HOST_WIDE_INT base;
      if (TREE_CODE (off) != INTEGER_CST || !tree_fits_shwi_p (off)
	  || !pointer_constant_address (rhs1, &base, depth + 1))
	return false;
      *addr = (base + (unsigned HOST_WIDE_INT) tree_to_shwi (off))
	      & 0xffffffff;
      return true;
    }
  /* A load of a foldable aperture global: assume the initializer and
     record the assumption; the census verifies no conflicting store
     exists (assume + verify, the derivation's own discipline).  */
  if (gimple_assign_load_p (def) && DECL_P (rhs1))
    {
      unsigned HOST_WIDE_INT value;
      if (foldable_global_p (rhs1, &value))
	{
	  /* An unrooted census recorded no store facts at all: the
	     assume+verify discipline has nothing to verify against,
	     so the fold fails closed.  */
	  if (tu_facts.census_unrooted)
	    return false;
	  if (tu_facts.globals)
	    {
	      global_census_entry &ge
		= tu_facts.globals->get_or_insert (rhs1);
	      /* A censused conflicting store makes the initializer
		 fold unsound at any later assumption point (the
		 end-of-census verification only covers assumptions
		 recorded during the walk itself).  */
	      if (ge.stored_unknown)
		return false;
	      ge.assumed = true;
	    }
	  *addr = value;
	  return true;
	}
    }
  return false;
}

/* Fold REF (a store lhs) to a constant byte address if possible
   (mirrors rtl-rvtt-mop-form.cc mop_ref_constant_address).  */

bool
ref_constant_address (tree ref, unsigned HOST_WIDE_INT *addr)
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
  if (!pointer_constant_address (TREE_OPERAND (base, 0), &a))
    return false;
  a += (unsigned HOST_WIDE_INT) tree_to_shwi (moff);
  a += (unsigned HOST_WIDE_INT) (pos / BITS_PER_UNIT);
  *addr = a & 0xffffffff;
  return true;
}

/* ------------------------------------------------------------------ */
/* TU-wide MOP template-file audit (the LREG face of the derivation
   rvtt-mop-derive.cc performs for PRGM state).  Computed EAGERLY at
   the first execution of this pass -- the moment every other function
   body in the TU is still gimple (the same timing argument
   gimple-rvtt-prgm-const.cc documents).  */

static void
census_slot_refusal (const char *why)
{
  if (!tu_facts.slots_unproven)
    {
      tu_facts.slots_unproven = true;
      tu_facts.slot_reason = why;
    }
  /* Recorded for EVERY refusal, not only the first: the init-hoist
     value-equality guard must know whether anything beyond
     body-unavailability fired.  */
  if (strcmp (why, "mop-template-body-unavailable") != 0)
    tu_facts.slot_refusal_non_body = true;
}

/* Audit one resolved instruction-slot word (slots 2..8).  */

static void
audit_slot_word (uint32_t word)
{
  tu_facts.slot_words.safe_push (word);
  unsigned opcode = word >> 24;
  if (opcode == 0x71)
    {
      tu_facts.slot_loadi_dests |= 1u << ((word >> 20) & 0xf);
      return;
    }
  if (opcode == XTT_REPLAY_OPCODE)
    {
      tu_facts.slot_replay = true;
      return;
    }
  word_verdict v = rvtt_word_lreg_class (word, 0);
  if (!v.ok || v.is_mop /* nested MOP in a slot: no recorded fact */)
    census_slot_refusal ("mop-template-slot-word-unproven");
}

/* Whether a store LHS can be dismissed while walking a virtual-operand
   chain for the addressable object BASE_DECL/(OFFSET,32 bits):
   returns 1 for the exact 32-bit field itself, 0 for a store provably
   elsewhere, -1 for a possible clobber.  A resolved constant target
   address is provably elsewhere: the Tensix MMIO delivery ranges are
   disjoint from every link-image object (XTT_LINK_IMAGE_DISJOINT,
   rvtt-mop-tables.h).  */

static int
classify_walk_store (tree lhs, tree base_decl, HOST_WIDE_INT offset)
{
  poly_int64 psize, poffset, pmax;
  bool reverse;
  tree base = get_ref_base_and_extent (lhs, &poffset, &psize, &pmax,
				       &reverse);
  if (base && DECL_P (base))
    {
      if (base != base_decl)
	return 0;		/* a different object */
      HOST_WIDE_INT off, size, max;
      if (!poffset.is_constant (&off) || !psize.is_constant (&size)
	  || !pmax.is_constant (&max) || size != max)
	return -1;
      if (off == offset && size == 32)
	return 1;
      if (off + size <= offset || offset + 32 <= off)
	return 0;		/* disjoint field */
      return -1;
    }
  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    return 0;			/* MMIO: disjoint from link-image data */
  return -1;
}

/* Walk the virtual-operand chain backward from FROM to find the value
   stored into (BASE_DECL, OFFSET) -- the call-site resolution of a
   demanded template field.  Fails closed on anything that may clobber
   the object and is not the store itself; the audited blocking-store
   asm at a resolved constant MMIO address bypasses.  */

static bool
resolve_object_field_at (gimple *from, tree base_decl, HOST_WIDE_INT offset,
			 uint32_t *word)
{
  tree vuse = gimple_vuse (from);
  for (unsigned steps = 0; vuse && steps < 256; ++steps)
    {
      gimple *def = SSA_NAME_DEF_STMT (vuse);
      if (!def || gimple_nop_p (def) || gimple_code (def) == GIMPLE_PHI)
	return false;
      if (gasm *a = dyn_cast <gasm *> (def))
	{
	  tree value, ptr;
	  unsigned HOST_WIDE_INT addr;
	  if (blocking_store_asm_p (a, &value, &ptr)
	      && pointer_constant_address (ptr, &addr))
	    {
	      vuse = gimple_vuse (def);
	      continue;
	    }
	  return false;
	}
      if (is_gimple_assign (def) && gimple_store_p (def))
	{
	  int k = gimple_clobber_p (def) ? -1
	    : classify_walk_store (gimple_get_lhs (def), base_decl, offset);
	  if (k == 1)
	    return resolve_exact_word (gimple_assign_rhs1 (def), word, 0);
	  if (k == 0)
	    {
	      vuse = gimple_vuse (def);
	      continue;
	    }
	  return false;
	}
      if (gimple_vdef (def))
	return false;		/* call or unknown memory effect */
      vuse = gimple_vuse (def);
    }
  return false;
}

/* VALUE (a template-slot word in the current census function) is a
   load of a parameter-relative 32-bit field, undisturbed since
   function entry.  Fill *D for call-site resolution.  */

static bool
param_field_demand (tree value, slot_demand *d)
{
  if (TREE_CODE (value) != SSA_NAME)
    return false;
  gimple *load = SSA_NAME_DEF_STMT (value);
  if (!load || !is_gimple_assign (load) || !gimple_assign_load_p (load))
    return false;
  tree ref = gimple_assign_rhs1 (load);
  poly_int64 psize, poffset, pmax;
  bool reverse;
  tree base = get_ref_base_and_extent (ref, &poffset, &psize, &pmax,
				       &reverse);
  if (!base || TREE_CODE (base) != MEM_REF)
    return false;
  tree ptr = TREE_OPERAND (base, 0);
  if (TREE_CODE (ptr) != SSA_NAME || !SSA_NAME_IS_DEFAULT_DEF (ptr)
      || TREE_CODE (SSA_NAME_VAR (ptr)) != PARM_DECL)
    return false;
  HOST_WIDE_INT off, size, max;
  if (!poffset.is_constant (&off) || !psize.is_constant (&size)
      || !pmax.is_constant (&max) || size != 32 || max != 32)
    return false;
  /* Locate the parameter index in the censused function.  */
  tree fndecl = census_fndecl;
  if (!fndecl)
    return false;
  int index = -1, i = 0;
  for (tree p = DECL_ARGUMENTS (fndecl); p; p = DECL_CHAIN (p), ++i)
    if (p == SSA_NAME_VAR (ptr))
      {
	index = i;
	break;
      }
  if (index < 0)
    return false;
  /* The field must be undisturbed since function entry: walk the
     virtual chain from the load to the entry's default definition,
     dismissing only provably-elsewhere stores.  */
  tree vuse = gimple_vuse (load);
  for (unsigned steps = 0; vuse && steps < 256; ++steps)
    {
      gimple *def = SSA_NAME_DEF_STMT (vuse);
      if (!def || gimple_nop_p (def))
	break;			/* function entry reached: undisturbed */
      if (gimple_code (def) == GIMPLE_PHI)
	return false;
      if (gasm *a = dyn_cast <gasm *> (def))
	{
	  tree value2, ptr2;
	  unsigned HOST_WIDE_INT addr;
	  if (blocking_store_asm_p (a, &value2, &ptr2)
	      && pointer_constant_address (ptr2, &addr))
	    {
	      vuse = gimple_vuse (def);
	      continue;
	    }
	  return false;
	}
      if (is_gimple_assign (def) && gimple_store_p (def))
	{
	  /* Any store through the same parameter pointer to an
	     overlapping range disturbs; a resolved constant MMIO
	     target or a distinct local object does not.  */
	  tree lhs = gimple_get_lhs (def);
	  poly_int64 s2, o2, m2;
	  bool rev2;
	  tree b2 = get_ref_base_and_extent (lhs, &o2, &s2, &m2, &rev2);
	  unsigned HOST_WIDE_INT addr;
	  bool elsewhere = false;
	  if (b2 && DECL_P (b2))
	    elsewhere = true;	/* a named local/global, not *parm */
	  else if (ref_constant_address (lhs, &addr))
	    elsewhere = true;	/* MMIO: link-image disjoint */
	  else if (b2 && TREE_CODE (b2) == MEM_REF
		   && TREE_OPERAND (b2, 0) == ptr)
	    {
	      HOST_WIDE_INT ob, sb, mb;
	      if (o2.is_constant (&ob) && s2.is_constant (&sb)
		  && m2.is_constant (&mb) && sb == mb
		  && (ob + sb <= off || off + 32 <= ob))
		elsewhere = true;
	    }
	  if (!elsewhere)
	    return false;
	  vuse = gimple_vuse (def);
	  continue;
	}
      if (gimple_vdef (def))
	return false;
      vuse = gimple_vuse (def);
    }
  d->fndecl = fndecl;
  d->parm_index = index;
  d->offset = off;
  return true;
}

/* Record one store to constant address ADDR of VALUE.  */

static void
census_constant_store (unsigned HOST_WIDE_INT addr, tree value,
		       gimple *stmt)
{
  if (addr < XTT_MOP_CFG_MMIO_BASE || addr > XTT_MOP_CFG_MMIO_LIMIT)
    return;
  if (addr >= XTT_MOP_CFG_MMIO_BASE + 4 * XTT_MOP_CFG_SLOTS
      || (addr - XTT_MOP_CFG_MMIO_BASE) % 4 != 0)
    {
      census_slot_refusal ("mop-template-slot-range-unproven");
      return;
    }
  unsigned slot = (addr - XTT_MOP_CFG_MMIO_BASE) / 4;
  if (slot <= 1)
    /* Loop lengths / flags: never expanded as instruction words
       (rvtt-mop-tables.h union taxonomy, the same slot rule
       rvtt-mop-derive.cc applies).  */
    return;
  uint32_t word;
  if (resolve_exact_word (value, &word))
    {
      audit_slot_word (word);
      return;
    }
  /* The out-of-line template-programming shape: the word is a
     parameter-relative field load, undisturbed since function entry;
     defer to call-site resolution.  */
  slot_demand d;
  if (param_field_demand (value, &d))
    {
      tu_facts.demands.safe_push (d);
      return;
    }
  census_slot_refusal ("mop-template-slot-word-unresolved");
  if (dump_file && stmt)
    {
      fprintf (dump_file, "crosscall-hoist: unresolved slot word in %s: ",
	       census_fname ? census_fname : "?");
      print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
    }
}

/* Whether the store target described by PTR provably lies outside the
   MOP template file.  Mirrors the derivation's address classification
   (rvtt-mop-derive.cc classify_pointer_base) restricted to the one
   question this census asks: link-image objects (TU-defined decls,
   XTT_LINK_IMAGE_DISJOINT), the recorded crt0 data anchors, the
   instruction-FIFO anchor (a push, not a template write), constant
   addresses outside the template range, inductions over such bases,
   and pointer parameters whose every reachable call site passes a safe
   base.  Everything else is unknown (fail closed).  */

static bool ptr_not_template_p (tree ptr, hash_set<tree> &visiting,
				hash_set<cgraph_node *> *executable,
				unsigned depth = 0,
				hash_set<tree> *parm_visiting = nullptr);

/* Whether the store target BASE, a declared object, provably lies
   outside the MOP template file: any TU-defined data object
   (link-image disjointness), the instruction-FIFO aperture, or a
   recorded crt0 data anchor.  Unknown externals fail closed.  */

static bool
decl_not_template_p (tree base)
{
  if (!VAR_P (base))
    return false;
  if (!DECL_EXTERNAL (base))
    return true;		/* TU data object */
  const char *name = DECL_ASSEMBLER_NAME (base)
    ? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (base))
    : (DECL_NAME (base) ? IDENTIFIER_POINTER (DECL_NAME (base)) : nullptr);
  if (!name)
    return false;
  if (!strcmp (name, "__instrn_buffer"))
    return true;		/* FIFO aperture: not the template file */
  static const char *const anchors[] = {
    "__ldm_bss_start", "__ldm_bss_end", "__ldm_data_start",
    "__ldm_data_end", "__loader_init_start", "__loader_init_end",
    "__init_array_start", "__init_array_end", "__stack_top",
    "__global_pointer$", "__l1_data_start", "__l1_data_end",
    "__firmware_start",
  };
  for (const char *a : anchors)
    if (!strcmp (name, a))
      return true;		/* recorded crt0 data anchors
				   (rvtt-mop-derive.cc census) */
  return false;
}

/* Whether the pointer parameter PARM of a censused function provably
   never carries the MOP template file: the function's caller set
   must be enumerable (a definition, not address-taken or aliased,
   not an entry root), and every executable call site must pass an
   argument that is itself proven not-template.  DEPTH bounds the
   recursion; PARM_VISITING breaks parameter cycles.  */

static bool
param_not_template_p (tree parm, hash_set<cgraph_node *> *executable,
		      unsigned depth, hash_set<tree> *parm_visiting)
{
  hash_set<tree> local;
  hash_set<tree> &pv = parm_visiting ? *parm_visiting : local;
  if (depth > 4 || pv.add (parm))
    return false;
  tree fndecl = DECL_CONTEXT (parm);
  if (!fndecl || TREE_CODE (fndecl) != FUNCTION_DECL)
    return false;
  cgraph_node *cn = cgraph_node::get (fndecl);
  if (!cn || !cn->definition || cn->address_taken || cn->alias
      || cn->thunk || cn->clones || !cn->callers)
    return false;
  /* A closure root is enterable from OUTSIDE the TU (crt0/firmware):
     its in-TU call edges are not all the calls that can execute, so a
     parameter binding cannot be proven from them.  Fail closed (the
     same direction the address-taken check above takes).  */
  if (tu_facts.entry_roots && tu_facts.entry_roots->contains (cn))
    return false;
  int idx = -1, i = 0;
  for (tree p = DECL_ARGUMENTS (fndecl); p; p = DECL_CHAIN (p), ++i)
    if (p == parm)
      {
	idx = i;
	break;
      }
  if (idx < 0)
    return false;
  for (cgraph_edge *e = cn->callers; e; e = e->next_caller)
    {
      if (!executable->contains (e->caller))
	continue;
      gcall *call = e->call_stmt;
      if (!call || (unsigned) idx >= gimple_call_num_args (call))
	return false;
      hash_set<tree> vis;
      if (!ptr_not_template_p (gimple_call_arg (call, idx), vis, executable,
			       depth + 1, &pv))
	return false;
    }
  return true;
}

/* The recursive worker behind the contract described above the
   forward declaration: classify pointer PTR as provably outside the
   MOP template range.  VISITING breaks SSA cycles (a cycle member
   defers to the other arms), EXECUTABLE scopes the parameter-binding
   walk, DEPTH bounds recursion, PARM_VISITING breaks parameter
   cycles.  Fail closed.  */

static bool
ptr_not_template_p (tree ptr, hash_set<tree> &visiting,
		    hash_set<cgraph_node *> *executable, unsigned depth,
		    hash_set<tree> *parm_visiting)
{
  if (!ptr || depth > 16)
    return false;
  STRIP_NOPS (ptr);
  unsigned HOST_WIDE_INT addr;
  if (TREE_CODE (ptr) == INTEGER_CST)
    return pointer_constant_address (ptr, &addr)
      && (addr < XTT_MOP_CFG_MMIO_BASE || addr > XTT_MOP_CFG_MMIO_LIMIT);
  if (TREE_CODE (ptr) == ADDR_EXPR)
    {
      tree base = get_base_address (TREE_OPERAND (ptr, 0));
      return base && DECL_P (base) && decl_not_template_p (base);
    }
  if (TREE_CODE (ptr) != SSA_NAME)
    return false;
  /* The whole-pointer constant fold first: it chases the foldable
     aperture globals (regfile & co) and constant arithmetic.  */
  if (pointer_constant_address (ptr, &addr))
    return addr < XTT_MOP_CFG_MMIO_BASE || addr > XTT_MOP_CFG_MMIO_LIMIT;
  if (SSA_NAME_IS_DEFAULT_DEF (ptr) && SSA_NAME_VAR (ptr)
      && TREE_CODE (SSA_NAME_VAR (ptr)) == PARM_DECL)
    return param_not_template_p (SSA_NAME_VAR (ptr), executable, depth,
				 parm_visiting);
  if (visiting.add (ptr))
    return true;		/* cycle member: the other arms decide */
  bool res = false;
  gimple *def = SSA_NAME_DEF_STMT (ptr);
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      res = true;
      bool any = false;
      for (unsigned i = 0; i != gimple_phi_num_args (phi); ++i)
	{
	  tree arg = gimple_phi_arg_def (phi, i);
	  if (arg == ptr)
	    continue;
	  any = true;
	  if (!ptr_not_template_p (arg, visiting, executable, depth + 1,
				   parm_visiting))
	    res = false;
	}
      res &= any;
    }
  else if (is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	  || code == ADDR_EXPR)
	res = ptr_not_template_p (gimple_assign_rhs1 (def), visiting,
				  executable, depth + 1, parm_visiting);
      else if (code == POINTER_PLUS_EXPR || code == PLUS_EXPR)
	{
	  /* A constant whole-address fold first (an offset can move a
	     constant base anywhere); otherwise an in-object offset from
	     a data-class base stays in the object.  */
	  if (pointer_constant_address (ptr, &addr))
	    res = addr < XTT_MOP_CFG_MMIO_BASE
	      || addr > XTT_MOP_CFG_MMIO_LIMIT;
	  else
	    res = ptr_not_template_p (gimple_assign_rhs1 (def), visiting,
				      executable, depth + 1, parm_visiting)
	      && !pointer_constant_address (gimple_assign_rhs1 (def), &addr);
	}
    }
  visiting.remove (ptr);
  return res;
}

/* One store statement of the TU walk.  */

static void
census_store (gimple *stmt, hash_set<cgraph_node *> *executable)
{
  if (!is_gimple_assign (stmt) || !gimple_store_p (stmt))
    return;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return;

  /* Direct stores to a censused foldable global: any store makes the
     assumed fold unsound (refusing default; the production aperture
     globals are never stored).  */
  if (DECL_P (lhs) && tu_facts.globals)
    {
      unsigned HOST_WIDE_INT init_value;
      if (foldable_global_p (lhs, &init_value))
	{
	  /* A store of exactly the initializer value keeps the fold
	     (the derivation's own census rule); anything else voids
	     it.  */
	  unsigned HOST_WIDE_INT stored;
	  tree rhs = gimple_assign_rhs1 (stmt);
	  bool same = false;
	  if (TREE_CODE (rhs) == INTEGER_CST && tree_fits_uhwi_p (rhs))
	    same = (tree_to_uhwi (rhs) & 0xffffffff) == init_value;
	  else if (pointer_constant_address (rhs, &stored))
	    same = stored == init_value;
	  if (!same)
	    {
	      tu_facts.globals->get_or_insert (lhs).stored_unknown = true;
	      if (dump_file)
		{
		  fprintf (dump_file,
			   "crosscall-hoist: foldable global stored in %s: ",
			   census_fname ? census_fname : "?");
		  print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
		}
	    }
	}
    }

  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    {
      census_constant_store (addr, gimple_assign_rhs1 (stmt), stmt);
      return;
    }
  /* Non-constant address.  A store into a known non-volatile object is
     memory, not MMIO (hardware registers are declared volatile; the
     rule the mop-form caller census records).  */
  tree base = get_base_address (lhs);
  if (!TREE_THIS_VOLATILE (lhs)
      && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
    return;
  if (base && DECL_P (base) && decl_not_template_p (base))
    return;
  if (base && TREE_CODE (base) == MEM_REF)
    {
      hash_set<tree> visiting;
      if (ptr_not_template_p (TREE_OPERAND (base, 0), visiting, executable))
	return;
    }
  census_slot_refusal ("mop-template-alias-unproven");
  if (dump_file)
    {
      fprintf (dump_file, "crosscall-hoist: unresolved volatile store in %s: ",
	       census_fname ? census_fname : "?");
      print_gimple_stmt (dump_file, stmt, 0, TDF_NONE);
    }
}

/* The blocking-store scalar asm idiom stores %0 at (%1); resolve the
   address like any other store.  Recognition is the derivation's own
   exported predicate (rvtt-mop-derive.h); operand recovery mirrors its
   canonical shape: one tied output (%0, the stored value arrives as
   the tied "0" input) and the pointer input (%1).  Other asm shapes
   are irrelevant here: a `.ttinsn' word is delivered where it
   executes, and the audited scalar templates store nothing.  */

bool
blocking_store_asm_p (const gasm *stmt, tree *value, tree *addr)
{
  if (!rvtt_mop_blocking_store_asm_p (stmt))
    return false;
  unsigned nout = gimple_asm_noutputs (stmt);
  unsigned nin = gimple_asm_ninputs (stmt);
  tree val = NULL_TREE, ptr = NULL_TREE;
  /* %0: output with a matching-digit input.  */
  for (unsigned j = 0; j != nin && !val; ++j)
    {
      tree in = gimple_asm_input_op (stmt, j);
      tree cst = TREE_VALUE (TREE_PURPOSE (in));
      if (cst && TREE_CODE (cst) == STRING_CST
	  && ISDIGIT (TREE_STRING_POINTER (cst)[0])
	  && atoi (TREE_STRING_POINTER (cst)) == 0)
	val = TREE_VALUE (in);
    }
  /* %1: operand index 1 == the first input when there is one output.  */
  if (nout == 1 && nin >= 1)
    ptr = TREE_VALUE (gimple_asm_input_op (stmt, 0));
  if (!val || !ptr)
    return false;
  *value = val;
  *addr = ptr;
  return true;
}

/* One asm statement of the TU census walk: only the blocking-store
   idiom can store; record a constant-address store for the template
   audit (census_constant_store), dismiss a target provably outside
   the template file, and refuse the rest.  EXECUTABLE scopes the
   parameter-binding walk.  */

static void
census_asm (gasm *stmt, hash_set<cgraph_node *> *executable)
{
  tree value, ptr;
  if (!blocking_store_asm_p (stmt, &value, &ptr))
    return;
  unsigned HOST_WIDE_INT addr;
  if (pointer_constant_address (ptr, &addr))
    {
      census_constant_store (addr, value, stmt);
      return;
    }
  hash_set<tree> visiting;
  if (ptr_not_template_p (ptr, visiting, executable))
    return;
  census_slot_refusal ("mop-template-alias-unproven");
}

/* The executable closure of the TU under AXIOM kernel-single-TU (the
   whole thread program is this TU plus crt0/firmware): roots are
   everything the link image can enter from OUTSIDE the TU.  The link
   model (AXIOM extern-fixed-surface) makes that set precise:

   - a TU that carries its own `_start' is entered ONLY at `_start'
     (the reset vector; no external component exists that could call
     anything else) -- the raw-word census's startup axiom.  A public
     body no live code calls is then an orphaned out-of-line copy of
     an inlined function, not a hidden entry (the production trisc
     shape leaves exactly such orphans);
   - a TU with `main' but no `_start' is entered only at `main' (the
     external crt0 calls exactly `main' -- the wave-8 production
     shape this census used to unroot);
   - a TU with neither anchor can be entered at any externally-visible
     non-comdat definition (firmware -> run_kernel; pre-built external
     components can call the public surface but cannot name this TU's
     comdat instantiations, which exist only where instantiated);
   - attribute/ABI-forced and interrupt definitions can additionally
     be entered from assembly or vectors in every model.

   Roots further include static constructors and destructors, address-
   taken definitions, and every function a variable initializer
   references (the init_array entries); membership propagates through
   call edges and function references FROM members only.  A defined
   body outside the closure has no executable call path: every call
   was inlined away and nothing holds its address.  Within the model,
   over-approximation is the safe direction: an extra member only adds
   census obligations.

   *ENTRY_ROOTS receives the external entries themselves -- the
   functions whose call sites the TU cannot enumerate, which the
   parameter-binding and slot-demand resolutions must fail closed
   on.  */

static void
compute_executable_closure (hash_set<cgraph_node *> *executable,
			    hash_set<cgraph_node *> *entry_roots)
{
  auto_vec<cgraph_node *, 32> work;
  auto add = [&] (symtab_node *s)
    {
      if (cgraph_node *cn = dyn_cast <cgraph_node *> (s))
	if (!executable->add (cn))
	  work.safe_push (cn);
    };
  /* The link model's entry anchor: `_start' when the TU carries it,
     else `main' (the crt0 entry).  */
  cgraph_node *anchor_start = nullptr, *anchor_main = nullptr;
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition)
	continue;
      const char *name = DECL_ASSEMBLER_NAME (node->decl)
	? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (node->decl)) : nullptr;
      if (name && !strcmp (name, "_start"))
	anchor_start = node;
      else if (DECL_NAME (node->decl) && MAIN_NAME_P (DECL_NAME (node->decl))
	       && TREE_PUBLIC (node->decl))
	anchor_main = node;
    }
  cgraph_node *anchor = anchor_start ? anchor_start : anchor_main;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition)
	continue;
      bool externally_visible
	= TREE_PUBLIC (node->decl) && !DECL_COMDAT (node->decl);
      bool forced = DECL_PRESERVE_P (node->decl)
	|| node->force_output || node->forced_by_abi
	|| lookup_attribute ("interrupt", DECL_ATTRIBUTES (node->decl));
      /* The external entries under the link model: the anchor when one
	 pins the surface (with an in-TU `_start' the reset vector is
	 the image's ONLY external entry, so a public body no live code
	 calls is an orphaned out-of-line copy, not a hidden entry --
	 the production trisc shape leaves exactly such orphans after
	 inlining); every externally-visible non-comdat definition when
	 no anchor does; asm-callable forced definitions always.  */
      bool entry = node == anchor
	|| (!anchor && externally_visible)
	|| forced;
      if (entry
	  || DECL_STATIC_CONSTRUCTOR (node->decl)
	  || DECL_STATIC_DESTRUCTOR (node->decl)
	  || node->address_taken)
	{
	  add (node);
	  if (entry)
	    entry_roots->add (node);
	}
    }
  varpool_node *vnode;
  FOR_EACH_VARIABLE (vnode)
    {
      ipa_ref *ref;
      for (int i = 0; vnode->iterate_reference (i, ref); i++)
	add (ref->referred);
    }
  while (!work.is_empty ())
    {
      cgraph_node *cn = work.pop ();
      for (cgraph_edge *e = cn->callees; e; e = e->next_callee)
	add (e->callee);
      for (cgraph_edge *e = cn->indirect_calls; e; e = e->next_callee)
	(void) e;		/* indirect targets are the init-array
				   constructors: rooted via their
				   variable references above */
      ipa_ref *ref;
      for (int i = 0; cn->iterate_reference (i, ref); i++)
	add (ref->referred);
    }
}

/* Compute the TU-wide facts once per translation unit into the
   global tu_facts: the executable closure and its entry roots, the
   fail-closed unrooted verdict, the census of every store and
   blocking-store asm in every closure member (MOP template slot
   words, foldable-global stores), verification of the assumed global
   folds, and resolution of the deferred parameter-relative slot
   words at every reachable call site.  */

void
compute_tu_facts ()
{
  if (tu_facts.computed)
    return;
  tu_facts.computed = true;
  tu_facts.globals = new hash_map<tree, global_census_entry>;
  tu_facts.executable = new hash_set<cgraph_node *>;
  tu_facts.entry_roots = new hash_set<cgraph_node *>;

  hash_set<cgraph_node *> *executable = tu_facts.executable;
  compute_executable_closure (executable, tu_facts.entry_roots);

  /* Fail closed on an unrooted TU: defined bodies with no entry /
     constructor / externally-visible root.  A census that can see no
     entry can vouch for nothing -- the wave-8 defect was the opposite
     (vacuously "proven") verdict.  */
  if (executable->is_empty ())
    {
      cgraph_node *body_node;
      FOR_EACH_FUNCTION (body_node)
	if (body_node->definition && body_node->has_gimple_body_p ())
	  {
	    tu_facts.census_unrooted = true;
	    census_slot_refusal ("crosscall-census-unrooted");
	    if (dump_file)
	      fprintf (dump_file,
		       "crosscall-hoist: census unrooted: defined bodies "
		       "but no entry/constructor/externally-visible root "
		       "(crosscall-census-unrooted)\n");
	    break;
	  }
    }

  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;
      /* A body outside the executable closure cannot run: under AXIOM
	 kernel-single-TU (rtl-rvtt-mop-form.cc) the whole thread
	 program is this TU plus crt0/firmware, whose only entries into
	 the TU are the closure roots above.  (The production shape:
	 the retained comdat ckernel_template member bodies whose every
	 call was inlined -- their `this'-relative slot stores are dead
	 code that would otherwise refuse the audit unresolvably.)  */
      if (!executable->contains (node))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "crosscall-hoist: census skips unreachable body "
		     "%s\n", node->dump_name ());
	  continue;
	}
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      if (!ofn || !ofn->cfg || (ofn->curr_properties & PROP_rtl))
	{
	  /* A pre-materialization clone carries no body of its own; the
	     clone_of origin's is the sound over-approximation
	     (the clone's statements are the origin's
	     under parameter substitution, and any word the origin
	     leaves unresolved defers to the demands machinery).  An
	     already-EXPANDED body (PROP_rtl: the init-hoist service
	     computes the census at planner time, when the contract
	     subject itself is past gimple) has no gimple left to walk:
	     its stores cannot be audited any more, so it fails closed
	     the same way.  */
	  cgraph_node *o = node->clone_of;
	  while (o && (!DECL_STRUCT_FUNCTION (o->decl)
		       || !DECL_STRUCT_FUNCTION (o->decl)->cfg
		       || (DECL_STRUCT_FUNCTION (o->decl)->curr_properties
			   & PROP_rtl)))
	    o = o->clone_of;
	  ofn = o ? DECL_STRUCT_FUNCTION (o->decl) : nullptr;
	  if (!ofn || !ofn->cfg)
	    {
	      census_slot_refusal ("mop-template-body-unavailable");
	      if (!tu_facts.unavailable_bodies)
		tu_facts.unavailable_bodies = new hash_set<cgraph_node *>;
	      tu_facts.unavailable_bodies->add (node);
	      if (dump_file)
		fprintf (dump_file,
			 "crosscall-hoist: census body unavailable "
			 "(%s): %s\n",
			 DECL_STRUCT_FUNCTION (node->decl)
			 && (DECL_STRUCT_FUNCTION (node->decl)
			       ->curr_properties & PROP_rtl)
			 ? "already expanded" : "no gimple cfg",
			 node->dump_name ());
	      continue;
	    }
	}
      census_fname = node->dump_name ();
      census_fndecl = node->decl;
      basic_block bb;
      FOR_EACH_BB_FN (bb, ofn)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    if (is_gimple_debug (stmt))
	      continue;
	    if (gasm *a = dyn_cast <gasm *> (stmt))
	      census_asm (a, executable);
	    else
	      census_store (stmt, executable);
	  }
    }

  /* Verify the assumed global folds: a store to an assumed global
     invalidates the fold (refusing default).  */
  for (auto it = tu_facts.globals->begin ();
       it != tu_facts.globals->end (); ++it)
    if ((*it).second.assumed && (*it).second.stored_unknown)
      census_slot_refusal ("mop-template-global-fold-unproven");

  /* Resolve the deferred parameter-relative slot words at every
     reachable call site: the argument must be the address of a local
     object whose demanded field a dominating constant store filled
     (the ckernel_template::program call-site shape).  Every site of
     every demand must resolve, fail-closed.  */
  for (const slot_demand &d : tu_facts.demands)
    {
      if (tu_facts.slots_unproven)
	break;
      cgraph_node *fnode = cgraph_node::get (d.fndecl);
      if (!fnode)
	{
	  census_slot_refusal ("mop-template-slot-word-unresolved");
	  break;
	}
      /* A demanding function that is itself a closure root can be
	 called from OUTSIDE the TU with arguments the TU cannot see:
	 its in-TU call sites are not all the sites.  Fail closed.  */
      if (tu_facts.entry_roots->contains (fnode))
	{
	  census_slot_refusal ("mop-template-slot-word-unresolved");
	  if (dump_file)
	    fprintf (dump_file,
		     "crosscall-hoist: demanded slot word in closure root "
		     "%s: out-of-TU call sites cannot be enumerated\n",
		     fnode->dump_name ());
	  break;
	}
      /* A demanding function whose address is taken can be entered
	 through a function pointer with arguments no cgraph caller
	 edge carries: the enumerable direct sites are not all the
	 sites.  Fail closed (the entry-root rule above, same class).  */
      if (fnode->address_taken)
	{
	  census_slot_refusal ("mop-template-slot-caller-unenumerable");
	  if (dump_file)
	    fprintf (dump_file,
		     "crosscall-hoist: demanded slot word in address-taken "
		     "%s: indirect call sites cannot be enumerated "
		     "(mop-template-slot-caller-unenumerable)\n",
		     fnode->dump_name ());
	  break;
	}
      unsigned n_resolved = 0;
      for (cgraph_edge *e = fnode->callers; e; e = e->next_caller)
	{
	  if (!executable->contains (e->caller))
	    continue;
	  gcall *call = e->call_stmt;
	  tree arg = call && d.parm_index < gimple_call_num_args (call)
	    ? gimple_call_arg (call, d.parm_index) : NULL_TREE;
	  uint32_t word;
	  if (!arg || TREE_CODE (arg) != ADDR_EXPR
	      || !DECL_P (TREE_OPERAND (arg, 0))
	      || !resolve_object_field_at (call, TREE_OPERAND (arg, 0),
					   d.offset, &word))
	    {
	      census_slot_refusal ("mop-template-slot-word-unresolved");
	      if (dump_file)
		{
		  fprintf (dump_file,
			   "crosscall-hoist: demanded slot word "
			   "unresolved at call site in %s: ",
			   e->caller->dump_name ());
		  if (call)
		    print_gimple_stmt (dump_file, call, 0, TDF_NONE);
		}
	      break;
	    }
	  audit_slot_word (word);
	  ++n_resolved;
	}
      /* Zero enumerable executable call sites satisfy the demand only
	 VACUOUSLY -- the comment above this resolver says fail-closed,
	 so at least one resolved site must vouch for every demand
	 (the parameter-binding closure's !callers rule).  */
      if (!tu_facts.slots_unproven && n_resolved == 0)
	{
	  census_slot_refusal ("mop-template-slot-caller-unenumerable");
	  if (dump_file)
	    fprintf (dump_file,
		     "crosscall-hoist: demanded slot word in %s has no "
		     "enumerable executable call site "
		     "(mop-template-slot-caller-unenumerable)\n",
		     fnode->dump_name ());
	  break;
	}
    }

  if (dump_file)
    fprintf (dump_file,
	     "crosscall-hoist: TU template audit: %s%s%s loadi-dests=%#x\n",
	     tu_facts.slots_unproven ? "UNPROVEN (" : "proven",
	     tu_facts.slots_unproven ? tu_facts.slot_reason : "",
	     tu_facts.slots_unproven ? ")" : "",
	     tu_facts.slot_loadi_dests);
}

/* The MOP admission for a contract: every instruction slot audited,
   no REPLAY slot, no SFPLOADI slot writing a contract register.  */

bool
mop_contract_ok_p (unsigned contract_mask, const char **why)
{
  if (tu_facts.slots_unproven)
    {
      *why = tu_facts.slot_reason;
      return false;
    }
  if (tu_facts.slot_replay)
    {
      *why = "mop-template-replay-unproven";
      return false;
    }
  if (tu_facts.slot_loadi_dests & contract_mask)
    {
      *why = "mop-template-loadi-contract";
      return false;
    }
  return true;
}

/* ------------------------------------------------------------------ */
/* Statement classification for the caller's loop epoch (and the
   callee's own body): every statement must be proven unable to write
   a contract LREG.  */

/* The audited scalar asm templates (the same list the epoch pass and
   the prgm-const scan carry: base-ISA instructions with no Tensix
   encoding space).  */

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
      if (name && !strcmp (name, "__instrn_buffer"))
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
		refuse ("crosscall-consumer-not-pinned", fn->decl, load);
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
	return refuse ("crosscall-consumer-conflict", fn->decl, e.load);
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
	  return refuse ("crosscall-callee-vector-outside-loop", fn->decl,
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
		return refuse ("crosscall-callee-vector-outside-loop",
			       fn->decl, stmt);
	    }
	  if (!is_contract_load && !scan_stmt (ctx, stmt, /*in_caller=*/false))
	    return refuse (ctx->why, fn->decl, ctx->why_stmt);
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
    return refuse ("crosscall-callee-pressure", fn->decl, nullptr);

  /* Every return must be dominated by every load (the exit write-back
     uses the load's SSA value).  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    for (const contract_entry &c : contract)
      if (!dominated_by_p (CDI_DOMINATORS, e->src, gimple_bb (c.load)))
	return refuse ("crosscall-callee-shape-unproven", fn->decl, c.load);

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
    return refuse ("crosscall-caller-body-unavailable", caller->decl,
		   call_stmt);
  class loop *loop = bb->loop_father;
  if (!loop || !loop_outer (loop))
    return refuse ("crosscall-caller-no-loop", caller->decl, call_stmt);

  edge entry = rvtt_loop_entry_edge (loop);
  if (!entry || rvtt_preheader_insertion_blocked_p (entry))
    return refuse ("crosscall-caller-preheader-unproven", caller->decl,
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
	  ok = refuse ("crosscall-caller-lreg-live", caller->decl,
		       psi.phi ());
      for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	   ok && !gsi_end_p (gsi); gsi_next (&gsi))
	if (!scan_stmt (&ctx, gsi_stmt (gsi), /*in_caller=*/true))
	  ok = refuse (ctx.why, caller->decl, ctx.why_stmt);
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
	  refuse ("crosscall-caller-mop-slot-unproven", caller->decl,
		  call_stmt);
	  if (dump_file && why)
	    fprintf (dump_file, "crosscall-hoist:   (%s)\n", why);
	  return false;
	}
    }

  if (vector_value_live_in_loop_p (fn, place_loop))
    return refuse ("crosscall-caller-lreg-live", caller->decl, call_stmt);

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
		return refuse ("crosscall-caller-foreign-contract",
			       caller->decl, call);
	    }
	  else if (insnd->id == rvtt_insn_data::sfprawlreg_access)
	    {
	      tree rel = gimple_call_arg (call, 0);
	      if (TREE_CODE (rel) != INTEGER_CST
		  || (TREE_INT_CST_LOW (rel)
		      & (contract_mask | config_mask)))
		return refuse ("crosscall-caller-foreign-contract",
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
	return refuse ("crosscall-caller-mop-slot-unproven", fn->decl,
		       nullptr);
    }

  /* The caller closure: a definition, no aliases/thunks/clones, not
     address-taken, at least one caller, no recursion.	*/
  if (!cn->definition || cn->address_taken || cn->alias || cn->thunk
      || cn->clones || !cn->callers)
    return refuse ("crosscall-caller-body-unavailable", fn->decl, nullptr);

  /* One call site per caller (v1); collect and prove each caller.  */
  auto_vec<caller_plan> plans;
  for (cgraph_edge *e = cn->callers; e; e = e->next_caller)
    {
      if (e->caller == cn)
	return refuse ("crosscall-caller-body-unavailable", fn->decl,
		       nullptr);
      for (const caller_plan &p : plans)
	if (p.node == e->caller)
	  return refuse ("crosscall-caller-multi-site", e->caller->decl,
			 nullptr);
      if (!e->caller->definition || !e->caller->has_gimple_body_p ()
	  || !e->call_stmt)
	return refuse ("crosscall-caller-body-unavailable",
		       e->caller->decl, nullptr);
      /* A caller outside the TU executable closure: the census never
	 vouched for its stores, so no proof over it can consult the
	 template audit consistently (the wave-8 internal-inconsistency
	 shape: proving a caller epoch the census skipped).  */
      cgraph_node *ccheck = e->caller->inlined_to
	? e->caller->inlined_to : e->caller;
      if (tu_facts.executable && !tu_facts.executable->contains (ccheck))
	return refuse ("crosscall-caller-unrooted", ccheck->decl, nullptr);
      function *cfn = DECL_STRUCT_FUNCTION (e->caller->decl);
      if (!cfn || !cfn->cfg)
	return refuse ("crosscall-caller-body-unavailable",
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
