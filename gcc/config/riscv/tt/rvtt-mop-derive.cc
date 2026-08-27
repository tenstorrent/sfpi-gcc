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

/* The prgm-const pass (gimple-rvtt-prgm-const.cc) allocates PRGM
   constant registers only under a TU-wide freedom proof: nothing in
   the TU may write a PRGM register, LaneConfig, or the CC state
   unaudited.  Raw `.ttinsn' words decode through the audited table;
   the one word class the table cannot decode by itself is MOP
   (frontend opcode 0x01): it expands the instruction words previously
   programmed into the nine MOP template registers, so its effects
   live in OTHER stores, not in the word.  This file derives those
   effects from the TU's own template-programming writes and decodes
   them through the same audited table -- the compiler PROVES the MOP
   run's effects; it is never told them by a source annotation (the
   former TRUSTED ttregion markers are retired by ruling, 2026-08-18).

   The derivation is flow-insensitive, matching the proof's shape: the
   freedom proof does not need to know WHICH template a given MOP run
   expands -- only that NO template word programmable in this TU can
   touch owned state unaudited.  Union slot taxonomy over both MOP
   types (rvtt-mop-tables.h): slots 0..1 are loop lengths (type 1) /
   flags (type 0), never expanded as instruction words, so any value is
   admissible; slots 2..8 are instruction slots under at least one
   type, so every value stored there must decode through the audited
   table.  A MOP that runs with NO in-TU programming expands the
   thread-entry template state, which is audited by the reset-template
   fact (rvtt-mop-tables.h).  In-TU races reorder WHICH audited words
   run, never whether they are audited.

   Closing the volatile-store blind spot: the gimple raw census used to
   classify only `.ttinsn' asm and calls -- a volatile store of a raw
   instruction word (`__instrn_buffer[0] = word', `mop_cfg[i] = word')
   evaded the TU proof entirely.  This file makes stores first-class
   scan objects:

     - constant-address stores classify by target range
       ([SIM t_tile_mmio_wr32] decoder, facts in rvtt-mop-tables.h):
       the MOP template file (slot rules above), the instruction-FIFO
       aperture (push: the stored word classifies through the audited
       table), the PC_BUF sync/semaphore words (inert), the
       RISCV_DEBUG_REGS block (refuses: silicon documents an
       instruction-injection interface there), anything else (inert
       for this proof: no other MMIO-write arm reaches instruction
       delivery or SFPU state);
     - non-constant-address volatile stores must prove they cannot
       alias an instruction FIFO: an address derived from a
       link-image object (a TU-defined variable, or one of the
       C-runtime section anchors the harness linker scripts place in
       the data image) cannot, by the in-bounds object model plus the
       link-image extents ([PROD] fact in rvtt-mop-tables.h); the
       `__instrn_buffer' ABI anchor IS the FIFO and classifies its
       stored word; any other externally-anchored or unresolvable
       address refuses by name.

   Runtime-composed words classify by the constant opcode base of
   their PLUS/BIT_IOR composition (AXIOM tt-op-field-discipline,
   rvtt-mop-tables.h): admissible only for opcode classes the audited
   table admits for EVERY field value -- a composed word whose audited
   class is field-sensitive (SFPLOADI's destination, SFPCONFIG's
   destination) could hide the sensitive field in its runtime operand
   and refuses.

   The crt0 init-array indirect call: the C runtime's `_start' walks
   [__init_array_start, __init_array_end) calling each constructor
   indirectly, and an indirect call normally refuses the TU (unknown
   body).  rvtt_mop_init_array_call_p discharges exactly this shape
   structurally:

     (a) the called value is loaded through a pointer that derives
	 ONLY from `__init_array_start' (SSA casts, pointer
	 arithmetic, PHIs) -- by the C++ object model, in-bounds
	 pointer arithmetic on the linker-defined init-array object
	 yields only init-array entries;
     (b) this TU's contribution to .init_array is exactly its
	 registered static constructors (DECL_STATIC_CONSTRUCTOR
	 bodies -- all scanned by the TU walk like any definition),
	 PROVIDED no declaration places extra data there by section
	 attribute and no toplevel asm exists (both checked; either
	 refuses);
     (c) no other object contributes entries: AXIOM kernel-single-TU
	 (one translation unit per TRISC image, the harness build
	 convention -- the same axiom the mop-form outward-ownership
	 proof already rests on, rtl-rvtt-mop-form.cc file header).

   Refusal taxonomy (all TU-wide, byte-identical):
     mop-template-word-unproven	   a slot-2..8 write whose word the
				   audited table cannot pin (reported
				   at the deferred MOP admission);
     mop-template-replay-unproven  a REPLAY word in a slot (the
				   recorded-content audit is a later
				   increment);
     mop-template-nested-unproven  a MOP/MOP_CFG word in a slot (the
				   expander re-dispatch is unproven);
     mop-store-alias-unproven	   a volatile store whose address can
				   be proven neither inside nor
				   outside the instruction FIFOs;
     plus the existing `unaudited raw opcode' for direct words.

   The address-folding and opcode-base helpers mirror the proven
   classifiers of rtl-rvtt-mop-form.cc (mop_pointer_constant_address /
   mop_ref_constant_address / mop_pushed_word_base); that pass owns
   the outward-ownership FORMING direction and stays untouched.  */

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
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"

/* ------------------------------------------------------------------ */
/* The audited raw-word capability table.			      */

bool
rvtt_mop_audited_word_p (uint32_t word, unsigned *claimed, const char **why,
			 rvtt_mop_derive_state *st, bool in_slot)
{
  unsigned opcode = word >> 24;
  if (opcode == XTT_MOP_OPCODE || opcode == XTT_MOP_CFG_OPCODE)
    {
      if (in_slot)
	{
	  /* A MOP/MOP_CFG word inside a template slot re-enters the
	     expander from inside an expansion; no recorded fact pins
	     that behavior.  */
	  *why = "mop-template-nested-unproven: MOP word in a template slot";
	  return false;
	}
      if (!st)
	{
	  *why = "unaudited raw opcode";
	  return false;
	}
      if (opcode == XTT_MOP_OPCODE)
	/* Effects live in the template slots; admission is deferred to
	   rvtt_mop_derive_finish once every TU slot write is audited.
	   The word's own fields (mop_type/loop_count/zmask) are
	   expansion-count facts, not effect facts
	   (rvtt-mop-tables.h).  */
	st->mop_pushed = true;
      /* MOP_CFG writes only the persistent zmask high half -- a
	 type-0 iteration-count fact ([SIM] mop_cfg(),
	 rvtt-mop-tables.h).  Unconditionally inert.  */
      return true;
    }
  if (in_slot && opcode == XTT_REPLAY_OPCODE)
    {
      /* The slot word would play back recorded replay-buffer content;
	 auditing recorded content is a later increment.  */
      *why = "mop-template-replay-unproven: REPLAY word in a template slot";
      return false;
    }
  if (opcode == 0x00)		/* TENSIX NOP */
    return true;
  if (opcode == 0x02)
    /* Tensix NOP (0x02 << 24): classified by opcode alone and
       swallowed at the instruction FIFO -- delivers nothing.  [SIM]
       tensix.cpp IS_TENSIX_NOP (bits<31,24> == 0x02) and
       tensix_push_inst_fifo's NOP early-return; rvtt-mop-tables.h NOP
       fact.  The production template constructors park unused slots
       on exactly this word (ckernel_template ctor TT_OP_NOP).  */
    return true;
  if (opcode >= 0xA0 && opcode <= 0xA7)	/* sync family */
    return true;
  if (opcode >= 0xB0 && opcode <= 0xB8)	/* thread-config family (SETC16) */
    return true;
  if (opcode == 0x36 || opcode == 0x37)	/* CLEARDVALID / SETRWC */
    return true;
  if (opcode == 0x28)
    /* ELWADD: FPU elementwise add, the production math datacopy's MOP
       loop word (llk_math_eltwise_unary_datacopy.h).  Consumes
       SrcA/SrcB banks, writes Dst rows, steps the RWC per its
       addr_mode field, and optionally clears dvalid -- matrix-unit
       state only, for EVERY field value.  [SPEC]
       craq-sim tests/aristotle/mega-union/specs/ELWADD.md functional
       model (no LReg/LaneConfig/lane-flag reference); [SIM]
       tensix.cpp TENSIX_EXECUTE_ELWADD -> tensix_execute_elw_op @
       9f324140: reads src banks, writes dst[], RWC/dvalid bookkeeping;
       l_regs/lane_config untouched.  */
    return true;
  if (opcode == 0x12)
    /* MOVA2D: SrcA-to-Dst row move, the other production math
       datacopy MOP loop word.  Reads SrcA and (read-only) the
       LaneConfig BLOCK_DEST_MOV gate; writes Dst rows only, for EVERY
       field value (the one flagged field combination, TF32 source
       with the Dst32bLo modifier, is UndefinedBehavior confined to
       the WRITTEN DST DATA -- "write data will be corrupted").
       [SPEC] specs/MOVA2D.md functional model (LaneConfig read at the
       column gate; all writes are Dst32b/Dst16b rows); [SIM]
       tensix.cpp TENSIX_EXECUTE_MOVA2D @ 9f324140: dst[] writes only,
       l_regs/lane_config untouched.  */
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
	  /* LaneConfig default-reset class: dest 15, mod1 bit0
	     (MOD1_IMM16_IS_VALUE) set, imm16 == 0 -- the SFPU init's
	     TTI_SFPCONFIG (0, 0xF, 1), word 0x910000F1.

	     Audited by the architectural spec (SFPCONFIG.md functional
	     model) and the corrected simulator (craq tensix.cpp
	     TENSIX_EXECUTE_SFPCONFIG, craq 9f324140): the VD == 15 arm
	     assigns LaneConfig only; LReg[11..14] writes exist solely in
	     the VD 11..14 arm, so the programmable constant registers
	     SURVIVE this word.  Within the admitted class every mod1
	     completion is still LaneConfig-confined: set/AND with value
	     0 is the hardware default-reset (reserved high bits
	     restored per spec), OR/XOR with 0 is a no-op, and
	     IMM16_IS_LANE_MASK with imm16 == 0 masks every lane off.
	     The resulting LaneConfig is always {unchanged, default}, and
	     default (0) is exactly the all-lanes, no-ROW_MASK state the
	     allocator's own SFPCONFIG programming write assumes.  No
	     destination is claimed: the word touches no PRGM register.

	     Near misses stay refused by class: imm16 != 0 can set
	     ROW_MASK/behavior bits (unproven lane model); mod1 bit0 == 0
	     takes the value from LReg[0] (unauditable from the word).  */
	  if ((word & 1) == 1 && ((word >> 8) & 0xffff) == 0)
	    return true;
	  *why = "raw SFPCONFIG writes LaneConfig";
	  return false;
	}
      *claimed |= 1u << dest;
      return true;
    }
  *why = "unaudited raw opcode";
  return false;
}

namespace {

static bool pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
				      unsigned depth);

/* ------------------------------------------------------------------ */
/* TU global-pointer value derivation (assume during the scan, verify
   at the finish adjudication).

   The production LLK names its hardware apertures through TU-defined
   global pointer variables with constant initializers
   (ckernel_helper.h: pc_buf_base/regfile/...; the profiler's
   barrier/epoch pointers).  A load of such a global folds to its
   initializer constant PROVIDED the TU never stores a different value
   into it.  The scan cannot know that yet (stores are discovered as
   bodies are walked), so a load ASSUMES the initializer value and
   records the assumption; every direct store to a candidate global is
   censused by rvtt_mop_derive_store as the walk proceeds; and
   rvtt_mop_derive_finish verifies each assumed global's census
   (no store, or every store folds to the same constant) -- refusing
   the TU by name otherwise.  Shape conditions make the census
   complete: the global is TU-defined, non-addressable (no indirect
   stores), and every admitted asm template is store-transparent, so
   the walk sees every possible writer.  */

struct global_ptr_census
{
  bool stored = false;		/* some direct store exists */
  bool unknown = false;		/* a store didn't fold / disagreed */
  unsigned HOST_WIDE_INT value = 0;
};

static hash_map<tree, global_ptr_census> *global_ptr_stores;
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

/* Census one direct store to a candidate global (called for every
   scanned store whose lhs is a VAR_DECL).  */

static void
census_global_pointer_store (tree decl, tree value)
{
  unsigned HOST_WIDE_INT ignore;
  if (!foldable_global_pointer_p (decl, &ignore))
    return;
  if (!global_ptr_stores)
    global_ptr_stores = new hash_map<tree, global_ptr_census>;
  global_ptr_census &c = global_ptr_stores->get_or_insert (decl);
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
/* Address folding (mirrors the proven rtl-rvtt-mop-form.cc helpers).  */

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
     value (verified against the TU store census at finish).  */
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

/* ---- Context-bound resolution (lane CF; contract in
   rvtt-mop-derive.h).  */

/* Resolve VAL through the PARM-binding chain of *CTX_IO: while VAL is
   the default definition of a bound parameter, replace it by the
   driving call's actual argument, read under the caller's context.
   The hop bound is proof work, not semantics (an unresolved chain
   refuses downstream).  */

} // anonymous namespace

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

void
rvtt_mop_census_poison (rvtt_mop_scan_ctx *ctx, tree var)
{
  if (ctx && ctx->census && var)
    ctx->census->poisoned.add (var);
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

/* The censused automatic aggregate and field a memory reference REF
   denotes: a direct field of an automatic local
   (var.field / COMPONENT_REF(VAR_DECL)), or a field reached through a
   bound this-pointer (COMPONENT_REF(MEM_REF(bound &var, 0))).  */

static bool
censused_field_ref (tree ref, rvtt_mop_scan_ctx *ctx, tree *var, tree *field)
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
	  int a = pushed_word_opcode_byte (gimple_assign_rhs1 (def), depth + 1, ctx);
	  int b = pushed_word_opcode_byte (gimple_assign_rhs2 (def), depth + 1, ctx);
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
	return pushed_word_opcode_byte (gimple_assign_rhs1 (def), depth + 1, ctx);
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
   EVERY field value: under the field-discipline axiom the runtime
   operands stay inside their bit fields, but WHICH fields they occupy
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
      /* A word loaded back out of a censused local aggregate's field:
	 admissible exactly when the subtree census proves every store
	 to that (object, field) audited and the object's address never
	 escaped the scanned subtree.  Flow-insensitive by design --
	 WHICH store reached this load never matters.  */
      if (is_gimple_assign (def)
	  && gimple_assign_rhs_class (def) == GIMPLE_SINGLE_RHS)
	{
	  tree var = NULL_TREE, field = NULL_TREE;
	  if (censused_field_ref (gimple_assign_rhs1 (def), ctx,
				  &var, &field))
	    {
	      if (!ctx || !ctx->census)
		{
		  *why = "aggregate-field word outside a censused scan";
		  return false;
		}
	      if (ctx->census->poisoned.contains (var))
		{
		  *why = "mop-template-field-unproven: the aggregate's "
			 "address escaped the scanned subtree";
		  return false;
		}
	      bool seen = false;
	      for (rvtt_mop_obj_census::entry &e : ctx->census->fields)
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
	      *why = "mop-template-field-unproven: no censused store "
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
   inside linker-allocated memory regions ([PROD] fact
   XTT_LINK_IMAGE_DISJOINT, rvtt-mop-tables.h).  A store through a
   pointer derived from one of these stays inside the anchored object
   by the in-bounds object model and therefore cannot alias an
   instruction FIFO.  `__instrn_buffer' is deliberately NOT here: it
   is the FIFO (XTT_INSTRN_BUF fact) and classifies its word.  */

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

static addr_class
classify_decl_base (tree decl)
{
  if (!VAR_P (decl))
    return ADDR_UNKNOWN;
  const char *name = decl_asm_name (decl);
  if (name && !strcmp (name, "__instrn_buffer"))
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
   Sound because (a) the function is not address-taken, so every
   executable call is a direct cgraph edge (indirect copies cannot
   exist), and (b) under AXIOM kernel-single-TU (rtl-rvtt-mop-form.cc
   file header) no other translation unit can call it.  Recursion
   through a caller's own parameter is depth-limited and
   cycle-refused.  */

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
   K cannot pass the guard once the bound is reached (the production
   shapes: the GPR-file zeroing fill, byte-copy loops over casted
   constant apertures).  On success *LO/*HI receive the closed store
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
	  the STEPPED value -- taking the backedge implies the test
	  passed, so the NEXT iteration's PHI value is bounded;
       B. unrotated loop: the PHI's own block ends in a cond on the
	  PHI value and the store's block is that cond's guarded
	  immediate successor -- executing the store implies THIS
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
	  /* Only word 0 is the architected aperture ([SIM]
	     tensix_inst_wr32 verifies offset == 0).  */
	  *why = "instruction-buffer offset unproven";
	  return false;
	}
      return classify_word_value (value, claimed, why, st,
				  /*in_slot=*/false, 0, ctx);
    }
  /* PC_BUF: the sync and semaphore words are inert for
     PRGM/LaneConfig/CC and deliver no instruction ([SIM]
     tensix_pc_buf_wr32: TENSIX_SYNC/MOP_SYNC block, SEMAPHORE
     posts; no other offset is architected).  */
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
     interface on silicon (ckernel_debug.h); no recorded fact pins
     which offsets are inert.  */
  if (addr >= XTT_DEBUG_REGS_MMIO_BASE && addr <= XTT_DEBUG_REGS_MMIO_LIMIT)
    {
      *why = "debug-regs-write-unproven";
      return false;
    }
  /* Any other constant address: inert for this proof -- no other
     MMIO-store arm reaches instruction delivery or SFPU state
     ([SIM] t_tile_mmio_wr32 decoder census, XTT_MMIO_STORE_INERT
     fact in rvtt-mop-tables.h); L1/data addresses a fortiori.  */
  return true;
}

} // anonymous namespace

/* ------------------------------------------------------------------ */
/* Store classification entry point.				      */

bool
rvtt_mop_derive_store (gimple *stmt, unsigned *claimed, const char **why,
		       rvtt_mop_derive_state *st, rvtt_mop_scan_ctx *ctx)
{
  if (!is_gimple_assign (stmt) || !gimple_store_p (stmt))
    return true;
  /* An end-of-life clobber marks storage death; it stores nothing and
     in particular is not an unaudited write for the field census.  */
  if (gimple_clobber_p (stmt))
    return true;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return true;

  /* Census direct stores to foldable global pointers (the verify half
     of the assume+verify global-value derivation).  */
  if (DECL_P (lhs))
    census_global_pointer_store (lhs, gimple_assign_rhs1 (stmt));

  /* Context-bound scan: census stores into fields of automatic local
     aggregates (rvtt-mop-derive.h) -- each store records whether its
     value classifies through the audited word table, so a later
     template-slot load out of the same field can be admitted by the
     union-over-stores proof.  A stored ADDRESS of such an aggregate is
     an escape and poisons it (a store census can no longer exclude
     unaudited writers).  */
  if (ctx && ctx->census)
    {
      tree var = NULL_TREE, field = NULL_TREE;
      if (censused_field_ref (lhs, ctx, &var, &field))
	{
	  const char *fwhy = nullptr;
	  bool ok = classify_word_value (gimple_assign_rhs1 (stmt), claimed,
					 &fwhy, st, /*in_slot=*/true, 0, ctx);
	  ctx->census->fields.safe_push
	    (rvtt_mop_obj_census::entry { var, field, ok });
	}
      else if (tree evar = bound_local_object (gimple_assign_rhs1 (stmt),
					       ctx))
	rvtt_mop_census_poison (ctx, evar);
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
	    rvtt_mop_census_poison (ctx, base);
	}
    }

  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    return classify_constant_target (addr, gimple_assign_rhs1 (stmt),
				     claimed, why, st, ctx);

  /* Non-constant address.  A store into a known non-volatile object is
     memory, not MMIO (hardware registers are declared volatile; the
     same rule the mop-form caller census uses).  */
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
   value is unresolvable.  (Mirrors rtl-rvtt-mop-form.cc
   mop_asm_operand_value.)  */

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

/* The canonical scalar blocking-store idiom (store, reload, consume:
   the pcbuf/mailbox handshake; [PROD] ckernel.h store_blocking).  Two
   spellings exist in the production headers -- positional and named
   operands.  It STORES its value operand at its address operand, so
   it classifies like any other store (formerly it was admitted
   blind, which was exactly the volatile-push blind spot in asm
   form).  */

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
     store census (the scan visited every store in every walkable
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
	global_ptr_census *c
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

/* ------------------------------------------------------------------ */
/* Raw REPLAY record regions (lane HS;
   -mtt-tensix-optimize-opaque-replay-record).

   THE THEOREM.  A raw REPLAY word with load_mode=1 opens a record
   window: the thread's next COUNT delivered frontend words are stored
   to the 32-slot per-thread replay buffer, and with
   execute_while_loading=0 they are architecturally SWALLOWED -- never
   pushed to the backend ([SIM]/[SPEC] facts: rvtt-mop-tables.h REPLAY
   field-decode block).  Stored content has effects only when a
   playback delivers it, and in a translation unit this proof admits,
   NO playback path exists:

     (a) a raw REPLAY execute word (load_mode=0) refuses this proof by
	 name (replay-execute-unproven), so an admitted TU contains
	 none;
     (b) a REPLAY word in a MOP template slot keeps its established
	 refusal (mop-template-replay-unproven), and a REPLAY word
	 pushed through the instruction-FIFO store census keeps the
	 audited-table refusal;
     (c) the compiler's own typed replay machinery delivers only its
	 OWN recorded content: every replay-forming pass bounds its
	 record-to-launch extent against calls and raw asm
	 (rtl-rvtt-replay.cc loop_preserves_replay_p, the window run
	 scans, and the hoist boundary rules all treat any asm or call
	 as a hard boundary), so a raw record in a callee can never
	 execute between a typed record and its launches.

   Therefore an admitted record region's words have ZERO
   PRGM/LaneConfig/CC effect: they are removed from the delivered
   stream and their stored images are never played back.  The scan
   EXCLUDES them from the executed-word census (the *SUPPRESSED set);
   every other word in the TU is still audited as delivered.

   PROOF OBLIGATIONS (all fail-closed, named; any refusal keeps the
   established opaque-region refusal byte-identically):

     - the record word itself must decode cleanly: reserved bits
       zero, len in [1,32], start_idx + len within the buffer
       ([SIM] TTSIM_VERIFY arms) -- replay-record-word-unproven;
     - exec_while_loading=1 admits WITHOUT suppression: every window
       word also executes and is audited as delivered by the ordinary
       scan; the stored image is unreachable by (a)-(c);
     - exec=0 needs the swallowed region statically identified: the
       walk from the record collects exactly COUNT raw `.ttinsn'
       constant words along straight-line control flow (transparent
       scalar statements allowed) and through at most one structurally
       counted single-block loop whose EVERY trip is swallowed --
       any call, typed builtin, volatile store, unrecognized asm, or
       FIFO-delivering statement inside the window refuses
       (replay-record-interleave-unproven: it would be swallowed too,
       silently discarding compiler-known semantics), any other
       control shape refuses (replay-record-region-shape-unproven),
       and a window the static region cannot fill exactly refuses
       (replay-record-count-unproven);
     - recorded words that could write PRGM state if a future
       increment ever admitted playback refuse as a belt: SFPCONFIG,
       SFPLOADI with a non-allocatable destination, nested
       MOP/MOP_CFG/REPLAY (replay-record-content-prgm-unproven /
       replay-record-nested-unproven).  CC-writing or otherwise
       unaudited SFPU compute words are ADMITTED as recorded content:
       the no-playback theorem, not a per-opcode effect table, is what
       discharges them (the production shape: the binary-GCD init's
       recorded SFPLZ with the CC_NE0 modifier).

   The counted-loop arm exists for the production rolled shape
   (ckernel_sfpu_gcd.h calculate_sfpu_gcd_init: TTI_REPLAY(0,28,0,1)
   followed by a 4-trip `#pragma GCC unroll' loop of 7 words -- the
   einline-stage body the TU walk scans on demand is still rolled).
   Trip counts are proven structurally from the loop's own IV and
   guard (constant init, positive constant step, NE-with-divisibility
   or LT exit against a constant bound, init below the bound), the
   same discipline as the bounded-IV store-range proof above; the
   count is exact, so "every trip swallowed" is an equality, never an
   estimate.

   PLACEMENT SAFETY (why the transform can never insert programming
   INSIDE an admitted window): residency/prgm-const programming lands
   only on the function-entry edge or on preheaders of loops that
   contain typed residency candidates.  An admitted window's interior
   consists of raw words and transparent scalars only -- a loop inside
   it contains no typed statement at all (the interleave refusal), so
   it can neither be nor enclose a candidate loop -- and the entry
   edge precedes every statement.  */

namespace {

/* Belt classification of one recorded (swallowed) word: NULL when
   recordable, else the named refusal.  */

static const char *
replay_record_word_disposition (uint32_t word)
{
  unsigned opcode = word >> 24;
  if (opcode == XTT_REPLAY_OPCODE || opcode == XTT_MOP_OPCODE
      || opcode == XTT_MOP_CFG_OPCODE)
    return "replay-record-nested-unproven: expander word in recorded content";
  if (opcode == 0x91)		/* SFPCONFIG: PRGM/LaneConfig writer */
    return "replay-record-content-prgm-unproven: SFPCONFIG in recorded "
	   "content";
  if (opcode == 0x71 && ((word >> 20) & 0xf) >= 8)
    return "replay-record-content-prgm-unproven: non-allocatable SFPLOADI "
	   "in recorded content";
  return nullptr;
}

/* One statement inside an open record window.  */

enum region_stmt_class { RGN_TRANSPARENT, RGN_WORD, RGN_REFUSE };

static region_stmt_class
classify_region_stmt (gimple *stmt, uint32_t *word, const char **why)
{
  if (is_gimple_debug (stmt))
    return RGN_TRANSPARENT;
  switch (gimple_code (stmt))
    {
    case GIMPLE_LABEL:
    case GIMPLE_NOP:
    case GIMPLE_PREDICT:
    case GIMPLE_COND:		/* control: adjudicated by the BB walk */
    case GIMPLE_RETURN:
      return RGN_TRANSPARENT;
    default:
      break;
    }
  if (gasm *a = dyn_cast <gasm *> (stmt))
    {
      const char *s = gimple_asm_string (a);
      while (*s == ' ' || *s == '\t')
	++s;
      if (!*s || !strcmp (s, "fence"))
	/* Pure barrier / scalar fence: delivers no Tensix word.  */
	return RGN_TRANSPARENT;
      if (rvtt_raw_ttinsn_word_p (a, word))
	{
	  if (const char *d = replay_record_word_disposition (*word))
	    {
	      *why = d;
	      return RGN_REFUSE;
	    }
	  return RGN_WORD;
	}
      *why = "replay-record-interleave-unproven: unrecognized asm inside "
	     "a record window";
      return RGN_REFUSE;
    }
  if (is_gimple_call (stmt))
    {
      /* Any call -- typed builtin, scalar builtin, defined function --
	 could deliver Tensix words that the open window would swallow;
	 fail closed on all of them.  */
      *why = "replay-record-interleave-unproven: call inside a record "
	     "window";
      return RGN_REFUSE;
    }
  if (is_gimple_assign (stmt))
    {
      if (gimple_clobber_p (stmt))
	return RGN_TRANSPARENT;
      if (gimple_store_p (stmt))
	{
	  tree lhs = gimple_get_lhs (stmt);
	  tree base = lhs ? get_base_address (lhs) : NULL_TREE;
	  if ((lhs && TREE_THIS_VOLATILE (lhs))
	      || (base && DECL_P (base) && TREE_THIS_VOLATILE (base)))
	    {
	      /* A volatile store could push an instruction-FIFO word
		 into the open window (the same rule the TU store
		 census uses: hardware registers are declared
		 volatile; non-volatile stores are memory).  */
	      *why = "replay-record-interleave-unproven: volatile store "
		     "inside a record window";
	      return RGN_REFUSE;
	    }
	}
      return RGN_TRANSPARENT;
    }
  *why = "replay-record-interleave-unproven: unproven statement inside "
	 "a record window";
  return RGN_REFUSE;
}

/* Structural exact trip count of the single-block self-loop LOOP_BB
   entered by ENTRY_E (the same discipline as bounded_iv_store_range's
   guard forms, restricted to the do-while shape a rolled counted region
   loop takes): IV = PHI <C (entry), STEP_VAL (backedge)>, STEP_VAL =
   IV + K in this block, the block's closing cond tests STEP_VAL
   against constant BOUND, and taking the backedge means the test
   passed.  NE requires divisibility; LT takes the ceiling; both
   require C < BOUND (the do-while body runs before any test).  */

static bool
region_loop_trip_count (basic_block header, edge entry_e, edge latch_e,
			edge iterate_e, unsigned HOST_WIDE_INT *trips)
{
  gimple_stmt_iterator lgsi = gsi_last_bb (header);
  if (gsi_end_p (lgsi))
    return false;
  gcond *cond = dyn_cast <gcond *> (gsi_stmt (lgsi));
  if (!cond)
    return false;
  tree op0 = gimple_cond_lhs (cond), op1 = gimple_cond_rhs (cond);
  tree_code ccode = gimple_cond_code (cond);
  if (TREE_CODE (op0) == INTEGER_CST)
    {
      std::swap (op0, op1);
      ccode = swap_tree_comparison (ccode);
    }
  if (TREE_CODE (op0) != SSA_NAME || TREE_CODE (op1) != INTEGER_CST
      || !(tree_fits_uhwi_p (op1) || tree_fits_shwi_p (op1)))
    return false;
  /* Two guard forms:
       do-while (rotated / ivcanon): the tested value is STEP_VAL =
	 IV + K defined in the loop, the PHI's latch argument;
       while (unrotated header guard): the tested value IS the PHI,
	 whose latch argument is PHI + K defined in the loop.
     Both give the same exact-trip formulas (NE divisibility / LT
     ceiling; the do-while's test-after-step and the while's
     test-before-body land on the same count for the same C/K/BOUND
     because the do-while's C has already absorbed no step at entry
     and the while's exit test runs on the un-stepped value).  */
  gphi *phi;
  tree kt;
  if (gphi *p = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (op0)))
    {
      /* while form: OP0 is the PHI; latch arg = PHI + K.  */
      phi = p;
      if (gimple_bb (phi) != header || gimple_phi_num_args (phi) != 2)
	return false;
      tree latch_val = gimple_phi_arg_def_from_edge (phi, latch_e);
      if (!latch_val || TREE_CODE (latch_val) != SSA_NAME)
	return false;
      gimple *stepd = SSA_NAME_DEF_STMT (latch_val);
      if (!stepd || !is_gimple_assign (stepd)
	  || (gimple_assign_rhs_code (stepd) != PLUS_EXPR
	      && gimple_assign_rhs_code (stepd) != POINTER_PLUS_EXPR)
	  || gimple_assign_rhs1 (stepd) != op0)
	return false;
      kt = gimple_assign_rhs2 (stepd);
    }
  else
    {
      /* do-while form: OP0 = IV + K; PHI's latch arg is OP0.  */
      gimple *stepd = SSA_NAME_DEF_STMT (op0);
      if (!stepd || !is_gimple_assign (stepd)
	  || (gimple_assign_rhs_code (stepd) != PLUS_EXPR
	      && gimple_assign_rhs_code (stepd) != POINTER_PLUS_EXPR))
	return false;
      tree ivt = gimple_assign_rhs1 (stepd);
      kt = gimple_assign_rhs2 (stepd);
      if (TREE_CODE (ivt) != SSA_NAME)
	return false;
      phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (ivt));
      if (!phi || gimple_bb (phi) != header
	  || gimple_phi_num_args (phi) != 2
	  || gimple_phi_arg_def_from_edge (phi, latch_e) != op0)
	return false;
    }
  if (TREE_CODE (kt) != INTEGER_CST
      || !(tree_fits_uhwi_p (kt) || tree_fits_shwi_p (kt)))
    return false;
  tree init = gimple_phi_arg_def_from_edge (phi, entry_e);
  if (!init || TREE_CODE (init) != INTEGER_CST
      || !(tree_fits_uhwi_p (init) || tree_fits_shwi_p (init)))
    return false;
  /* Normalize: CCODE holds exactly when the loop iterates (the
     do-while backedge / the while form's header-to-body edge).  */
  if (!(iterate_e->flags & EDGE_TRUE_VALUE))
    ccode = invert_tree_comparison (ccode, /*honor_nans=*/false);
  /* Fixed-width modular IV arithmetic at the IV type's precision (the
     ivcanon down-counting form steps by the modular -1).  */
  unsigned prec = TYPE_PRECISION (TREE_TYPE (op0));
  if (prec < 2 || prec > HOST_BITS_PER_WIDE_INT)
    return false;
  unsigned HOST_WIDE_INT mask
    = prec == HOST_BITS_PER_WIDE_INT
      ? ~(unsigned HOST_WIDE_INT) 0
      : (((unsigned HOST_WIDE_INT) 1 << prec) - 1);
  unsigned HOST_WIDE_INT c = TREE_INT_CST_LOW (init) & mask;
  unsigned HOST_WIDE_INT k = TREE_INT_CST_LOW (kt) & mask;
  unsigned HOST_WIDE_INT bound = TREE_INT_CST_LOW (op1) & mask;
  if (k == 0)
    return false;
  bool down = k > (mask >> 1);	/* modular negative step */
  if (ccode == NE_EXPR)
    {
      /* Monotonic modular ascent (descent) from C exits exactly at
	 BOUND; divisibility keeps equality from being stepped over
	 (a non-divisible NE loop would wrap -- refuse).  */
      if (down)
	{
	  unsigned HOST_WIDE_INT kd = (mask - k + 1) & mask;
	  if (bound >= c || (c - bound) % kd != 0)
	    return false;
	  *trips = (c - bound) / kd;
	  return true;
	}
      if (bound <= c || (bound - c) % k != 0)
	return false;
      *trips = (bound - c) / k;
      return true;
    }
  if (ccode == LT_EXPR && !down)
    {
      if (bound <= c)
	return false;
      *trips = (bound - c + k - 1) / k;
      return true;
    }
  return false;
}

} // anonymous namespace

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

bool
rvtt_mop_replay_record_admit (gasm *record, uint32_t word,
			      hash_set<gimple *> *suppressed,
			      const char **why)
{
  gcc_checking_assert ((word >> 24) == XTT_REPLAY_OPCODE);
  if (!((word >> XTT_REPLAY_LOAD_BIT) & 1))
    {
      *why = "replay-execute-unproven: playback of recorded content";
      return false;
    }
  unsigned len = (word >> XTT_REPLAY_LEN_SHIFT) & XTT_REPLAY_LEN_MASK;
  unsigned idx = (word >> XTT_REPLAY_IDX_SHIFT) & XTT_REPLAY_IDX_MASK;
  if ((word & XTT_REPLAY_RESERVED_MASK) != 0
      || len < 1 || len > XTT_REPLAY_BUF_SLOTS
      || idx >= XTT_REPLAY_BUF_SLOTS
      || idx + len > XTT_REPLAY_BUF_SLOTS)
    {
      /* The sim model TTSIM_VERIFY-faults these encodings; no recorded
	 fact pins what silicon does with them.  */
      *why = "replay-record-word-unproven: malformed REPLAY encoding";
      return false;
    }
  /* Exec-while-loading admits WITHOUT suppression -- every window
     word also executes and is audited as delivered by the ordinary
     scan, and the stored image is unreachable (theorem arms (a)-(c)).
     The window must STILL be walked: a nested expander word inside an
     open window is neutered to stored data by the hardware (the
     window arm precedes the REPLAY decode arm), so a REPLAY/MOP word
     the static scan would treat as ACTING must refuse -- and finding
     the window's end requires the same statically-countable region
     (a typed call's emitted word count is unknowable).  */
  bool exec_while_loading = (word >> XTT_REPLAY_EXEC_BIT) & 1;

  /* Prove the window region (swallowed when Exec=0).  */
  unsigned HOST_WIDE_INT need = len;
  basic_block bb = gimple_bb (record);
  if (!bb)
    {
      *why = "replay-record-region-shape-unproven";
      return false;
    }
  gimple_stmt_iterator gsi = gsi_for_stmt (record);
  gsi_next (&gsi);
  auto_vec<gimple *, 32> members;
  unsigned budget = 4096;
  while (need)
    {
      if (gsi_end_p (gsi))
	{
	  if (!single_succ_p (bb))
	    {
	      *why = "replay-record-region-shape-unproven";
	      return false;
	    }
	  basic_block succ = single_succ (bb);
	  if (succ->index == EXIT_BLOCK)
	    {
	      *why = "replay-record-count-unproven: window escapes the "
		     "scanned body";
	      return false;
	    }
	  if (single_pred_p (succ))
	    {
	      bb = succ;
	      gsi = gsi_start_bb (bb);
	      continue;
	    }
	  /* The one non-straight-line shape: a structurally counted
	     loop entered here -- either the rotated single-block
	     self-loop (do-while) or the unrotated header+body pair
	     (while).  SUCC is the header.  */
	  if (EDGE_COUNT (succ->preds) != 2 || EDGE_COUNT (succ->succs) != 2)
	    {
	      *why = "replay-record-region-shape-unproven";
	      return false;
	    }
	  edge entry_e = nullptr, latch_e = nullptr;
	  edge_iterator ei;
	  edge e;
	  FOR_EACH_EDGE (e, ei, succ->preds)
	    {
	      if (e->src == bb)
		entry_e = e;
	      else
		latch_e = e;
	    }
	  if (!entry_e || !latch_e)
	    {
	      *why = "replay-record-region-shape-unproven";
	      return false;
	    }
	  basic_block body = nullptr;	/* while form's body block */
	  edge iterate_e = nullptr, exit_e = nullptr;
	  if (latch_e->src == succ)
	    {
	      /* do-while self-loop: the latch is the header itself.  */
	      iterate_e = latch_e;
	      exit_e = EDGE_SUCC (succ, 0) == latch_e
		       ? EDGE_SUCC (succ, 1) : EDGE_SUCC (succ, 0);
	    }
	  else
	    {
	      /* while form: the latch must be a separate body block,
		 entered only from the header's iterate edge and
		 returning only to the header.  */
	      body = latch_e->src;
	      if (!single_pred_p (body) || !single_succ_p (body)
		  || single_pred (body) != succ
		  || single_succ (body) != succ)
		{
		  *why = "replay-record-region-shape-unproven";
		  return false;
		}
	      iterate_e = EDGE_SUCC (succ, 0)->dest == body
			  ? EDGE_SUCC (succ, 0) : EDGE_SUCC (succ, 1);
	      exit_e = EDGE_SUCC (succ, 0)->dest == body
		       ? EDGE_SUCC (succ, 1) : EDGE_SUCC (succ, 0);
	      if (iterate_e->dest != body || exit_e->dest == body)
		{
		  *why = "replay-record-region-shape-unproven";
		  return false;
		}
	    }
	  if (exit_e->dest == succ)
	    {
	      *why = "replay-record-region-shape-unproven";
	      return false;
	    }
	  /* Per-trip word census.  The words live in the per-trip part
	     (the self-loop block / the while body); a delivered word in
	     the while HEADER would run trips+1 times and refuses.  */
	  unsigned HOST_WIDE_INT per_trip = 0;
	  auto_vec<gimple *, 16> loop_members;
	  basic_block census[2] = { succ, body };
	  for (basic_block cbb : census)
	    {
	      if (!cbb)
		continue;
	      for (gimple_stmt_iterator lgsi = gsi_start_bb (cbb);
		   !gsi_end_p (lgsi); gsi_next (&lgsi))
		{
		  if (!budget--)
		    {
		      *why = "replay-record-region-shape-unproven";
		      return false;
		    }
		  uint32_t w;
		  const char *cwhy = nullptr;
		  switch (classify_region_stmt (gsi_stmt (lgsi), &w, &cwhy))
		    {
		    case RGN_TRANSPARENT:
		      break;
		    case RGN_WORD:
		      if (body && cbb == succ)
			{
			  *why = "replay-record-region-shape-unproven: "
				 "delivered word in a while-loop header";
			  return false;
			}
		      loop_members.safe_push (gsi_stmt (lgsi));
		      ++per_trip;
		      break;
		    case RGN_REFUSE:
		      *why = cwhy;
		      return false;
		    }
		}
	    }
	  if (per_trip == 0)
	    {
	      /* A wordless loop delivers nothing: transparent.  */
	      bb = exit_e->dest;
	      if (bb->index == EXIT_BLOCK || !single_pred_p (bb))
		{
		  *why = "replay-record-region-shape-unproven";
		  return false;
		}
	      gsi = gsi_start_bb (bb);
	      continue;
	    }
	  unsigned HOST_WIDE_INT trips = 0;
	  if (!region_loop_trip_count (succ, entry_e, latch_e, iterate_e,
				       &trips)
	      || trips == 0)
	    {
	      *why = "replay-record-trips-unproven: counted-loop trip "
		     "count inside a record window";
	      return false;
	    }
	  if (per_trip > need / trips || per_trip * trips > need)
	    {
	      /* A partial-trip swallow cannot be attributed per trip:
		 some executions of the same statement would be
		 swallowed and others delivered.  */
	      *why = "replay-record-count-unproven: loop deliveries "
		     "exceed the record window";
	      return false;
	    }
	  need -= per_trip * trips;
	  for (gimple *m : loop_members)
	    members.safe_push (m);
	  if (need == 0)
	    break;
	  bb = exit_e->dest;
	  if (bb->index == EXIT_BLOCK || !single_pred_p (bb))
	    {
	      *why = "replay-record-region-shape-unproven";
	      return false;
	    }
	  gsi = gsi_start_bb (bb);
	  continue;
	}
      if (!budget--)
	{
	  *why = "replay-record-region-shape-unproven";
	  return false;
	}
      gimple *stmt = gsi_stmt (gsi);
      uint32_t w;
      const char *cwhy = nullptr;
      switch (classify_region_stmt (stmt, &w, &cwhy))
	{
	case RGN_TRANSPARENT:
	  break;
	case RGN_WORD:
	  members.safe_push (stmt);
	  --need;
	  break;
	case RGN_REFUSE:
	  *why = cwhy;
	  return false;
	}
      gsi_next (&gsi);
    }
  /* Exec=0: the window words are architecturally never delivered --
     exclude them from the executed-word census.  Exec=1: they execute
     and stay in the census (the walk above still proved the window
     free of neutered expander words).  */
  if (!exec_while_loading)
    for (gimple *m : members)
      suppressed->add (m);
  return true;
}
