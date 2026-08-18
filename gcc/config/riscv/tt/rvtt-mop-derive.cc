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

/* Classify the 32-bit word VAL by the constant opcode base of its
   PLUS / BIT_IOR composition (AXIOM tt-op-field-discipline,
   rvtt-mop-tables.h).  Returns the frontend opcode byte, or -1 when no
   constant base pins it.  */

static int
pushed_word_base (tree val, unsigned depth = 0)
{
  if (depth > 12 || !val)
    return -1;
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
	  int a = pushed_word_base (gimple_assign_rhs1 (def), depth + 1);
	  int b = pushed_word_base (gimple_assign_rhs2 (def), depth + 1);
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
	return pushed_word_base (gimple_assign_rhs1 (def), depth + 1);
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
		     unsigned depth = 0)
{
  if (!val || depth > 4)
    {
      *why = "unclassifiable stored word";
      return false;
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
				      why, st, in_slot, depth + 1))
	      return false;
	  return true;
	}
      if (is_gimple_assign (def)
	  && (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def))
	      || gimple_assign_rhs_code (def) == SSA_NAME))
	return classify_word_value (gimple_assign_rhs1 (def), claimed, why,
				    st, in_slot, depth + 1);
      int base = pushed_word_base (val);
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
    fprintf (dump_file, "prgm-const: mop-derive: %s\n", out);
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
			  rvtt_mop_derive_state *st)
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
				/*in_slot=*/true))
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
				  /*in_slot=*/false);
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
		       rvtt_mop_derive_state *st)
{
  if (!is_gimple_assign (stmt) || !gimple_store_p (stmt))
    return true;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return true;

  /* Census direct stores to foldable global pointers (the verify half
     of the assume+verify global-value derivation).  */
  if (DECL_P (lhs))
    census_global_pointer_store (lhs, gimple_assign_rhs1 (stmt));

  unsigned HOST_WIDE_INT addr;
  if (ref_constant_address (lhs, &addr))
    return classify_constant_target (addr, gimple_assign_rhs1 (stmt),
				     claimed, why, st);

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
				/*in_slot=*/false);
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
