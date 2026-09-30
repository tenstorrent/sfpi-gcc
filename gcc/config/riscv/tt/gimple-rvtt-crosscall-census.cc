/* Interprocedural LICM across noinline boundaries: the TU-wide
   template-file audit (Layer: crosscall census).
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

/* Split out of gimple-rvtt-crosscall.cc, which had grown past 3000
   lines.  This half is self-contained: the audited 32-bit word
   classification on the LREG face, constant address resolution for
   stores, and the TU-wide MOP template-file census those two feed.
   The driver, the caller/callee proofs and the commit stay in the
   original file.  Six symbols cross the boundary and are declared in
   gimple-rvtt-crosscall-int.h; nothing here is otherwise shared.  */

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
/* Audited 32-bit word classification (LREG face).

   The classifier now lives in THE unified audited word-fact table
   (rvtt-raw-boundary.cc rvtt_word_facts_classify);
   rvtt_word_lreg_class is this face's query accessor
   -- same question (can this word write an ALLOCATABLE hard LREG in
   the contract set?), same verdicts, same refusal names, refusing
   default for every class not on record.  The verdict struct keeps
   its local spelling.  */

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

word_verdict
classify_delivered_value (tree val, unsigned contract_mask,
			  bool region_strict, bool config_strict,
			  unsigned phi_depth)
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
  if (rvtt_instrn_buffer_name_p (name))
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

