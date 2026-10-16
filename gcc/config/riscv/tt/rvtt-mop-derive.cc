/* MOP template-effect derivation for the TU-wide PRGM freedom proof.
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

/* PRGM constant registers may be allocated only under a TU-wide
   freedom proof: nothing in the TU may write a PRGM register,
   LaneConfig, or the CC state unaudited.  Raw `.ttinsn' words decode
   through the audited table, except MOP (frontend opcode 0x01), which
   expands the words held in the nine MOP template registers.  This
   file derives those effects from the TU's own template-programming
   stores and decodes them through the same audited table.

   The derivation is flow-insensitive: the proof does not need to know
   which template a given MOP expands, only that no template word
   programmable in this TU touches owned state unaudited.  Slots 0..1
   are loop lengths or flags under both MOP types and never expand as
   instructions; every value stored in slots 2..8 must decode through
   the audited table (rvtt-mop-tables.h).  A MOP that runs with no
   in-TU programming expands the audited thread-entry template state.

   A volatile store of a raw instruction word delivers or programs
   Tensix work just as a `.ttinsn' word does, so stores are scanned
   too.  A constant-address store classifies by target range: the MOP
   template file (slot rules above), the instruction-FIFO aperture (the
   stored word goes through the audited table), the PC_BUF sync and
   semaphore words (inert), the RISCV_DEBUG_REGS block (refused: it
   provides an instruction-injection interface), anything else
   (inert).  A non-constant-address volatile store must be proven not
   to alias an instruction FIFO: an address derived from a TU-defined
   variable or a C-runtime data-image anchor cannot; `__instrn_buffer'
   is the FIFO and its stored word is classified; any other address
   refuses.

   Runtime-composed words classify by the constant opcode base of
   their PLUS/BIT_IOR composition, and are admissible only for opcode
   classes the table admits for every field value (AXIOM
   tt-op-field-discipline, rvtt-mop-tables.h).

   The C runtime's init-array walk is an indirect call that would
   otherwise refuse the TU; rvtt_mop_init_array_call_p admits it when
   the callee is loaded through a pointer derived only from
   `__init_array_start', and nothing but the TU's registered static
   constructors (all scanned) can populate .init_array (AXIOM
   kernel-single-TU: one translation unit per TRISC image).

   Refusal reasons: mop-template-word-unproven,
   mop-template-replay-unproven, mop-template-nested-unproven,
   mop-store-alias-unproven, and `unaudited raw opcode' for directly
   delivered words.  The address folding helpers follow those of
   rtl-rvtt-mop-outward.cc, and additionally fold loads of TU global
   pointers whose value is verified at the end of the scan.  */

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
#include "tree-ssanames.h"
#include "cgraph.h"
#include "varasm.h"
#include "stringpool.h"
#include "attribs.h"
#include "dumpfile.h"
#include "tree-pretty-print.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"

/* ------------------------------------------------------------------ */
/* The audited raw-word table (rvtt_mop_audited_word_p, declared in
   rvtt-mop-derive.h) is implemented in rvtt-raw-boundary.cc on top of
   rvtt_word_facts_classify, which the LREG, CC, ADDR_MOD and Dst/RWC
   checks share.  */

namespace {

static bool pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
				      unsigned depth);

/* ------------------------------------------------------------------ */
/* TU global-pointer values (assumed during the scan, verified in
   rvtt_mop_derive_finish).

   The LLK names its hardware apertures through TU-defined global
   pointer variables with constant initializers (ckernel_helper.h:
   pc_buf_base, regfile, ...).  A load of such a global folds to its
   initializer provided the TU never stores a different value into it.
   Stores are discovered as bodies are walked, so a load assumes the
   initializer value and records the assumption, rvtt_mop_derive_store
   scans every direct store to a candidate global, and
   rvtt_mop_derive_finish checks that each assumed global has no store
   or only stores of the same constant.  The scan is complete because
   the global is TU-defined and not addressable, and every admitted asm
   template is store-transparent.  */

struct global_ptr_scan
{
  bool stored = false;		/* some direct store exists */
  bool unknown = false;		/* a store didn't fold / disagreed */
  unsigned HOST_WIDE_INT value = 0;
};

static hash_map<tree, global_ptr_scan> *global_ptr_stores;
static hash_set<tree> *assumed_globals;

/* A global whose loads the derivation may fold: TU-defined scalar
   pointer, never address-taken (every store is a direct visible
   assignment), with a constant-foldable initializer.  */

static bool
foldable_global_pointer_p (tree decl, unsigned HOST_WIDE_INT *val)
{
  if (!VAR_P (decl) || DECL_EXTERNAL (decl) || !TREE_STATIC (decl)
      || TREE_ADDRESSABLE (decl) || TREE_THIS_VOLATILE (decl)
      || !POINTER_TYPE_P (TREE_TYPE (decl)))
    return false;
  tree init = DECL_INITIAL (decl);
  if (!init)
    return false;
  STRIP_NOPS (init);
  if (TREE_CODE (init) != INTEGER_CST || !tree_fits_uhwi_p (init))
    return false;
  *val = tree_to_uhwi (init) & 0xffffffff;
  return true;
}

/* Scan one direct store to a candidate global (called for every
   scanned store whose lhs is a VAR_DECL).  */

static void
scan_global_pointer_store (tree decl, tree value)
{
  unsigned HOST_WIDE_INT ignore;
  if (!foldable_global_pointer_p (decl, &ignore))
    return;
  if (!global_ptr_stores)
    global_ptr_stores = new hash_map<tree, global_ptr_scan>;
  global_ptr_scan &c = global_ptr_stores->get_or_insert (decl);
  unsigned HOST_WIDE_INT v;
  if (!pointer_constant_address (value, &v, 0)
      || (c.stored && !c.unknown && c.value != v))
    c.unknown = true;
  else
    c.value = v;
  c.stored = true;
}

/* Assume DECL's value is its initializer constant; verified at
   finish.  */

static bool
assume_global_pointer_value (tree decl, unsigned HOST_WIDE_INT *val)
{
  if (!foldable_global_pointer_p (decl, val))
    return false;
  if (!assumed_globals)
    assumed_globals = new hash_set<tree>;
  assumed_globals->add (decl);
  return true;
}

/* ------------------------------------------------------------------ */
/* Address folding.  */

/* Fold PTR (a pointer value) to a constant byte address, following a
   short SSA chain of casts, constant pointer arithmetic, and loads of
   foldable global pointers (assumed + verified above).  */

static bool
pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
			  unsigned depth = 0)
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
  /* A load of a foldable TU global pointer: assume the initializer
     value (verified against the TU store scan at finish).  */
  if (gimple_assign_load_p (def) && DECL_P (gimple_assign_rhs1 (def)))
    return assume_global_pointer_value (gimple_assign_rhs1 (def), addr);
  if (CONVERT_EXPR_CODE_P (code) || code == INTEGER_CST || code == SSA_NAME)
    return pointer_constant_address (gimple_assign_rhs1 (def), addr,
				     depth + 1);
  if (code == POINTER_PLUS_EXPR || code == PLUS_EXPR)
    {
      tree off = gimple_assign_rhs2 (def);
      unsigned HOST_WIDE_INT base;
      if (TREE_CODE (off) != INTEGER_CST || !tree_fits_shwi_p (off)
	  || !pointer_constant_address (gimple_assign_rhs1 (def), &base,
					depth + 1))
	return false;
      *addr = (base + (unsigned HOST_WIDE_INT) tree_to_shwi (off))
	      & 0xffffffff;
      return true;
    }
  return false;
}

/* Fold REF (a store lhs) to a constant byte address if possible.  */

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

/* ---- Context-bound resolution (contract in
   rvtt-mop-derive.h).  */

} /* anonymous namespace */

/* Resolve VAL through the PARM-binding chain of *CTX_IO: while VAL is
   the default definition of a bound parameter, replace it by the
   driving call's actual argument, read under the caller's context.
   The hop bound only limits work; an unresolved chain refuses
   downstream.  */

tree
rvtt_mop_resolve_bound (tree val, rvtt_mop_scan_ctx **ctx_io)
{
  for (unsigned hop = 0; hop != 8; ++hop)
    {
      rvtt_mop_scan_ctx *ctx = *ctx_io;
      if (!ctx || !ctx->parms || !val || TREE_CODE (val) != SSA_NAME
	  || !SSA_NAME_IS_DEFAULT_DEF (val)
	  || !SSA_NAME_VAR (val)
	  || TREE_CODE (SSA_NAME_VAR (val)) != PARM_DECL)
	return val;
      rvtt_mop_bound_arg *bound = ctx->parms->get (SSA_NAME_VAR (val));
      if (!bound)
	return val;
      val = bound->value;
      *ctx_io = bound->ctx;
    }
  return val;
}

/* Poison VAR in CTX's field scan: its address escaped the scanned
   subtree, so unaudited stores to its fields can no longer be
   excluded and every load from it must refuse.  No-op without a
   scan (a root scan that recorded nothing).  */

void
rvtt_mop_scan_poison (rvtt_mop_scan_ctx *ctx, tree var)
{
  if (ctx && ctx->scan && var)
    ctx->scan->poisoned.add (var);
}

namespace {

/* The automatic local aggregate a pointer value provably addresses, or
   NULL_TREE: the value must resolve (through the binding chain) to the
   address of an automatic VAR_DECL.  Anything else -- globals, unknown
   pointers, offsets -- stays unresolved and the consumer refuses.  */

static tree
bound_local_object (tree ptr, rvtt_mop_scan_ctx *ctx)
{
  rvtt_mop_scan_ctx *c = ctx;
  ptr = rvtt_mop_resolve_bound (ptr, &c);
  if (ptr && TREE_CODE (ptr) == ADDR_EXPR)
    {
      tree var = TREE_OPERAND (ptr, 0);
      if (VAR_P (var) && !TREE_STATIC (var) && !DECL_EXTERNAL (var))
	return var;
    }
  return NULL_TREE;
}

/* The scanned automatic aggregate and field a memory reference REF
   denotes: a direct field of an automatic local
   (var.field / COMPONENT_REF(VAR_DECL)), or a field reached through a
   bound this-pointer (COMPONENT_REF(MEM_REF(bound &var, 0))).  */

static bool
scanned_field_ref (tree ref, rvtt_mop_scan_ctx *ctx, tree *var, tree *field)
{
  if (!ref || TREE_CODE (ref) != COMPONENT_REF)
    return false;
  tree base = TREE_OPERAND (ref, 0);
  *field = TREE_OPERAND (ref, 1);
  if (VAR_P (base) && !TREE_STATIC (base) && !DECL_EXTERNAL (base))
    {
      *var = base;
      return true;
    }
  if (TREE_CODE (base) == MEM_REF
      && integer_zerop (TREE_OPERAND (base, 1)))
    {
      *var = bound_local_object (TREE_OPERAND (base, 0), ctx);
      return *var != NULL_TREE;
    }
  return false;
}

/* Classify the 32-bit word VAL by the constant opcode base of its
   PLUS / BIT_IOR composition (AXIOM tt-op-field-discipline,
   rvtt-mop-tables.h).  Returns the frontend opcode byte, or -1 when no
   constant base pins it.  */

static int
pushed_word_opcode_byte (tree val, unsigned depth = 0,
		  rvtt_mop_scan_ctx *ctx = nullptr)
{
  if (depth > 12 || !val)
    return -1;
  {
    rvtt_mop_scan_ctx *c = ctx;
    tree bound = rvtt_mop_resolve_bound (val, &c);
    if (bound != val)
      return pushed_word_opcode_byte (bound, depth + 1, c);
  }
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	return -1;
      unsigned HOST_WIDE_INT w = TREE_INT_CST_LOW (val) & 0xffffffff;
      return (int) (w >> 24);
    }
  if (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (!def || !is_gimple_assign (def))
	return -1;
      tree_code code = gimple_assign_rhs_code (def);
      if (code == PLUS_EXPR || code == BIT_IOR_EXPR)
	{
	  int a = pushed_word_opcode_byte (gimple_assign_rhs1 (def),
					   depth + 1, ctx);
	  int b = pushed_word_opcode_byte (gimple_assign_rhs2 (def),
					   depth + 1, ctx);
	  /* Exactly one side carries the opcode base; two competing
	     bases (or none) leave the word unclassified.  */
	  if (a > 0 && b <= 0)
	    return a;
	  if (b > 0 && a <= 0)
	    return b;
	  if (a == 0 && b == 0)
	    return 0;
	  return -1;
	}
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME || code == NOP_EXPR)
	return pushed_word_opcode_byte (gimple_assign_rhs1 (def),
					depth + 1, ctx);
      /* Shifted single fields below the opcode byte cannot construct
	 an opcode by themselves under the discipline axiom.  */
      if (code == LSHIFT_EXPR || code == BIT_AND_EXPR || code == RSHIFT_EXPR)
	return 0;
      return -1;
    }
  return -1;
}

/* A composed (non-constant) word with constant opcode base BASE is
   admissible exactly when the audited table admits opcode BASE for
   every field value: under the field-discipline axiom the runtime
   operands stay inside their bit fields, but which fields they occupy
   is unknown, so any field the table reads (SFPLOADI dest, SFPCONFIG
   dest/mod/imm) could be runtime and the class refuses.  */

static bool
field_insensitive_audited_class_p (int base)
{
  return base == 0x00 || base == 0x02	     /* NOP classes */
	 || (base >= 0xA0 && base <= 0xA7)   /* sync family */
	 || (base >= 0xB0 && base <= 0xB8)   /* thread-config family */
	 || base == 0x36 || base == 0x37     /* CLEARDVALID / SETRWC */
	 || base == 0x28 || base == 0x12;    /* ELWADD / MOVA2D (FPU) */
}

/* Classify a stored VALUE that reaches an instruction FIFO or a
   template instruction slot.  IN_SLOT selects the slot discipline.
   PHIs classify when every argument classifies (the runtime-selected-
   template shape: each branch's word must be audited).  */

static bool
classify_word_value (tree val, unsigned *claimed, const char **why,
		     rvtt_mop_derive_state *st, bool in_slot,
		     unsigned depth = 0, rvtt_mop_scan_ctx *ctx = nullptr)
{
  if (!val || depth > 4)
    {
      *why = "unclassifiable stored word";
      return false;
    }
  {
    /* Context-bound scan: a parameter value classifies as the driving
       call's actual argument, read under the caller's context.  */
    rvtt_mop_scan_ctx *c = ctx;
    tree bound = rvtt_mop_resolve_bound (val, &c);
    if (bound != val)
      return classify_word_value (bound, claimed, why, st, in_slot,
				  depth + 1, c);
  }
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	{
	  *why = "unclassifiable stored word";
	  return false;
	}
      return rvtt_mop_audited_word_p (TREE_INT_CST_LOW (val) & 0xffffffff,
				      claimed, why, st, in_slot);
    }
  if (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (gphi *phi = dyn_cast <gphi *> (def))
	{
	  for (unsigned i = 0; i != gimple_phi_num_args (phi); ++i)
	    if (!classify_word_value (gimple_phi_arg_def (phi, i), claimed,
				      why, st, in_slot, depth + 1, ctx))
	      return false;
	  return true;
	}
      if (is_gimple_assign (def)
	  && (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def))
	      || gimple_assign_rhs_code (def) == SSA_NAME))
	return classify_word_value (gimple_assign_rhs1 (def), claimed, why,
				    st, in_slot, depth + 1, ctx);
      /* A runtime-selected word: both arms must classify (the same
	 union rule as the PHI form above; at this pipeline position
	 the select may still be a COND_EXPR assignment).  */
      if (is_gimple_assign (def)
	  && gimple_assign_rhs_code (def) == COND_EXPR)
	return classify_word_value (gimple_assign_rhs2 (def), claimed, why,
				    st, in_slot, depth + 1, ctx)
	  && classify_word_value (gimple_assign_rhs3 (def), claimed, why,
				  st, in_slot, depth + 1, ctx);
      /* A word loaded back out of a scanned local aggregate's field:
	 admissible exactly when the subtree scan proves every store
	 to that (object, field) audited and the object's address never
	 escaped the scanned subtree.  This is flow-insensitive: which
	 store reached this load does not matter.  */
      if (is_gimple_assign (def)
	  && gimple_assign_rhs_class (def) == GIMPLE_SINGLE_RHS)
	{
	  tree var = NULL_TREE, field = NULL_TREE;
	  if (scanned_field_ref (gimple_assign_rhs1 (def), ctx,
				  &var, &field))
	    {
	      if (!ctx || !ctx->scan)
		{
		  *why = "aggregate-field word outside a scanned scan";
		  return false;
		}
	      if (ctx->scan->poisoned.contains (var))
		{
		  *why = "mop-template-field-unproven: the aggregate's "
			 "address escaped the scanned subtree";
		  return false;
		}
	      bool seen = false;
	      for (rvtt_mop_obj_scan::entry &e : ctx->scan->fields)
		if (e.var == var && e.field == field)
		  {
		    if (!e.ok)
		      {
			*why = "mop-template-field-unproven: an unaudited "
			       "store reaches this aggregate field";
			return false;
		      }
		    seen = true;
		  }
	      if (seen)
		return true;
	      *why = "mop-template-field-unproven: no scanned store "
		     "reaches this aggregate field";
	      return false;
	    }
	}
      int base = pushed_word_opcode_byte (val, 0, ctx);
      if (base < 0)
	{
	  *why = "unclassifiable composed word (no constant opcode base)";
	  return false;
	}
      if (field_insensitive_audited_class_p (base))
	return true;
      if (base == (int) XTT_MOP_OPCODE || base == (int) XTT_MOP_CFG_OPCODE)
	{
	  if (in_slot)
	    {
	      *why = "mop-template-nested-unproven: MOP word in a "
		     "template slot";
	      return false;
	    }
	  if (!st)
	    {
	      *why = "composed MOP word";
	      return false;
	    }
	  if (base == (int) XTT_MOP_OPCODE)
	    /* Runtime loop_count/zmask are expansion-count facts.  */
	    st->mop_pushed = true;
	  return true;
	}
      *why = "composed word of a field-sensitive or unaudited class";
      return false;
    }
  *why = "unclassifiable stored word";
  return false;
}

/* Record a template-slot audit failure (deferred to the MOP
   admission; claims from other slots still accumulate).  */

static void
record_slot_refusal (rvtt_mop_derive_state *st, unsigned slot,
		     const char *why, tree value)
{
  char buf[192];
  char *out = st->slots_refused ? buf : st->slot_reason;
  size_t outsz = st->slots_refused ? sizeof buf : sizeof st->slot_reason;
  st->slots_refused = true;
  /* Named slot-word refusals (replay/nested) already carry their own
     taxonomy prefix.  */
  const char *pfx = strncmp (why, "mop-template", 12) == 0
		    ? "" : "mop-template-word-unproven: ";
  if (value && TREE_CODE (value) == INTEGER_CST && tree_fits_uhwi_p (value))
    snprintf (out, outsz, "%s%s (template slot %u, word 0x%08x)",
	      pfx, why, slot,
	      (unsigned) (tree_to_uhwi (value) & 0xffffffff));
  else
    snprintf (out, outsz, "%s%s (template slot %u)", pfx, why, slot);
  if (dump_file)
    {
      fprintf (dump_file, "prgm-const: mop-derive: %s\n", out);
      if (value)
	{
	  fprintf (dump_file, "prgm-const: mop-derive: refusing value: ");
	  print_generic_expr (dump_file, value, TDF_NONE);
	  if (TREE_CODE (value) == SSA_NAME)
	    {
	      fprintf (dump_file, " def: ");
	      print_gimple_stmt (dump_file, SSA_NAME_DEF_STMT (value), 0,
				 TDF_NONE);
	    }
	  else
	    fprintf (dump_file, "\n");
	}
    }
}

/* ------------------------------------------------------------------ */
/* Non-constant store addresses: the FIFO-alias proof.		      */

/* The C-runtime data-image anchors the harness linker scripts define
   inside linker-allocated memory regions (XTT_LINK_IMAGE_DISJOINT,
   rvtt-mop-tables.h).  A store through a
   pointer derived from one of these stays inside the anchored object
   by the in-bounds object model and therefore cannot alias an
   instruction FIFO.  `__instrn_buffer' is not listed: it is the FIFO
   (XTT_INSTRN_BUF) and its stored word is classified.  */

static bool
crt0_data_anchor_p (const char *name)
{
  static const char *const anchors[] = {
    "__ldm_bss_start", "__ldm_bss_end",
    "__ldm_data_start", "__ldm_data_end",
    "__loader_init_start", "__loader_init_end",
    "__init_array_start", "__init_array_end",
    "__stack_top", "__global_pointer$",
    "__l1_data_start", "__l1_data_end",
    "__firmware_start",
  };
  for (const char *a : anchors)
    if (!strcmp (name, a))
      return true;
  return false;
}

/* DECL's assembler name when one is set, else its source-level
   DECL_NAME, else null -- the spelling the anchor and ABI-symbol
   string compares below match against.  */

static const char *
decl_asm_name (tree decl)
{
  if (!DECL_P (decl) || !HAS_DECL_ASSEMBLER_NAME_P (decl)
      || !DECL_ASSEMBLER_NAME_SET_P (decl))
    {
      if (DECL_P (decl) && DECL_NAME (decl))
	return IDENTIFIER_POINTER (DECL_NAME (decl));
      return nullptr;
    }
  return IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
}

/* Classification of a non-constant store address.  */

enum addr_class
{
  ADDR_DATA,		/* provably a link-image data object */
  ADDR_INSTRN_FIFO,	/* the __instrn_buffer ABI anchor */
  ADDR_UNKNOWN		/* neither provable: refuse */
};

/* Classify the store-address base DECL: the `__instrn_buffer' ABI
   anchor is the FIFO; a variable defined in this TU or a known
   C-runtime data-image anchor is link-image data (disjoint from the
   MMIO delivery ranges, XTT_LINK_IMAGE_DISJOINT); any other external
   refuses as unknown.  */

static addr_class
classify_decl_base (tree decl)
{
  if (!VAR_P (decl))
    return ADDR_UNKNOWN;
  const char *name = decl_asm_name (decl);
  if (rvtt_instrn_buffer_name_p (name))
    return ADDR_INSTRN_FIFO;
  if (!DECL_EXTERNAL (decl))
    /* Defined in this TU: the compiler allocates it in a linker-
       managed section, disjoint from the Tensix MMIO delivery ranges
       (XTT_LINK_IMAGE_DISJOINT).  */
    return ADDR_DATA;
  if (name && crt0_data_anchor_p (name))
    return ADDR_DATA;
  return ADDR_UNKNOWN;
}

static addr_class classify_pointer_base (tree ptr, hash_set<tree> &visiting,
					 unsigned depth = 0,
					 hash_set<tree> *parm_visiting
					   = nullptr);

/* Caller-closure join for a pointer PARAMETER: the parameter's targets
   are DATA if every call site in the TU passes a DATA-class argument.
   This is sound because the function is not address-taken, so every
   call is a direct cgraph edge, and under AXIOM kernel-single-TU no
   other translation unit can call it.  Recursion through a caller's
   own parameter is depth-limited and cycle-refused.  */

static bool
param_points_to_data_p (tree parm, hash_set<tree> &parm_visiting,
			unsigned depth)
{
  if (depth > 4 || parm_visiting.add (parm))
    return false;
  tree fndecl = DECL_CONTEXT (parm);
  if (!fndecl || TREE_CODE (fndecl) != FUNCTION_DECL)
    return false;
  cgraph_node *cn = cgraph_node::get (fndecl);
  if (!cn || !cn->definition || cn->address_taken || cn->alias
      || cn->thunk || cn->clones)
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
  /* No callers: the function body is unreachable and the store never
     executes -- vacuously safe.  */
  for (cgraph_edge *e = cn->callers; e; e = e->next_caller)
    {
      if (!e->call_stmt
	  || gimple_call_num_args (e->call_stmt) <= (unsigned) idx)
	return false;
      tree arg = gimple_call_arg (e->call_stmt, idx);
      hash_set<tree> visiting;
      if (classify_pointer_base (arg, visiting, depth + 1, &parm_visiting)
	  != ADDR_DATA)
	return false;
    }
  return true;
}

/* Walk a pointer's SSA derivation to its base anchors.  Every
   reachable leaf must classify identically; offsets (constant or
   variable) are absorbed by the in-bounds object model.  */

static addr_class
classify_pointer_base (tree ptr, hash_set<tree> &visiting, unsigned depth,
		       hash_set<tree> *parm_visiting)
{
  if (!ptr || depth > 16)
    return ADDR_UNKNOWN;
  STRIP_NOPS (ptr);
  if (TREE_CODE (ptr) == ADDR_EXPR)
    {
      tree base = get_base_address (TREE_OPERAND (ptr, 0));
      return base ? classify_decl_base (base) : ADDR_UNKNOWN;
    }
  if (TREE_CODE (ptr) != SSA_NAME)
    return ADDR_UNKNOWN;
  /* A pointer parameter: join over the TU call sites.  */
  if (SSA_NAME_IS_DEFAULT_DEF (ptr) && SSA_NAME_VAR (ptr)
      && TREE_CODE (SSA_NAME_VAR (ptr)) == PARM_DECL)
    {
      hash_set<tree> local_parms;
      hash_set<tree> &pv = parm_visiting ? *parm_visiting : local_parms;
      return param_points_to_data_p (SSA_NAME_VAR (ptr), pv, depth)
	     ? ADDR_DATA : ADDR_UNKNOWN;
    }
  if (visiting.add (ptr))
    /* Cycle member: classification comes from the other args.  */
    return ADDR_DATA;
  gimple *def = SSA_NAME_DEF_STMT (ptr);
  addr_class res = ADDR_UNKNOWN;
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      res = ADDR_DATA;
      bool any = false;
      for (unsigned i = 0; i != gimple_phi_num_args (phi); ++i)
	{
	  tree arg = gimple_phi_arg_def (phi, i);
	  if (arg == ptr)
	    continue;
	  addr_class c = classify_pointer_base (arg, visiting, depth + 1,
						 parm_visiting);
	  if (!any)
	    res = c, any = true;
	  else if (c != res)
	    res = ADDR_UNKNOWN;
	}
      if (!any)
	res = ADDR_UNKNOWN;
    }
  else if (is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	  || code == POINTER_PLUS_EXPR || code == PLUS_EXPR
	  || code == ADDR_EXPR)
	res = classify_pointer_base (gimple_assign_rhs1 (def), visiting,
				     depth + 1, parm_visiting);
    }
  visiting.remove (ptr);
  return res;
}

/* Bounded-IV store range proof: the store address is a two-arg PHI
   induction  P = PHI <INIT, P + K>  with INIT folding to constant C,
   positive constant step K, and the PHI's backedge guarded by the exit
   test of the stepped value against a bound folding to constant C2
   (`P+K != C2' with (C2-C) divisible by K, or `P+K < C2').  Then every
   executed store lies in [C, C2): monotonic ascent from C in steps of
   K cannot pass the guard once the bound is reached (for example a
   GPR-file zeroing fill, or a byte-copy loop over a constant
   aperture).  On success *LO/*HI receive the closed store
   byte range.  */

static bool
bounded_iv_store_range (gimple *store_stmt, tree lhs,
			unsigned HOST_WIDE_INT *lo,
			unsigned HOST_WIDE_INT *hi)
{
  poly_int64 bitsize, bitpos;
  tree offset;
  machine_mode mode;
  int unsignedp, reversep, volatilep = 0;
  tree base = get_inner_reference (lhs, &bitsize, &bitpos, &offset, &mode,
				   &unsignedp, &reversep, &volatilep);
  if (offset || !base || TREE_CODE (base) != MEM_REF)
    return false;
  tree moff = TREE_OPERAND (base, 1);
  HOST_WIDE_INT pos, size;
  if (TREE_CODE (moff) != INTEGER_CST || !tree_fits_shwi_p (moff)
      || !bitpos.is_constant (&pos) || (pos % BITS_PER_UNIT) != 0
      || !bitsize.is_constant (&size) || (size % BITS_PER_UNIT) != 0)
    return false;
  HOST_WIDE_INT extra = tree_to_shwi (moff) + pos / BITS_PER_UNIT;
  size /= BITS_PER_UNIT;

  /* Strip SSA casts/copies down to the PHI.  */
  tree ptr = TREE_OPERAND (base, 0);
  for (unsigned d = 0; d < 8 && TREE_CODE (ptr) == SSA_NAME; ++d)
    {
      gimple *def = SSA_NAME_DEF_STMT (ptr);
      if (!def || !is_gimple_assign (def))
	break;
      tree_code code = gimple_assign_rhs_code (def);
      if (!CONVERT_EXPR_CODE_P (code) && code != SSA_NAME)
	break;
      ptr = gimple_assign_rhs1 (def);
    }
  if (TREE_CODE (ptr) != SSA_NAME)
    return false;
  gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (ptr));
  if (!phi || gimple_phi_num_args (phi) != 2)
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: bounded-iv: no 2-arg phi\n");
      return false;
    }

  /* Identify the step arm: ARG = PTR + K.  */
  int step_ix = -1;
  unsigned HOST_WIDE_INT k = 0;
  tree stepped = NULL_TREE;
  for (int i = 0; i != 2; ++i)
    {
      tree arg = gimple_phi_arg_def (phi, i);
      if (TREE_CODE (arg) != SSA_NAME)
	continue;
      gimple *d = SSA_NAME_DEF_STMT (arg);
      if (!d || !is_gimple_assign (d))
	continue;
      tree_code code = gimple_assign_rhs_code (d);
      if ((code != POINTER_PLUS_EXPR && code != PLUS_EXPR)
	  || gimple_assign_rhs1 (d) != ptr
	  || TREE_CODE (gimple_assign_rhs2 (d)) != INTEGER_CST
	  || !tree_fits_shwi_p (gimple_assign_rhs2 (d))
	  || tree_to_shwi (gimple_assign_rhs2 (d)) <= 0)
	continue;
      step_ix = i;
      k = (unsigned HOST_WIDE_INT) tree_to_shwi (gimple_assign_rhs2 (d));
      stepped = arg;
      break;
    }
  if (step_ix < 0)
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: bounded-iv: no step arm\n");
      return false;
    }
  unsigned HOST_WIDE_INT c;
  if (!pointer_constant_address (gimple_phi_arg_def (phi, 1 - step_ix), &c))
    {
      if (dump_file)
	{
	  fprintf (dump_file, "prgm-const: bounded-iv: init not const: ");
	  print_generic_expr (dump_file, gimple_phi_arg_def (phi, 1 - step_ix));
	  fprintf (dump_file, "\n");
	}
      return false;
    }

  /* Two guard forms bound the ascent (the value tested is TESTV, and
     the guarded traversal is the edge GE whose taking implies the
     test passed):
       A. rotated loop: the backedge's source block ends in a cond on
	  the stepped value -- taking the backedge implies the test
	  passed, so the next iteration's PHI value is bounded;
       B. unrotated loop: the PHI's own block ends in a cond on the
	  PHI value and the store's block is that cond's guarded
	  immediate successor -- executing the store implies this
	  iteration's value passed the test.  (SSA definition-
	  dominates-use already places the header before the store;
	  requiring the store in the guarded successor closes the
	  remaining path question without dominance info, which is
	  unavailable for a non-cfun body.)  */
  edge be = gimple_phi_arg_edge (phi, step_ix);
  gcond *cond = nullptr;
  tree testv = NULL_TREE;
  edge ge = nullptr;
  bool next_iter_bound = false;	/* form A: bound holds at the NEXT PHI */
  gimple_stmt_iterator gsi = gsi_last_bb (be->src);
  if (!gsi_end_p (gsi))
    if (gcond *c = dyn_cast <gcond *> (gsi_stmt (gsi)))
      {
	cond = c;
	testv = stepped;
	ge = be;
	next_iter_bound = true;
      }
  if (!cond)
    {
      basic_block phibb = gimple_bb (phi);
      basic_block storebb = gimple_bb (store_stmt);
      gimple_stmt_iterator hgsi = gsi_last_bb (phibb);
      if (!gsi_end_p (hgsi))
	if (gcond *c = dyn_cast <gcond *> (gsi_stmt (hgsi)))
	  {
	    if (EDGE_COUNT (phibb->succs) != 2)
	      return false;
	    edge e0 = EDGE_SUCC (phibb, 0), e1 = EDGE_SUCC (phibb, 1);
	    if (e0->dest == e1->dest)
	      return false;
	    edge to_store = e0->dest == storebb ? e0
			    : e1->dest == storebb ? e1 : nullptr;
	    if (to_store)
	      {
		cond = c;
		testv = ptr;
		ge = to_store;
	      }
	  }
    }
  if (!cond)
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: bounded-iv: no cond\n");
      return false;
    }
  tree op0 = gimple_cond_lhs (cond), op1 = gimple_cond_rhs (cond);
  tree_code ccode = gimple_cond_code (cond);
  if (op1 == testv)
    {
      std::swap (op0, op1);
      ccode = swap_tree_comparison (ccode);
    }
  if (op0 != testv)
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: bounded-iv: cond not on step\n");
      return false;
    }
  bool taken_true = (ge->flags & EDGE_TRUE_VALUE) != 0;
  if (!taken_true)
    ccode = invert_tree_comparison (ccode, /*honor_nans=*/false);
  unsigned HOST_WIDE_INT c2;
  if (!pointer_constant_address (op1, &c2) || c2 <= c)
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: bounded-iv: bound not const\n");
      return false;
    }
  unsigned HOST_WIDE_INT last;
  if (ccode == NE_EXPR)
    {
      /* Monotonic +K from C exits exactly at C2: require
	 divisibility so equality cannot be stepped over.  */
      if ((c2 - c) % k != 0)
	return false;
      last = c2 - k;
    }
  else if (ccode == LT_EXPR)
    last = c2 - 1;
  else
    return false;
  *lo = c + extra;
  *hi = last + extra + size;	/* inclusive-end byte bound */
  return true;
}

/* The whole byte range [LO, HI] must avoid every MMIO block with
   instruction-delivery or unproven semantics (XTT_MMIO_STORE_INERT:
   everything else is inert for this proof).  */

static bool
store_range_inert_p (unsigned HOST_WIDE_INT lo, unsigned HOST_WIDE_INT hi)
{
  auto overlaps = [&] (unsigned HOST_WIDE_INT b, unsigned HOST_WIDE_INT l)
    { return lo <= l && hi >= b; };
  return !overlaps (XTT_MOP_CFG_MMIO_BASE, XTT_MOP_CFG_MMIO_LIMIT)
	 && !overlaps (XTT_INSTRN_BUF_MMIO_BASE, XTT_INSTRN_BUF_MMIO_LIMIT)
	 && !overlaps (XTT_PC_BUF_MMIO_BASE, XTT_PC_BUF_MMIO_LIMIT)
	 && !overlaps (XTT_DEBUG_REGS_MMIO_BASE, XTT_DEBUG_REGS_MMIO_LIMIT);
}

/* Classify a store of VALUE to the proven-constant address ADDR.  */

static bool
classify_constant_target (unsigned HOST_WIDE_INT addr, tree value,
			  unsigned *claimed, const char **why,
			  rvtt_mop_derive_state *st,
			  rvtt_mop_scan_ctx *ctx = nullptr)
{
  /* MOP template file: slot rules.  */
  if (addr >= XTT_MOP_CFG_MMIO_BASE && addr <= XTT_MOP_CFG_MMIO_LIMIT)
    {
      if (addr >= XTT_MOP_CFG_MMIO_BASE + 4 * XTT_MOP_CFG_SLOTS
	  || (addr - XTT_MOP_CFG_MMIO_BASE) % 4 != 0)
	{
	  /* Beyond the nine architected words (or misaligned): no
	     recorded fact.  */
	  *why = "mop-template-slot-range-unproven";
	  return false;
	}
      unsigned slot = (addr - XTT_MOP_CFG_MMIO_BASE) / 4;
      if (slot <= 1)
	/* Loop lengths (type 1) / flags (type 0): never expanded
	   as instruction words; any value admissible
	   (rvtt-mop-tables.h union taxonomy).  */
	return true;
      const char *slot_why = nullptr;
      if (!classify_word_value (value, claimed, &slot_why, st,
				/*in_slot=*/true, 0, ctx))
	record_slot_refusal (st, slot, slot_why, value);
      return true;		/* deferred to the MOP admission */
    }
  /* Instruction-FIFO aperture: the stored word classifies.  */
  if (addr >= XTT_INSTRN_BUF_MMIO_BASE && addr <= XTT_INSTRN_BUF_MMIO_LIMIT)
    {
      if (addr != XTT_INSTRN_BUF_MMIO_BASE)
	{
	  /* Only word 0 is the architected aperture (the simulator's
	     tensix_inst_wr32 checks offset == 0).  */
	  *why = "instruction-buffer offset unproven";
	  return false;
	}
      return classify_word_value (value, claimed, why, st,
				  /*in_slot=*/false, 0, ctx);
    }
  /* PC_BUF: the sync and semaphore words are inert for
     PRGM/LaneConfig/CC and deliver no instruction (TENSIX_SYNC,
     MOP_SYNC and SEMAPHORE posts; no other offset is architected).  */
  if (addr >= XTT_PC_BUF_MMIO_BASE && addr <= XTT_PC_BUF_MMIO_LIMIT)
    {
      unsigned off = addr - XTT_PC_BUF_MMIO_BASE;
      if (off == XTT_PC_BUF_TENSIX_SYNC_OFFSET
	  || off == XTT_PC_BUF_MOP_SYNC_OFFSET
	  || (off >= XTT_PC_BUF_SEMAPHORE_OFFSET
	      && off < XTT_PC_BUF_SEMAPHORE_OFFSET + 4 * 8
	      && off % 4 == 0))
	return true;
      *why = "pc-buf-write-unproven";
      return false;
    }
  /* The debug-register block documents an instruction-injection
     interface on the device (ckernel_debug.h); no recorded fact pins
     which offsets are inert.  */
  if (addr >= XTT_DEBUG_REGS_MMIO_BASE && addr <= XTT_DEBUG_REGS_MMIO_LIMIT)
    {
      *why = "debug-regs-write-unproven";
      return false;
    }
  /* Any other constant address is inert for this proof: no other
     MMIO store reaches instruction delivery or SFPU state
     (XTT_MMIO_STORE_INERT in rvtt-mop-tables.h).  */
  return true;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Store classification entry point.				      */

bool
rvtt_mop_derive_store (gimple *stmt, unsigned *claimed, const char **why,
		       rvtt_mop_derive_state *st, rvtt_mop_scan_ctx *ctx)
{
  if (!is_gimple_assign (stmt) || !gimple_store_p (stmt))
    return true;
  /* An end-of-life clobber marks storage death; it stores nothing and
     in particular is not an unaudited write for the field scan.  */
  if (gimple_clobber_p (stmt))
    return true;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return true;

  /* Scan direct stores to foldable global pointers (the verify half
     of the assume+verify global-value derivation).  */
  if (DECL_P (lhs))
    scan_global_pointer_store (lhs, gimple_assign_rhs1 (stmt));

  /* Context-bound scan: record, for each store into a field of an
     automatic local aggregate (rvtt-mop-derive.h), whether its value
     classifies through the audited word table, so a later load from
     the same field can be admitted when every store to it is audited.
     Storing the address of such an aggregate lets it escape and
     poisons it.  */
  if (ctx && ctx->scan)
    {
      tree var = NULL_TREE, field = NULL_TREE;
      if (scanned_field_ref (lhs, ctx, &var, &field))
	{
	  const char *fwhy = nullptr;
	  bool ok = classify_word_value (gimple_assign_rhs1 (stmt), claimed,
					 &fwhy, st, /*in_slot=*/true, 0, ctx);
	  ctx->scan->fields.safe_push
	    (rvtt_mop_obj_scan::entry { var, field, ok });
	}
      else if (tree evar = bound_local_object (gimple_assign_rhs1 (stmt),
					       ctx))
	rvtt_mop_scan_poison (ctx, evar);
      /* A whole-aggregate store: zero-initialization stores the
	 audited all-zero word to every field; anything else poisons
	 (refusing default).  */
      tree base = get_base_address (lhs);
      if (lhs == base && VAR_P (base) && !TREE_STATIC (base)
	  && !DECL_EXTERNAL (base)
	  && AGGREGATE_TYPE_P (TREE_TYPE (base)))
	{
	  tree rhs = gimple_assign_rhs1 (stmt);
	  if (!(TREE_CODE (rhs) == CONSTRUCTOR && initializer_zerop (rhs)))
	    rvtt_mop_scan_poison (ctx, base);
	}
    }

  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    return classify_constant_target (addr, gimple_assign_rhs1 (stmt),
				     claimed, why, st, ctx);

  /* Non-constant address.  A store into a known non-volatile object is
     memory, not MMIO (hardware registers are declared volatile).  */
  tree base = get_base_address (lhs);
  if (!TREE_THIS_VOLATILE (lhs)
      && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
    return true;

  /* Volatile store, unresolved address: prove the FIFO-alias question
     from the address derivation.  */
  addr_class cls = ADDR_UNKNOWN;
  if (base && DECL_P (base))
    cls = classify_decl_base (base);
  else if (base && TREE_CODE (base) == MEM_REF)
    {
      hash_set<tree> visiting;
      cls = classify_pointer_base (TREE_OPERAND (base, 0), visiting);
    }
  if (cls == ADDR_DATA)
    return true;
  if (cls == ADDR_INSTRN_FIFO)
    return classify_word_value (gimple_assign_rhs1 (stmt), claimed, why, st,
				/*in_slot=*/false, 0, ctx);
  /* Last resort: a bounded induction over a constant-based range whose
     whole extent is inert (the GPR-file fill class).  */
  unsigned HOST_WIDE_INT lo, hi;
  if (bounded_iv_store_range (stmt, lhs, &lo, &hi)
      && store_range_inert_p (lo, hi))
    return true;
  static char alias_buf[192];
  const char *name = nullptr;
  if (base && DECL_P (base))
    name = decl_asm_name (base);
  snprintf (alias_buf, sizeof alias_buf,
	    "mop-store-alias-unproven: volatile store%s%s cannot be proven "
	    "outside the instruction FIFOs",
	    name ? " through " : "", name ? name : "");
  *why = alias_buf;
  return false;
}

/* ------------------------------------------------------------------ */
/* The blocking-store asm idiom.				      */

/* Resolve one asm operand reference (`%N' digits or `%[name]') to the
   tree whose VALUE reaches the instruction: for an input, its value;
   for an output with a matching-digit input constraint (the `+r'
   split), the matching input's value.  Returns NULL_TREE when the
   value is unresolvable.  */

static tree
asm_operand_value (const gasm *stmt, const char *ref, size_t len)
{
  unsigned nout = gimple_asm_noutputs (stmt);
  unsigned nin = gimple_asm_ninputs (stmt);
  int idx = -1;
  if (len >= 3 && ref[0] == '[' && ref[len - 1] == ']')
    {
      for (unsigned i = 0; i != nout + nin && idx < 0; ++i)
	{
	  tree op = i < nout ? gimple_asm_output_op (stmt, i)
			     : gimple_asm_input_op (stmt, i - nout);
	  tree name = TREE_PURPOSE (TREE_PURPOSE (op));
	  if (name && TREE_CODE (name) == IDENTIFIER_NODE
	      && IDENTIFIER_LENGTH (name) == len - 2
	      && strncmp (IDENTIFIER_POINTER (name), ref + 1, len - 2) == 0)
	    idx = (int) i;
	}
    }
  else
    {
      idx = 0;
      for (size_t i = 0; i != len; ++i)
	{
	  if (!ISDIGIT (ref[i]))
	    return NULL_TREE;
	  idx = idx * 10 + (ref[i] - '0');
	}
    }
  if (idx < 0)
    return NULL_TREE;
  if ((unsigned) idx < nout)
    {
      /* Output operand: its inbound value is the input with the
	 matching numeric constraint, if any.  */
      for (unsigned j = 0; j != nin; ++j)
	{
	  tree in = gimple_asm_input_op (stmt, j);
	  tree cst = TREE_VALUE (TREE_PURPOSE (in));
	  if (cst && TREE_CODE (cst) == STRING_CST
	      && ISDIGIT (TREE_STRING_POINTER (cst)[0])
	      && atoi (TREE_STRING_POINTER (cst)) == idx)
	    return TREE_VALUE (in);
	}
      return NULL_TREE;
    }
  if ((unsigned) idx < nout + nin)
    return TREE_VALUE (gimple_asm_input_op (stmt, idx - nout));
  return NULL_TREE;
}

/* The scalar blocking-store asm idiom (store, reload, consume: the
   pcbuf/mailbox handshake of ckernel.h store_blocking), in its
   positional and named-operand spellings.  It stores its value operand
   at its address operand, so it must be classified like any other
   store; otherwise a raw instruction word could reach a FIFO
   unaudited.  */

bool
rvtt_mop_blocking_store_asm_p (const gasm *stmt)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  return !strcmp (s, "sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0")
	 || !strcmp (s, "sw %[raw], (%[ptr])\n\t"
			"lw %[raw], (%[ptr])\n\t"
			"and x0, x0, %[raw]");
}

/* Classify the blocking-store asm STMT (both the positional and the
   named-operand spelling) exactly like an ordinary store: resolve its
   address operand and route a constant address to the constant-target
   classifier, a proven data-object base to admission, and a proven
   FIFO base to word classification of its value operand.  Refuses
   (false, *WHY) when the address is unresolvable or its FIFO
   disjointness is unproven.  CLAIMED and ST are the shared derive
   outputs of the word classifiers.  */

bool
rvtt_mop_derive_asm_store (const gasm *stmt, unsigned *claimed,
			   const char **why, rvtt_mop_derive_state *st)
{
  const char *s = gimple_asm_string (stmt);
  bool named = strchr (s, '[') != nullptr;
  tree value = named ? asm_operand_value (stmt, "[raw]", 5)
		     : asm_operand_value (stmt, "0", 1);
  tree addr = named ? asm_operand_value (stmt, "[ptr]", 5)
		    : asm_operand_value (stmt, "1", 1);
  if (!addr)
    {
      *why = "blocking-store address unresolvable";
      return false;
    }
  unsigned HOST_WIDE_INT a;
  if (pointer_constant_address (addr, &a))
    return classify_constant_target (a, value, claimed, why, st);
  hash_set<tree> visiting;
  switch (classify_pointer_base (addr, visiting))
    {
    case ADDR_DATA:
      return true;
    case ADDR_INSTRN_FIFO:
      return classify_word_value (value, claimed, why, st,
				  /*in_slot=*/false);
    default:
      *why = "mop-store-alias-unproven: blocking-store address cannot be "
	     "proven outside the instruction FIFOs";
      return false;
    }
}

/* ------------------------------------------------------------------ */
/* The crt0 init-array indirect call.				      */

/* Leaf test for the loaded-from pointer: derives only from
   __init_array_start.  */

static bool
init_array_pointer_p (tree ptr, hash_set<tree> &visiting, unsigned depth = 0)
{
  if (!ptr || depth > 16)
    return false;
  STRIP_NOPS (ptr);
  if (TREE_CODE (ptr) == ADDR_EXPR)
    {
      tree base = get_base_address (TREE_OPERAND (ptr, 0));
      if (!base || !VAR_P (base))
	return false;
      const char *name = decl_asm_name (base);
      return name && !strcmp (name, "__init_array_start");
    }
  if (TREE_CODE (ptr) != SSA_NAME)
    return false;
  if (visiting.add (ptr))
    return true;		/* cycle member: judged by other args */
  gimple *def = SSA_NAME_DEF_STMT (ptr);
  bool ok = false;
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      ok = false;
      bool any = false;
      for (unsigned i = 0; i != gimple_phi_num_args (phi); ++i)
	{
	  tree arg = gimple_phi_arg_def (phi, i);
	  if (arg == ptr)
	    continue;
	  if (!init_array_pointer_p (arg, visiting, depth + 1))
	    {
	      any = true;
	      ok = false;
	      break;
	    }
	  any = ok = true;
	}
      if (!any)
	ok = false;
    }
  else if (is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	  || code == POINTER_PLUS_EXPR || code == PLUS_EXPR
	  || code == ADDR_EXPR)
	ok = init_array_pointer_p (gimple_assign_rhs1 (def), visiting,
				   depth + 1);
    }
  visiting.remove (ptr);
  return ok;
}

/* This TU's .init_array contribution is exactly its registered static
   constructors when no declaration extends the section by attribute
   and no toplevel asm exists.  */

static bool
tu_init_array_extended_p ()
{
  /* Toplevel asm can emit arbitrary section content (including
     .init_array entries) invisible to the cgraph.  */
  if (symtab->first_asm_symbol ())
    return true;
  auto section_extends = [] (tree decl) -> bool
    {
      if (!DECL_SECTION_NAME (decl))
	return false;
      const char *s = DECL_SECTION_NAME (decl);
      return strstr (s, ".init_array") || strstr (s, ".preinit_array")
	     || strstr (s, ".ctors");
    };
  varpool_node *vnode;
  FOR_EACH_VARIABLE (vnode)
    if (section_extends (vnode->decl))
      return true;
  cgraph_node *cnode;
  FOR_EACH_FUNCTION (cnode)
    if (section_extends (cnode->decl))
      return true;
  return false;
}

/* Prove that the indirect CALL is the C-runtime init-array walk
   calling only this TU's own registered static constructors: the
   callee must be loaded through a pointer anchored at
   __init_array_start (MEM_REF or post-ivopts TARGET_MEM_REF form),
   and nothing may extend the section beyond the registered
   constructors (tu_init_array_extended_p).  Dumps the failing
   obligation and returns false otherwise.  */

bool
rvtt_mop_init_array_call_p (gcall *call)
{
  tree fn = gimple_call_fn (call);
  if (!fn || TREE_CODE (fn) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (fn);
  if (!def || !is_gimple_assign (def) || !gimple_assign_load_p (def))
    {
      if (dump_file)
	fprintf (dump_file, "prgm-const: init-array: callee not a load\n");
      return false;
    }
  tree rhs = gimple_assign_rhs1 (def);
  bool anchored = false;
  if (TREE_CODE (rhs) == MEM_REF)
    {
      hash_set<tree> visiting;
      anchored = init_array_pointer_p (TREE_OPERAND (rhs, 0), visiting);
    }
  else if (TREE_CODE (rhs) == TARGET_MEM_REF)
    {
      /* addr = BASE + INDEX*STEP + INDEX2 + OFFSET (the post-ivopts
	 form).  By pointer provenance an in-bounds access to the
	 init-array object carries its anchor in SOME address operand;
	 the remaining operands are offsets the object model
	 absorbs.  */
      tree ops[3] = { TMR_BASE (rhs), TMR_INDEX (rhs), TMR_INDEX2 (rhs) };
      for (tree op : ops)
	if (op && TREE_CODE (op) != INTEGER_CST)
	  {
	    hash_set<tree> visiting;
	    if (init_array_pointer_p (op, visiting))
	      {
		anchored = true;
		break;
	      }
	  }
    }
  if (!anchored)
    {
      if (dump_file)
	fprintf (dump_file,
		 "prgm-const: init-array: pointer derivation unproven\n");
      return false;
    }
  /* The called values are in-bounds init-array entries (C++ object
     model on the linker-defined array).  Under AXIOM kernel-single-TU
     those entries are exactly this TU's registered static
     constructors -- every one a scanned definition of the TU walk --
     unless something extends the section.  */
  if (tu_init_array_extended_p ())
    {
      if (dump_file)
	fprintf (dump_file,
		 "prgm-const: init-array: section extended beyond the "
		 "TU's registered constructors\n");
      return false;
    }
  return true;
}

/* ------------------------------------------------------------------ */

bool
rvtt_mop_derive_finish (const rvtt_mop_derive_state *st, const char **why)
{
  /* Verify every assumed global-pointer value against the complete TU
     store scan (the scan visited every store in every walkable
     body, and the shape conditions exclude invisible writers).  */
  if (assumed_globals)
    for (tree decl : *assumed_globals)
      {
	unsigned HOST_WIDE_INT init;
	if (!foldable_global_pointer_p (decl, &init))
	  {
	    *why = "global-pointer-value-unproven";
	    return false;
	  }
	global_ptr_scan *c
	  = global_ptr_stores ? global_ptr_stores->get (decl) : nullptr;
	if (c && (c->unknown || c->value != init))
	  {
	    static char gbuf[192];
	    const char *name = DECL_NAME (decl)
	      ? IDENTIFIER_POINTER (DECL_NAME (decl)) : "?";
	    snprintf (gbuf, sizeof gbuf,
		      "global-pointer-value-unproven: %s is stored a value "
		      "other than its initializer", name);
	    if (dump_file)
	      fprintf (dump_file, "prgm-const: mop-derive: %s\n", gbuf);
	    *why = gbuf;
	    return false;
	  }
      }
  if (st->mop_pushed && st->slots_refused)
    {
      *why = st->slot_reason;
      return false;
    }
  return true;
}

/* Parse STMT as the canonical raw `.ttinsn %0' asm with exactly one
   constant input and no outputs; on success set *WORD to the 32-bit
   instruction word and return true.  Any other spelling or a
   non-constant operand returns false.  */

bool
rvtt_raw_ttinsn_word_p (gasm *stmt, uint32_t *word)
{
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strncmp (s, ".ttinsn", 7) != 0)
    return false;
  s += 7;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strcmp (s, "%0") != 0
      || gimple_asm_ninputs (stmt) != 1
      || gimple_asm_noutputs (stmt) != 0)
    return false;
  tree op = TREE_VALUE (gimple_asm_input_op (stmt, 0));
  if (TREE_CODE (op) != INTEGER_CST)
    return false;
  *word = (uint32_t) TREE_INT_CST_LOW (op);
  return true;
}
