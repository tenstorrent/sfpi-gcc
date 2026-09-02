/* One vector-register pressure/liveness engine for Tensix.
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

/* GIMPLE-side vector-register (LREG) pressure engine.

   Three formerly hand-kept mirrors of the same conservative counting
   are owned here now, each reproduced verdict-identically (CLASS-I):

   - engine_compute_lreg_pressure: the function-wide may-live model
     (promoted seed, from gimple-rvtt-prgm-const.cc) -- backward
     fixpoint on GCC bitmaps over SSA_NAME_VERSION with per-point
     peaks and fail-closed width handling;
   - engine_bb_peak: the single-block conservative peak (from
     gimple-rvtt-reassoc.cc, also queried per licensed mad-fuse
     candidate from rvtt.gc);
   - engine_loop_legal_p: the loop-scoped candidate-set legality
     proof (from gimple-rvtt-invariant.cc), and its incremental
     profile form (rvtt_loop_pressure) that answers each greedy
     selector's per-candidate verdict from a base profile computed
     once instead of a full body re-walk per candidate.

   Parameters live here as data, once:
   - the file capacity (rvtt_pressure_capacity -- the ONE place the
     budget constant is read);
   - the value width table (lreg_width);
   - the tracked-value predicate (rvtt_pressure_tracked_p);
   - the CC-transient per-insn charges (rvtt_pressure_cc_transient,
     declared facts -- the typed-effect-table handoff point);
   - the LUT table-slot operand-class fact
     (rvtt_pressure_lut_slot_args, declared per insn and validated
     under flag_checking against the machine description's hard-LREG
     operand constraints, so a known class of bug -- a new
     slot-reading position silently treated as creg-capable -- fails
     the checking build instead of undercounting).

   Stage-A conservatism contract: for one pin (50) every public query
   recomputed its verdict with a verbatim copy of the retired mirror
   and asserted equality, corpus- and testsuite-wide, with zero
   disagreements; the legacy copies and their asserts were deleted at
   pin 51.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-ssanames.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "dominance.h"
#include "rtl.h"
#include "memmodel.h"
#include "insn-config.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include <unordered_map>
#include <unordered_set>

/* Capacity of the allocatable vector-register file.  THE one place
   the engine reads it.  (SFPU_REG_NUM itself derives from the
   register-file bounds in riscv.h; no other pressure consumer may
   spell the budget.)  */

unsigned
rvtt_pressure_capacity ()
{
  return SFPU_REG_NUM;
}

/* Declared per-insn CC-transient LREG charges: the RTL-only LREG
   temporaries CC lowering materializes at STMT's position --
   compare-immediate loads (rvtt_emit_sfpxfcmps/xicmps) and the
   boolean-tree saved-enables value (gimple-rvtt-expand.cc
   process_bool_tree) -- which no SSA walk can see.  Declared here as
   data; when the typed per-insn effect table reaches gimple, these
   rows move there and this function becomes a lookup.  */

unsigned
rvtt_pressure_cc_transient (gimple *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return 0;
  if (insnd->id == rvtt_insn_data::sfpxbool
      || insnd->id == rvtt_insn_data::sfpxcondi)
    return 2;
  if (insnd->id != rvtt_insn_data::sfppushc
      && insnd->id != rvtt_insn_data::sfppopc
      && insnd->sets_cc (as_a <gcall *> (stmt)))
    return 1;
  return 0;
}

/* LUT table-slot operand-class fact.  A LUT table slot is an implicit
   hard register: the formed instruction reads the architectural table
   LRegs directly, so a slot operand defined by a constant-register
   read forces a physical copy into the slot LReg at register
   allocation (the FP32-direct placement exemption would
   otherwise undercount that copy).  The declared rows are the leading
   table-slot argument counts of the LUT-family builtins; the checking
   validation below ties every row -- and the absence of undeclared
   family members -- to the machine description's operand constraints,
   the authoritative statement of the hard-LREG pinning.  */

#define RVTT_PRESSURE_LUT_SLOT_INSNS \
  RVTT_LUT_SLOT_INSN (sfplut, 3) \
  RVTT_LUT_SLOT_INSN (sfplutfp32_3r, 3) \
  RVTT_LUT_SLOT_INSN (sfplutfp32_6r, 6)

/* The md patterns realizing the declared rows (split forms
   included).  */

#define RVTT_PRESSURE_LUT_SLOT_ICODES \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplut, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_3r, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_3r_split, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_6r, 6)

/* Number of leading LUT table-slot arguments of the insn INSND
   describes; 0 for everything outside the LUT family.  */

static unsigned
rvtt_pressure_lut_slot_args (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
#define RVTT_LUT_SLOT_INSN(ID, NSLOTS) \
    case rvtt_insn_data::ID: return NSLOTS;
    RVTT_PRESSURE_LUT_SLOT_INSNS
#undef RVTT_LUT_SLOT_INSN
    default:
      return 0;
    }
}

/* True when OP is an input operand constraint pinning a single hard
   LREG ("x0".."x7"): the md spelling of an implicit-table position.  */

static bool
hard_lreg_constraint_p (const insn_operand_data &op)
{
  const char *c = op.constraint;
  return c && c[0] == 'x' && ISDIGIT (c[1]) && !c[2]
	 && op.mode == E_XTT32SImode;
}

/* flag_checking validation of the declared slot facts against the
   machine description (run once per process):
   - every declared icode hard-pins md operands 1..NSLOTS (the row
     cannot go stale against a renumbered or retired pattern);
   - every target insn whose operands 1 and 2 are both hard-pinned
     vector inputs is a declared row (a NEW LUT-family pattern cannot
     be added without declaring its slot fact -- that bug class
     fails the checking build instead of undercounting).  */

static void
rvtt_pressure_validate_lut_slot_facts ()
{
  static bool validated = false;
  if (validated)
    return;
  validated = true;

  struct { int icode; unsigned nslots; } rows[] = {
#define RVTT_LUT_SLOT_ICODE(ICODE, NSLOTS) { (int) ICODE, NSLOTS },
    RVTT_PRESSURE_LUT_SLOT_ICODES
#undef RVTT_LUT_SLOT_ICODE
  };
  for (const auto &r : rows)
    {
      const insn_data_d &d = insn_data[r.icode];
      gcc_assert (d.n_operands > (int) r.nslots);
      for (unsigned k = 1; k <= r.nslots; ++k)
	gcc_assert (hard_lreg_constraint_p (d.operand[k]));
    }
  for (unsigned i = 0; i < NUM_INSN_CODES; ++i)
    {
      const insn_data_d &d = insn_data[i];
      if (d.n_operands < 3
	  || !hard_lreg_constraint_p (d.operand[1])
	  || !hard_lreg_constraint_p (d.operand[2]))
	continue;
      bool declared = false;
      for (const auto &r : rows)
	declared |= r.icode == (int) i;
      gcc_assert (declared);
    }
}

/* True when NAME has a non-debug use in a LUT table-slot position
   (see the fact above): such a use forces a physical copy of a
   constant-register value and must never be creg-exempted.  */

static bool
rvtt_pressure_lut_slot_use_p (tree name)
{
  if (flag_checking)
    rvtt_pressure_validate_lut_slot_facts ();
  gimple *use;
  imm_use_iterator iter;
  FOR_EACH_IMM_USE_STMT (use, iter, name)
    {
      if (is_gimple_debug (use))
	continue;
      gcall *call = dyn_cast <gcall *> (use);
      if (!call)
	continue;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
      if (!insnd)
	continue;
      unsigned nslots = rvtt_pressure_lut_slot_args (insnd);
      for (unsigned ix = 0; ix < nslots; ix++)
	if (gimple_call_arg (call, ix) == name)
	  return true;
    }
  return false;
}
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

bool
rvtt_pressure_tracked_p (tree name)
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
   conservative counting of the loop proof (engine_loop_legal_p,
   below).  */


static void
engine_compute_lreg_pressure (function *fn, unsigned capacity,
			      rvtt_pressure_model *m)
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
		  if (rvtt_pressure_tracked_p (arg))
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
	      if (lhs && rvtt_pressure_tracked_p (lhs))
		bitmap_clear_bit (in, SSA_NAME_VERSION (lhs));
	      ssa_op_iter iter;
	      tree use;
	      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
		if (rvtt_pressure_tracked_p (use))
		  bitmap_set_bit (in, SSA_NAME_VERSION (use));
	    }
	  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	       gsi_next (&psi))
	    {
	      tree res = gimple_phi_result (psi.phi ());
	      if (rvtt_pressure_tracked_p (res))
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
	  if (lhs && rvtt_pressure_tracked_p (lhs))
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
	    if (rvtt_pressure_tracked_p (use)
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

/* Conservative peak count of simultaneously live SFPU vector SSA values
   across BB (the corpus finding that mandated this: a pressure-blind
   licensed rebalance turned compilable Cos/Sin/I1/welford kernels into
   lreg-pressure-exceeded refusals -- a licensed transform must never
   make a compilable kernel uncompilable).  Modeled on
   rvtt_loop_lreg_pressure_legal_p (gimple-rvtt-invariant.cc), scoped to
   one block, over-approximating in the refusing direction:
   - vector values defined outside BB and used inside it are live from
     block entry;
   - vector values with any use outside BB are pinned (never released);
   - vector values defined in a dominator of BB with a use outside their
     own defining block are counted as live THROUGH the block (they may
     span it without appearing in it);
   - everything else releases at its last in-block use.  */

static unsigned
engine_bb_peak (basic_block bb)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  std::unordered_set<tree> live;
  std::unordered_set<tree> pinned;
  std::unordered_map<tree, unsigned> remaining;

  /* Uses inside the block, per name.  */
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      ssa_op_iter iter;
      tree use;
      FOR_EACH_SSA_TREE_OPERAND (use, gsi_stmt (gsi), iter, SSA_OP_USE)
	if (VECTOR_TYPE_P (TREE_TYPE (use)))
	  ++remaining[use];
    }

  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!name || !VECTOR_TYPE_P (TREE_TYPE (name)))
	continue;
      gimple *def = SSA_NAME_DEF_STMT (name);
      basic_block def_bb = def ? gimple_bb (def) : nullptr;
      bool used_in_bb = remaining.count (name) != 0;
      bool used_outside = false;
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (!is_gimple_debug (use) && gimple_bb (use) != bb)
	  {
	    used_outside = true;
	    break;
	  }
      if (def_bb == bb)
	{
	  if (used_outside)
	    pinned.insert (name);
	  continue;
	}
      if (used_in_bb)
	{
	  live.insert (name);
	  if (used_outside)
	    pinned.insert (name);
	}
      else if (used_outside && def_bb
	       && dominated_by_p (CDI_DOMINATORS, bb, def_bb))
	{
	  /* May span the block without appearing in it.  */
	  live.insert (name);
	  pinned.insert (name);
	}
    }

  size_t peak = live.size ();
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      ssa_op_iter iter;
      tree use;
      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	if (VECTOR_TYPE_P (TREE_TYPE (use)))
	  {
	    auto found = remaining.find (use);
	    if (found != remaining.end () && found->second
		&& !--found->second && !pinned.count (use))
	      live.erase (use);
	  }
      tree lhs = gimple_get_lhs (stmt);
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	live.insert (lhs);
      peak = MAX (peak, live.size ());
    }
  return peak;
}
/* Keep the transformed loop within the architectural eight-LREG file before
   IRA.  Every hoisted value is live across the loop, as is each vector PHI
   (a loop-carried value) and each vector value defined outside the loop that
   is consumed directly by a non-PHI statement in it.  This is intentionally
   conservative: refuse the whole loop before changing virtual operands or
   statement placement when the bound is exceeded.  */
static bool
engine_loop_legal_p (class loop *loop,
				 const auto_vec<gcall *> &loads,
				 bool report, bool cc_transients,
				 bool exempt_creg_reads)
{
  std::unordered_set<tree> candidates;
  std::unordered_set<tree> pinned;
  std::unordered_set<tree> live;
  std::unordered_map<tree, unsigned> remaining;

  /* A value defined by a read of a constant-register-file register
     (LReg[8..14]: the hardwired zero/one and the programmable
     constants) never occupies one of the eight allocatable LREGs --
     every vector operand position accepts the constant register class
     directly (reg_or_cstlreg_operand), so register allocation
     satisfies such a use in place.  Callers that place values around
     instructions with creg-capable operand positions may ask to
     exempt those definitions from the liveness count (the LUT
     coefficient placement); the default keeps every historical
     consumer's counting byte-identical.

     One position is NOT creg-capable: a LUT table-slot argument.  The
     slots are not encoded operands at all -- the instruction implicitly
     reads the architectural table registers -- so a constant-register
     value feeding a slot must be physically copied into that hard LREG
     and holds it across the loop like any other coefficient.  A creg
     read with such a use is therefore counted, not exempted (the
     FP32-direct placement exemption would otherwise undercount the
     copy; for the FP16 packed modes slot words are compile-time-packed
     immediates, so this test never fires there and their counting is
     unchanged).  */
  auto creg_resident_p = [&] (tree name) -> bool
    {
      if (!exempt_creg_reads || TREE_CODE (name) != SSA_NAME)
	return false;
      gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (name));
      if (!def)
	return false;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
      if (!insnd || insnd->id != rvtt_insn_data::sfpreadlreg)
	return false;
      tree arg = gimple_call_arg (def, 0);
      if (TREE_CODE (arg) != INTEGER_CST)
	return false;
      HOST_WIDE_INT creg = tree_to_shwi (arg);
      if (creg < 8 || creg > 14)
	return false;
      return !rvtt_pressure_lut_slot_use_p (name);
    };
  for (gcall *call : loads)
    {
      tree lhs = gimple_call_lhs (call);
      candidates.insert (lhs);
      pinned.insert (lhs);
      live.insert (lhs);
    }

  /* A vector can occupy an LREG throughout the loop without appearing in
     the loop at all: for example, a value read before the loop and stored
     after it.  Account for every vector SSA definition available at loop
     entry that has any non-debug use outside the loop.  This deliberately
     over-approximates values used only before the loop; false refusal is
     preferable to creating an unspillable LREG live range.  */
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!VECTOR_TYPE_P (TREE_TYPE (name)) || candidates.count (name))
	continue;
      bool outside_use = false;
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (!is_gimple_debug (use)
	    && (!gimple_bb (use)
		|| !flow_bb_inside_loop_p (loop, gimple_bb (use))))
	  {
	    outside_use = true;
	    break;
	  }
      if (outside_use && !creg_resident_p (name))
	{
	  /* Second fp16-placement refinement: a value whose every
	     non-debug use executes BEFORE the loop is entered (each use
	     block dominates the loop header and is outside the loop) is
	     dead at loop entry -- e.g. a config-staging materialization
	     consumed by a preheader SFPCONFIG write.  The historical
	     counting deliberately over-approximates these; the refined
	     path excludes them (false refusal is no longer preferable
	     once the caller has an exact obligation to meet).  */
	  if (exempt_creg_reads)
	    {
	      bool dead_before_entry = true;
	      gimple *use2;
	      imm_use_iterator iter2;
	      FOR_EACH_IMM_USE_STMT (use2, iter2, name)
		{
		  if (is_gimple_debug (use2))
		    continue;
		  basic_block ub = gimple_bb (use2);
		  if (!ub || flow_bb_inside_loop_p (loop, ub)
		      || !dominated_by_p (CDI_DOMINATORS, loop->header, ub))
		    {
		      dead_before_entry = false;
		      break;
		    }
		}
	      if (dead_before_entry)
		continue;
	    }
	  pinned.insert (name);
	  gimple *def = SSA_NAME_DEF_STMT (name);
	  basic_block def_bb = gimple_bb (def);
	  if (!def_bb
	      || (!flow_bb_inside_loop_p (loop, def_bb)
		  && dominated_by_p (CDI_DOMINATORS, loop->header, def_bb)))
	    live.insert (name);
	}
    }

  basic_block *body = get_loop_body_in_dom_order (loop);
  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
    {
      basic_block bb = body[ix];
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	{
	  gphi *phi = psi.phi ();
	  tree lhs = gimple_phi_result (phi);
	  if (lhs && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    live.insert (lhs);
	  for (unsigned argno = 0; argno != gimple_phi_num_args (phi); ++argno)
	    {
	      tree use = gimple_phi_arg_def (phi, argno);
	      if (TREE_CODE (use) == SSA_NAME
		  && VECTOR_TYPE_P (TREE_TYPE (use)))
		++remaining[use];
	    }
	}

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  ssa_op_iter iter;
	  tree use;
	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	    if (VECTOR_TYPE_P (TREE_TYPE (use)))
	      {
		++remaining[use];
		gimple *def = SSA_NAME_DEF_STMT (use);
		basic_block def_bb = gimple_bb (def);
		if ((!def_bb || !flow_bb_inside_loop_p (loop, def_bb))
		    && !creg_resident_p (use))
		  {
		    pinned.insert (use);
		    live.insert (use);
		  }
	      }
	}
    }

  /* Walk every body block in dominance order (SSA definitions are walked
     before their non-PHI uses), releasing values at their last counted use
     and admitting locally defined vectors, tracking the peak.  PHI-argument
     uses are counted but never released here, so loop-carried values remain
     live through the walk -- conservative in the refusing direction.  For a
     multi-block body this measures pressure across the whole region,
     including any inner loops.  */
  size_t peak = live.size ();
  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	ssa_op_iter iter;
	tree use;
	FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	  if (VECTOR_TYPE_P (TREE_TYPE (use)))
	    {
	      auto found = remaining.find (use);
	      gcc_assert (found != remaining.end () && found->second);
	      if (!--found->second && !pinned.count (use))
		live.erase (use);
	    }

	tree lhs = gimple_get_lhs (stmt);
	if (lhs && TREE_CODE (lhs) == SSA_NAME
	    && VECTOR_TYPE_P (TREE_TYPE (lhs))
	    && !candidates.count (lhs)
	    && !creg_resident_p (lhs))
	  live.insert (lhs);

	/* CC machinery materializes LREG temporaries only at RTL --
	   compare-immediate loads (rvtt_emit_sfpxfcmps/xicmps) and the
	   boolean-tree saved-enables value (gimple-rvtt-expand.cc
	   process_bool_tree) -- which this SSA walk cannot see.  A
	   value hoisted to the preheader is live across those
	   positions and would compete for the registers the
	   temporaries need, turning a previously-compiling loop into
	   the post-allocation lreg-pressure-exceeded user error.
	   Charge them at their positions when the caller asks
	   (invariant-loadi hoisting into CC-carrying loops); the
	   default keeps every other consumer's counting unchanged.  */
	size_t transient = 0;
	if (cc_transients)
	  transient = rvtt_pressure_cc_transient (stmt);
	peak = MAX (peak, live.size () + transient);
      }
  free (body);

  unsigned limit = rvtt_pressure_capacity ();
  if (peak <= limit)
    return true;
  if (report && dump_file)
    fprintf (dump_file,
	     "Invariant SFPU immediate hoist refused: loop LREG pressure %zu exceeds %u\n",
	     peak, limit);
  return false;
}

/* ====================================================================
   Public queries.  Each one answers from the engine.  (The stage-A
   legacy mirrors and their verdict-identity asserts served their
   one-pin window at pin 50 -- zero disagreements corpus- and
   testsuite-wide -- and were deleted at pin 51.)
   ==================================================================== */

void
rvtt_pressure_compute (function *fn, unsigned capacity,
		       rvtt_pressure_model *m)
{
  engine_compute_lreg_pressure (fn, capacity, m);
}

int
rvtt_pressure_residual (function *fn)
{
  rvtt_pressure_model model;
  rvtt_pressure_compute (fn, rvtt_pressure_capacity (), &model);
  return (int) rvtt_pressure_capacity () - (int) model.peak;
}

unsigned
rvtt_pressure_bb_peak (basic_block bb)
{
  return engine_bb_peak (bb);
}

/* Windowed point-pressure peak (NEW vocabulary -- no
   historical mirror).  Counts with the function-wide may-live model's
   exact semantics (engine_compute_lreg_pressure: backward may-live
   fixpoint over pressure-tracked values, lreg_width weights, dead-def
   transients), NOT with engine_bb_peak's single-block count -- that
   walk pins every outside-used value for the whole block, which on a
   loop-body block overstates the trig body's point pressure by 8x and
   would refuse every candidate the file actually admits.  The peak is
   taken over the points immediately BEFORE each statement after FIRST
   through LAST (equivalently: the points strictly between FIRST and
   LAST plus LAST's own operand-live point) -- exactly the span where
   a licensed mad-restructure's kept loadi adds its one live range.  */

unsigned
rvtt_pressure_window_peak (gimple *first, gimple *last)
{
  basic_block bb = gimple_bb (first);
  gcc_assert (bb && gimple_bb (last) == bb);

  rvtt_pressure_model m;
  rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &m);

  /* live-out of BB, per the model's own edge vocabulary.  */
  bitmap live = BITMAP_ALLOC (&m.obstack);
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == EXIT_BLOCK_PTR_FOR_FN (cfun))
	continue;
      bitmap_ior_into (live, m.live_in[e->dest->index]);
      for (gphi_iterator psi = gsi_start_phis (e->dest); !gsi_end_p (psi);
	   gsi_next (&psi))
	{
	  tree arg = gimple_phi_arg_def (psi.phi (), e->dest_idx);
	  if (rvtt_pressure_tracked_p (arg))
	    bitmap_set_bit (live, SSA_NAME_VERSION (arg));
	}
    }

  unsigned count = 0;
  {
    bitmap_iterator bi;
    unsigned v;
    EXECUTE_IF_SET_IN_BITMAP (live, 0, v, bi)
      count += lreg_width (ssa_name (v));
  }

  unsigned peak = 0;
  bool in_window = false;
  for (gimple_stmt_iterator gsi = gsi_last_bb (bb); !gsi_end_p (gsi);
       gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt))
	continue;
      if (stmt == last)
	in_window = true;
      bool record = in_window && stmt != first;
      tree lhs = gimple_get_lhs (stmt);
      if (lhs && rvtt_pressure_tracked_p (lhs))
	{
	  if (bitmap_clear_bit (live, SSA_NAME_VERSION (lhs)))
	    count -= lreg_width (lhs);
	  else if (record)
	    /* Dead def: transiently occupies its registers here.  */
	    peak = MAX (peak, count + lreg_width (lhs));
	}
      ssa_op_iter iter;
      tree use;
      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	if (rvtt_pressure_tracked_p (use)
	    && bitmap_set_bit (live, SSA_NAME_VERSION (use)))
	  count += lreg_width (use);
      if (record)
	peak = MAX (peak, count);
      if (stmt == first)
	break;
    }
  BITMAP_FREE (live);
  return peak;
}

bool
rvtt_pressure_loop_legal_p (class loop *loop,
			    const auto_vec<gcall *> &loads,
			    bool report, bool cc_transients,
			    bool exempt_creg_reads)
{
  bool ok = engine_loop_legal_p (loop, loads, report, cc_transients,
				 exempt_creg_reads);
  if (flag_checking)
    /* Validate the declared LUT slot facts against the md on every
       checking run, not only when a creg exemption is requested, so
       the whole testsuite's -fchecking vehicles police the table.  */
    rvtt_pressure_validate_lut_slot_facts ();
  return ok;
}

/* ====================================================================
   Incremental per-loop residual query (rvtt_loop_pressure).

   The base profile is one instrumented run of exactly the loop-proof
   counting above with an empty candidate set and the creg exemption
   off (the two greedy selectors' configuration): per-sample counts
   (sample 0 = loop entry, then one per walked statement) plus each
   name's counted half-open [start, end) sample interval.

   A candidate set S then prices as
     peak (S) = |S| + max over samples of (base - sum of S's intervals)
   because the proof pins every candidate live across the whole loop
   (uniform +1 per candidate at every sample) while removing it from
   ordinary tracking (its base interval).  Candidates are vetted by
   rvtt_invariant_constant_load_p in both consumers: loop-body
   definitions, every non-debug use in the loop, no vector-typed
   arguments -- so removing one from ordinary tracking perturbs no
   other name's pin, release point, or transient charge.  Verdict
   identity with the full proof is asserted under flag_checking at
   every query.
   ==================================================================== */

rvtt_loop_pressure::rvtt_loop_pressure (class loop *loop, bool cc_transients)
  : m_loop (loop), m_cc_transients (cc_transients)
{
  std::unordered_set<tree> pinned;
  std::unordered_set<tree> live;
  std::unordered_map<tree, unsigned> remaining;
  unsigned sample = 0;

  auto note_insert = [&] (tree name)
    {
      if (!live.insert (name).second)
	return;
      bool existed;
      live_interval &iv = m_interval.get_or_insert (name, &existed);
      /* Single-def SSA: a name enters the live set at most once
	 (pre-walk categories are pinned and never released; a walk
	 definition point is unique).  */
      gcc_assert (!existed);
      iv.start = sample;
      iv.end = UINT_MAX;
    };
  auto note_erase = [&] (tree name)
    {
      if (!live.erase (name))
	return;
      live_interval *iv = m_interval.get (name);
      gcc_assert (iv && iv->end == UINT_MAX);
      iv->end = sample;
    };

  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!VECTOR_TYPE_P (TREE_TYPE (name)))
	continue;
      bool outside_use = false;
      gimple *use;
      imm_use_iterator iter;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	if (!is_gimple_debug (use)
	    && (!gimple_bb (use)
		|| !flow_bb_inside_loop_p (loop, gimple_bb (use))))
	  {
	    outside_use = true;
	    break;
	  }
      if (outside_use)
	{
	  pinned.insert (name);
	  gimple *def = SSA_NAME_DEF_STMT (name);
	  basic_block def_bb = gimple_bb (def);
	  if (!def_bb
	      || (!flow_bb_inside_loop_p (loop, def_bb)
		  && dominated_by_p (CDI_DOMINATORS, loop->header, def_bb)))
	    note_insert (name);
	}
    }

  basic_block *body = get_loop_body_in_dom_order (loop);
  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
    {
      basic_block bb = body[ix];
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	{
	  gphi *phi = psi.phi ();
	  tree lhs = gimple_phi_result (phi);
	  if (lhs && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    note_insert (lhs);
	  for (unsigned argno = 0; argno != gimple_phi_num_args (phi); ++argno)
	    {
	      tree use = gimple_phi_arg_def (phi, argno);
	      if (TREE_CODE (use) == SSA_NAME
		  && VECTOR_TYPE_P (TREE_TYPE (use)))
		++remaining[use];
	    }
	}

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  ssa_op_iter iter;
	  tree use;
	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	    if (VECTOR_TYPE_P (TREE_TYPE (use)))
	      {
		++remaining[use];
		gimple *def = SSA_NAME_DEF_STMT (use);
		basic_block def_bb = gimple_bb (def);
		if (!def_bb || !flow_bb_inside_loop_p (loop, def_bb))
		  {
		    pinned.insert (use);
		    note_insert (use);
		  }
	      }
	}
    }

  m_base.safe_push ((unsigned) live.size ());	/* Entry sample.  */
  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	++sample;
	ssa_op_iter iter;
	tree use;
	FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	  if (VECTOR_TYPE_P (TREE_TYPE (use)))
	    {
	      auto found = remaining.find (use);
	      gcc_assert (found != remaining.end () && found->second);
	      if (!--found->second && !pinned.count (use))
		note_erase (use);
	    }
	tree lhs = gimple_get_lhs (stmt);
	if (lhs && TREE_CODE (lhs) == SSA_NAME
	    && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	  note_insert (lhs);
	size_t transient = 0;
	if (cc_transients)
	  transient = rvtt_pressure_cc_transient (stmt);
	m_base.safe_push ((unsigned) (live.size () + transient));
      }
  free (body);
}

bool
rvtt_loop_pressure::legal_with (const auto_vec<gcall *> &candidates)
{
  unsigned n = m_base.length ();
  m_delta.truncate (0);
  m_delta.safe_grow_cleared (n + 1, true);
  for (gcall *call : candidates)
    {
      tree lhs = gimple_call_lhs (call);
      live_interval *iv = m_interval.get (lhs);
      /* Candidates are loop-body definitions: the base walk counted
	 them over a recorded interval.  */
      gcc_assert (iv);
      m_delta[iv->start] -= 1;
      m_delta[iv->end == UINT_MAX ? n : iv->end] += 1;
    }
  int carried = 0;
  unsigned peak = 0;
  for (unsigned i = 0; i < n; ++i)
    {
      carried += m_delta[i];
      unsigned here = (unsigned) ((int) m_base[i] + carried);
      peak = MAX (peak, here);
    }
  peak += candidates.length ();
  bool ok = peak <= rvtt_pressure_capacity ();
  if (flag_checking)
    gcc_assert (ok == rvtt_pressure_loop_legal_p (m_loop, candidates,
						  /*report=*/false,
						  m_cc_transients,
						  /*exempt_creg_reads=*/false));
  return ok;
}
