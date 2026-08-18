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
     loop body -- and therefore the original per-call prefix --
     would have executed at least once.  Zero-trip paths keep the
     original register contents bit-for-bit;
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
   - every store anywhere in the TU that can reach the MOP template
     file (constant-address stores into the architected nine words;
     volatile stores whose address cannot be proven elsewhere fail
     closed) programs an instruction slot with a constant word whose
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
     crosscall-callee-stmt-unproven	call/asm/word in the callee not
					proven contract-inert
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
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"

namespace {

/* ------------------------------------------------------------------ */
/* Refusal plumbing.						      */

static bool
refuse (const char *reason, tree fn, gimple *stmt)
{
  if (dump_file)
    {
      fprintf (dump_file, "crosscall-hoist: refused (%s)", reason);
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

static bool
vector_typed_p (tree t)
{
  return t && TREE_TYPE (t) && VECTOR_TYPE_P (TREE_TYPE (t));
}

/* Any vector-typed operand (lhs or argument) on a call.  */

static bool
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

   Mirrors the audited raw-word capability table of rvtt-mop-derive.cc
   (provenance recorded there per class), asking the LREG question
   instead of the PRGM/CC one: can this word write an ALLOCATABLE hard
   LREG in the contract set?  Refusing default for every class not on
   record.  */

struct word_verdict
{
  bool ok;			/* audited contract-LREG-inert	     */
  bool is_mop;			/* defer to the TU template audit    */
  bool is_replay;		/* recorded content: refuse	     */
  const char *why;
};

static word_verdict
classify_word_lreg (uint32_t word, unsigned contract_mask)
{
  word_verdict v = { true, false, false, nullptr };
  unsigned opcode = word >> 24;
  switch (opcode)
    {
    case 0x00:			/* TENSIX NOP (zero word)	     */
    case 0x02:			/* Tensix NOP: swallowed at the FIFO */
      return v;
    case XTT_MOP_OPCODE:	/* effects live in the template file */
      v.is_mop = true;
      return v;
    case XTT_MOP_CFG_OPCODE:	/* zmask high half only		     */
      return v;
    case XTT_REPLAY_OPCODE:	/* plays back recorded content	     */
      v.ok = false;
      v.is_replay = true;
      v.why = "crosscall-caller-replay-unproven";
      return v;
    case 0x12:			/* MOVA2D: Dst rows only, l_regs
				   untouched (spec + sim facts recorded
				   in rvtt-mop-derive.cc)	     */
    case 0x28:			/* ELWADD: matrix-unit state only    */
    case 0x36:			/* CLEARDVALID			     */
    case 0x37:			/* SETRWC: RWC counters/bank valids
				   only (rvtt-raw-boundary.h class)  */
      return v;
    case 0x71:			/* SFPLOADI: writes LREG bits 23:20  */
      if ((contract_mask >> ((word >> 20) & 0xf)) & 1)
	{
	  v.ok = false;
	  v.why = "crosscall-caller-word-unproven";
	}
      return v;
    case 0x91:			/* SFPCONFIG: LReg writes exist solely
				   in the VD 11..14 arm (constant
				   registers, never allocatable L0-7;
				   spec case-15 + sim facts recorded in
				   rvtt-mop-derive.cc)		     */
      return v;
    default:
      if (opcode >= 0xA0 && opcode <= 0xA7)	/* sync family	     */
	return v;
      if (opcode >= 0xB0 && opcode <= 0xB8)	/* thread-config     */
	return v;
      v.ok = false;
      v.why = "crosscall-caller-word-unproven";
      return v;
    }
}

/* Constant-base extraction for a composed pushed word, per AXIOM
   tt-op-field-discipline (rtl-rvtt-mop-form.cc file header: runtime
   operands of a TT_OP composition stay inside their bit fields, the
   discipline the TT_OP macro family itself encodes).  Returns the
   constant base word through *BASE, or false when no constant base
   pins the opcode byte.  Mirrors rtl-rvtt-mop-form.cc's
   mop_pushed_word_base, additionally reporting the full base so field
   checks below the opcode can be applied where the class needs them.  */

static bool
pushed_word_base (tree val, uint32_t *base, unsigned depth = 0)
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
static bool blocking_store_asm_p (const gasm *stmt, tree *value, tree *addr);
static bool pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
				      unsigned depth = 0);

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
classify_delivered_value (tree val, unsigned contract_mask)
{
  word_verdict v = { false, false, false, "crosscall-caller-word-unproven" };
  if (TREE_CODE (val) == INTEGER_CST)
    return classify_word_lreg ((uint32_t) (TREE_INT_CST_LOW (val)
					   & 0xffffffff), contract_mask);
  uint32_t base;
  if (!pushed_word_base (val, &base))
    return v;
  unsigned opcode = base >> 24;
  if (opcode == 0x71)
    /* Runtime-completed SFPLOADI: the destination field is not pinned
       by the base under the field axiom alone.  */
    return v;
  return classify_word_lreg (base, contract_mask);
}

/* ------------------------------------------------------------------ */
/* Constant address resolution (stores).  Mirrors the pointer folding
   of rtl-rvtt-mop-form.cc (constant int-to-pointer chases) plus a
   refusing-default load-of-foldable-global step for the LLK aperture
   globals (TU-defined, never address-taken, constant initializer,
   never stored differently -- the census below verifies the last
   condition over the same whole-TU walk).  */

struct global_census_entry
{
  bool stored_unknown = false;	 /* some store didn't fold / disagreed */
  bool assumed = false;		 /* some load assumed the initializer  */
};

/* A template-slot word that is a parameter-relative field load (the
   out-of-line ckernel_template::program shape): the word is resolved
   at every reachable call site instead, from the constant field
   stores that dominate the call.  */

struct slot_demand
{
  tree fndecl;			/* the demanding function	     */
  unsigned parm_index;		/* which argument carries the object */
  HOST_WIDE_INT offset;		/* field bit offset within it	     */
};

struct crosscall_tu_facts
{
  bool computed = false;
  /* The MOP template-file audit.  */
  bool slots_unproven = false;
  const char *slot_reason = nullptr;
  bool slot_replay = false;
  unsigned slot_loadi_dests = 0;   /* SFPLOADI destinations programmed
				      into instruction slots	       */
  hash_map<tree, global_census_entry> *globals = nullptr;
  vec<slot_demand> demands = vNULL;
};

static crosscall_tu_facts tu_facts;

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

static bool
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
	  if (tu_facts.globals)
	    tu_facts.globals->get_or_insert (rhs1).assumed = true;
	  *addr = value;
	  return true;
	}
    }
  return false;
}

/* Fold REF (a store lhs) to a constant byte address if possible
   (mirrors rtl-rvtt-mop-form.cc mop_ref_constant_address).  */

static bool
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
}

/* Audit one resolved instruction-slot word (slots 2..8).  */

static void
audit_slot_word (uint32_t word)
{
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
  word_verdict v = classify_word_lreg (word, 0);
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

static bool
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
   whole thread program is this TU plus crt0): roots are the link
   model's entry anchor (`_start', the crt0 entry the raw-word census
   already treats as the startup axiom), static constructors and
   destructors, anything forced live for the ABI or by attribute, and
   every function a variable initializer references (the init_array
   entries); membership propagates through call edges and function
   references FROM members only.  A defined body outside the closure --
   comdat or not -- has no executable call path: every call was inlined
   away and nothing holds its address.  Over-approximation is the safe
   direction: an extra member only adds census obligations.  */

static void
compute_executable_closure (hash_set<cgraph_node *> *executable)
{
  auto_vec<cgraph_node *, 32> work;
  auto add = [&] (symtab_node *s)
    {
      if (cgraph_node *cn = dyn_cast <cgraph_node *> (s))
	if (!executable->add (cn))
	  work.safe_push (cn);
    };
  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition)
	continue;
      const char *name = DECL_ASSEMBLER_NAME (node->decl)
	? IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (node->decl)) : nullptr;
      if ((name && !strcmp (name, "_start"))
	  || DECL_STATIC_CONSTRUCTOR (node->decl)
	  || DECL_STATIC_DESTRUCTOR (node->decl)
	  || DECL_PRESERVE_P (node->decl)
	  || node->force_output || node->forced_by_abi
	  || node->address_taken)
	add (node);
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

static void
compute_tu_facts ()
{
  if (tu_facts.computed)
    return;
  tu_facts.computed = true;
  tu_facts.globals = new hash_map<tree, global_census_entry>;

  hash_set<cgraph_node *> executable_set;
  hash_set<cgraph_node *> *executable = &executable_set;
  compute_executable_closure (executable);

  cgraph_node *node;
  FOR_EACH_FUNCTION (node)
    {
      if (!node->definition || !node->has_gimple_body_p ())
	continue;
      /* A comdat body outside the executable closure cannot run: under
	 AXIOM kernel-single-TU (rtl-rvtt-mop-form.cc) the whole thread
	 program is this TU plus crt0, so only the closure computed
	 below can reach it.  (The production shape: the retained
	 ckernel_template member bodies whose every call was inlined --
	 their `this'-relative slot stores are dead code that would
	 otherwise refuse the audit unresolvably.)  */
      if (!executable->contains (node))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "crosscall-hoist: census skips unreachable body "
		     "%s\n", node->dump_name ());
	  continue;
	}
      function *ofn = DECL_STRUCT_FUNCTION (node->decl);
      if (!ofn || !ofn->cfg)
	{
	  census_slot_refusal ("mop-template-body-unavailable");
	  continue;
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

static bool
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

static bool
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

struct scan_ctx
{
  unsigned contract_mask;
  tree callee_decl;		/* the contract call target (caller scan);
				   NULL_TREE for the callee's own scan */
  bool saw_mop = false;
  const char *why = nullptr;
  gimple *why_stmt = nullptr;
};

static bool
scan_refuse (scan_ctx *ctx, const char *why, gimple *stmt)
{
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
	   (TREE_VALUE (gimple_asm_input_op (stmt, 0)), ctx->contract_mask),
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
	      (ctx, classify_delivered_value (value, ctx->contract_mask),
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
					  ctx->contract_mask), stmt);
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
					  ctx->contract_mask), stmt);
      if (!DECL_EXTERNAL (base))
	return true;		/* TU data object */
    }
  return scan_refuse (ctx, "crosscall-caller-word-unproven", stmt);
}

/* One statement of a scanned range.  IN_CALLER selects the caller-loop
   discipline (the contract call is admitted; vector dataflow refuses);
   the callee scan admits vector dataflow (register allocation resolves
   it against the pinned live range) but refuses the same delivered
   words, calls, and explicit-contract accesses.  */

static bool
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
	    return scan_refuse (ctx, in_caller
				? "crosscall-caller-cc-unproven"
				: "crosscall-callee-cc-unproven", stmt);
	  switch (insnd->id)
	    {
	    case rvtt_insn_data::sfpreadlreg:
	    case rvtt_insn_data::sfpwritelreg:
	      {
		tree regno = gimple_call_arg
		  (call, insnd->id == rvtt_insn_data::sfpwritelreg ? 1 : 0);
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

struct caller_plan
{
  cgraph_node *node;
  gcall *call_stmt;
  class loop *loop;		/* valid only while the caller's loop
				   state below is live		     */
  edge entry;
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

/* ------------------------------------------------------------------ */
/* Callee-wide checks beyond the loop scans.			      */

static bool
callee_body_ok_p (function *fn, const auto_vec<contract_entry> &contract,
		  class loop *consumer_loop, unsigned contract_mask,
		  scan_ctx *ctx)
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
	     otherwise vector-free).  */
	  bool is_contract_load = false;
	  for (const contract_entry &e : contract)
	    if (stmt == e.load)
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
     eight-LREG file (the shared conservative pressure proof).  */
  auto_vec<gcall *> loads;
  for (const contract_entry &e : contract)
    loads.safe_push (e.load);
  if (!rvtt_loop_lreg_pressure_legal_p (consumer_loop, loads,
					/*report=*/false))
    return refuse ("crosscall-callee-pressure", fn->decl, nullptr);

  /* Every return must be dominated by every load (the exit write-back
     uses the load's SSA value).  */
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fn)->preds)
    for (const contract_entry &c : contract)
      if (!dominated_by_p (CDI_DOMINATORS, e->src, gimple_bb (c.load)))
	return refuse ("crosscall-callee-shape-unproven", fn->decl, c.load);

  (void) contract_mask;
  return true;
}

/* ------------------------------------------------------------------ */
/* Caller-side proof for one cgraph caller.  Runs under the CALLER's
   cfun (push_cfun done by the caller of this function).	      */

static bool
prove_caller (cgraph_node *caller, gcall *call_stmt, tree callee_decl,
	      unsigned contract_mask, edge *entry_out)
{
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
  ctx.contract_mask = contract_mask;
  ctx.callee_decl = callee_decl;

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

  if (ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_contract_ok_p (contract_mask, &why))
	{
	  refuse ("crosscall-caller-mop-slot-unproven", caller->decl,
		  call_stmt);
	  if (dump_file && why)
	    fprintf (dump_file, "crosscall-hoist:   (%s)\n", why);
	  return false;
	}
    }

  if (vector_value_live_in_loop_p (fn, loop))
    return refuse ("crosscall-caller-lreg-live", caller->decl, call_stmt);

  *entry_out = entry;
  return true;
}

/* ------------------------------------------------------------------ */
/* Commit.							      */

/* Insert STMT at the tail of the preheader block PH (before a
   block-terminating statement if one ends the block -- the same
   insertion rule the prgm-const programming point uses).  */

static void
insert_in_preheader (basic_block ph, gimple *stmt)
{
  gimple_stmt_iterator gsi = gsi_last_bb (ph);
  if (gsi_end_p (gsi) || !stmt_ends_bb_p (gsi_stmt (gsi)))
    gsi_insert_after (&gsi, stmt, GSI_NEW_STMT);
  else
    gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);
}

static void
commit_caller (cgraph_node *caller, edge entry,
	       const auto_vec<contract_entry> &contract)
{
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);
  basic_block ph = rvtt_commit_hoist_preheader (entry);
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

static void
commit_callee (function *fn, const auto_vec<contract_entry> &contract)
{
  const rvtt_insn_data *read_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *write_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwritelreg);

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

  /* Callee-side proofs.  */
  scan_ctx callee_ctx;
  callee_ctx.contract_mask = contract_mask;
  callee_ctx.callee_decl = NULL_TREE;
  if (!callee_body_ok_p (fn, contract, consumer_loop, contract_mask,
			 &callee_ctx))
    return false;
  if (callee_ctx.saw_mop)
    {
      const char *why = nullptr;
      if (!mop_contract_ok_p (contract_mask, &why))
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
      function *cfn = DECL_STRUCT_FUNCTION (e->caller->decl);
      if (!cfn || !cfn->cfg)
	return refuse ("crosscall-caller-body-unavailable",
		       e->caller->decl, nullptr);
      caller_plan p = { e->caller, e->call_stmt, nullptr, nullptr };
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
			      &p.entry);
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
	 unchanged since the proof above).  */
      class loop *loop = gimple_bb (p.call_stmt)->loop_father;
      edge entry = rvtt_loop_entry_edge (loop);
      gcc_assert (entry);
      commit_caller (p.node, entry, contract);
      loop_optimizer_finalize ();
      pop_cfun ();
    }

  commit_callee (fn, contract);

  if (dump_file)
    fprintf (dump_file,
	     "crosscall-hoist: hoisted %u contract materializations from "
	     "%s into %u caller(s)\n",
	     contract.length (),
	     IDENTIFIER_POINTER (DECL_NAME (fn->decl)), plans.length ());
  return true;
}

const pass_data pass_data_rvtt_crosscall =
{
  GIMPLE_PASS, /* type */
  "rvtt_crosscall", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
	if (dump_file)
	  fprintf (dump_file,
		   "crosscall-hoist: refused (qsr-unproven)\n");
	return 0;
      }
    /* TU facts first, while every body is still gimple (the
       prgm-const timing argument).  */
    compute_tu_facts ();
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    if (!dom_info_available_p (CDI_DOMINATORS))
      calculate_dominance_info (CDI_DOMINATORS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_crosscall (gcc::context *ctxt)
{
  return new pass_rvtt_crosscall (ctxt);
}
