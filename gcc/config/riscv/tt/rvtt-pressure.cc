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

/* GIMPLE-level estimate of vector-register (LREG) pressure.  LREGs cannot
   be spilled, so transformations that lengthen live ranges use these
   queries to avoid turning a compilable kernel into one that needs more
   registers than exist.  Every estimate errs towards overcounting.

   - engine_compute_lreg_pressure: function-wide backward liveness over
     SSA names with per-block peaks;
   - engine_bb_peak: peak pressure within one block;
   - engine_loop_legal_p: whether a set of values can be kept live across
     a loop, plus an incremental form (rvtt_loop_pressure) that answers
     per-candidate queries from one precomputed profile.

   The register file capacity, the width of each value, which values are
   tracked, the LREG temporaries CC lowering adds, and which builtin
   operands are LUT table slots are all defined here.  The LUT slot table
   is checked against the machine description under flag_checking.  */

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
#include "rvtt-protos.h"
#include "rvtt-pressure.h"
#include <unordered_map>
#include <unordered_set>

/* Return the number of allocatable vector registers.  */

unsigned
rvtt_pressure_capacity ()
{
  return SFPU_REG_NUM;
}

/* Return the number of LREG temporaries CC lowering creates at STMT, which
   are not visible in SSA: compare-immediate loads
   (rvtt_emit_sfpxfcmps/xicmps) and the saved enables of a boolean tree
   (process_bool_tree in gimple-rvtt-pred.cc).  */

unsigned
rvtt_pressure_cc_transient (gimple *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return 0;
  /* sfpxlogic and sfpxcond take the 2-LREG charge.  sfpxcond is the single
     condition root for both compare and boolean-tree conditions, which this
     level cannot tell apart, so shapes needing fewer temporaries are
     over-charged.  Overestimating only costs a missed transformation, while
     underestimating could require a ninth LREG, which is a hard error.  */
  if (insnd->id == rvtt_insn_data::sfpxlogic
      || insnd->id == rvtt_insn_data::sfpxcond)
    return 2;
  if (insnd->id != rvtt_insn_data::sfppushc
      && insnd->id != rvtt_insn_data::sfppopc
      && insnd->sets_cc (as_a <gcall *> (stmt)))
    return 1;
  return 0;
}

/* LUT table slots.  The LUT instructions implicitly read fixed table
   LRegs, so a slot operand defined by a constant-register read needs a
   copy into the slot LReg and must be counted.  The table gives the number
   of leading table-slot arguments of each LUT builtin; under flag_checking
   it is checked against the hard-register operand constraints in the
   machine description.  */

#define RVTT_PRESSURE_LUT_SLOT_INSNS \
  RVTT_LUT_SLOT_INSN (sfplut, 3) \
  RVTT_LUT_SLOT_INSN (sfplutfp32_3r, 3) \
  RVTT_LUT_SLOT_INSN (sfplutfp32_6r, 6)

/* The md patterns of the table entries, including split forms.  */

#define RVTT_PRESSURE_LUT_SLOT_ICODES \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplut, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_3r, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_3r_split, 3) \
  RVTT_LUT_SLOT_ICODE (CODE_FOR_rvtt_sfplutfp32_6r, 6)

/* Return the number of leading LUT table-slot arguments of INSND, or 0
   if it is not a LUT instruction.  */

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

/* Return true if OP is an input operand whose constraint names a single
   hard LREG ("x0".."x7"), as LUT table slots do in the md.  */

static bool
hard_lreg_constraint_p (const insn_operand_data &op)
{
  const char *c = op.constraint;
  return c && c[0] == 'x' && ISDIGIT (c[1]) && !c[2]
	 && op.mode == E_XTT32SImode;
}

/* Check the LUT slot table against the machine description, once per
   process:
   - every listed icode has hard-register constraints on operands
     1..NSLOTS;
   - every insn whose operands 1 and 2 are both hard-register vector inputs
     is listed.  */

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

/* Return true if NAME has a non-debug use as a LUT table slot, which
   needs a copy even when NAME is a constant-register value.  */

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

/* Return the number of LREGs a tracked value occupies.  The multi-result
   modes XTT64SI and XTT128SI (sfpswap and sfptransp results) take 2 and 4
   registers; an unknown vector mode counts as the whole file.  */

/* Return the index of the constant register NAME reads, or -1.  */

static int
creg_read_index (tree name)
{
  if (TREE_CODE (name) != SSA_NAME)
    return -1;
  gimple *def = SSA_NAME_DEF_STMT (name);
  const rvtt_insn_data *insnd = def ? rvtt_get_insn_data (def) : nullptr;
  if (!insnd || insnd->id != rvtt_insn_data::sfpreadlreg)
    return -1;
  tree arg = gimple_call_arg (as_a <gcall *> (def), 0);
  if (TREE_CODE (arg) != INTEGER_CST
      || TREE_INT_CST_LOW (arg) < SFPU_CREG_IDX_LWM)
    return -1;
  return TREE_INT_CST_LOW (arg);
}

/* Return true if NAME is an SFPSWAP pair against a constant register of which
   only the other half is used.  SFPSWAP drops the write to the constant
   register, so the pair occupies a single LREG (rvtt.md
   *rvtt_sfpswap_cst1 and *rvtt_sfpswap_cst2; a VD constant must be below
   L12, or the instruction does not execute).  */

static bool
single_lreg_swap_p (tree name)
{
  gimple *def = SSA_NAME_DEF_STMT (name);
  const rvtt_insn_data *insnd = def ? rvtt_get_insn_data (def) : nullptr;
  if (!insnd || insnd->id != rvtt_insn_data::sfpswap)
    return false;
  gcall *call = as_a <gcall *> (def);
  int vd = creg_read_index (gimple_call_arg (call, 0));
  int vc = creg_read_index (gimple_call_arg (call, 1));
  unsigned creg_half;
  if (vc >= 0 && vd < 0)
    creg_half = 1;
  else if (vd >= 0 && vd < 12 && vc < 0)
    creg_half = 0;
  else
    return false;
  gimple *use;
  imm_use_iterator iter;
  FOR_EACH_IMM_USE_STMT (use, iter, name)
    {
      if (is_gimple_debug (use))
	continue;
      const rvtt_insn_data *used = rvtt_get_insn_data (use);
      if (!used || used->id != rvtt_insn_data::sfpselect2)
	return false;
      tree half = gimple_call_arg (as_a <gcall *> (use), 1);
      if (TREE_CODE (half) != INTEGER_CST
	  || TREE_INT_CST_LOW (half) == creg_half)
	return false;
    }
  return true;
}

static unsigned
lreg_width (tree name)
{
  switch (TYPE_MODE (TREE_TYPE (name)))
    {
    case E_XTT32SImode:
      return 1;
    case E_XTT64SImode:
      return single_lreg_swap_p (name) ? 1 : 2;
    case E_XTT128SImode:
      return 4;
    default:
      return SFPU_REG_NUM;
    }
}

/* Return true if NAME is a vector SSA value that will occupy an
   allocatable LREG.  Constant register reads (rvtt_sfpreadlreg with
   index >= SFPU_CREG_IDX_LWM) are folded into their consumers and are
   excluded.  */

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
   live out of the corresponding predecessor.  Counting follows
   engine_loop_legal_p.  Fill M's live-in sets and function-wide peak, and
   record in M->over_bbs every block whose peak exceeds CAPACITY.  */

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

/* Return an upper bound on the number of SFPU vector SSA values live at
   once in BB, for transformations that add live ranges within one block.
   Like engine_loop_legal_p, but for one block:
   - vector values defined outside BB and used inside it are live from block
     entry;
   - vector values with any use outside BB are pinned (never released);
   - vector values defined in a dominator of BB with a use outside their own
     defining block are counted as live through the block (they may span it
     without appearing in it);
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
/* Return true if the loop stays within the eight-LREG file after
   hoisting the candidates.  Every hoisted value is live across the loop,
   as is each vector PHI (a loop-carried value) and each vector value
   defined outside the loop that is used directly by a non-PHI statement
   in it.  */
static bool
engine_loop_legal_p (class loop *loop,
				 const auto_vec<gcall *> &loads,
				 bool report, bool cc_transients,
				 bool exempt_creg_reads)
{
  std::unordered_set<tree> candidates;
  /* A lowered 32-bit immediate is two SSA definitions but only one
     architectural value.  When its tail is pinned across the loop, the
     root is subsumed by the candidate: it is consumed immediately by the
     tail before loop entry and must neither be pinned independently nor
     counted as an unrelated outside-live value.  */
  std::unordered_set<tree> candidate_roots;
  std::unordered_set<tree> pinned;
  std::unordered_set<tree> live;
  std::unordered_map<tree, unsigned> remaining;

  /* A value defined by a read of a constant-register-file register
     (LReg[8..14]: the hardwired zero/one and the programmable constants)
     never occupies one of the eight allocatable LREGs -- every vector
     operand position accepts the constant register class directly
     (reg_or_cstlreg_operand).  Callers may ask to exclude those
     definitions from the count; by default they are counted.

     The exception is a LUT table-slot argument: the instruction reads the
     table registers implicitly, so a constant-register value feeding a
     slot must be copied into that LREG and is counted.  */
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
      if (gcall *root = rvtt_chained_loadi_root (call))
	candidate_roots.insert (gimple_call_lhs (root));
      pinned.insert (lhs);
      live.insert (lhs);
    }

  /* A vector can occupy an LREG throughout the loop without appearing in
     the loop at all: for example, a value read before the loop and stored
     after it.  Account for every vector SSA definition available at loop
     entry that has any non-debug use outside the loop.  This overcounts
     values used only before the loop.  */
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, cfun)
    {
      if (!VECTOR_TYPE_P (TREE_TYPE (name)) || candidates.count (name)
	  || candidate_roots.count (name))
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
	  /* With EXEMPT_CREG_READS, also exclude a value all of whose
	     non-debug uses execute before the loop (each use block is
	     outside the loop and dominates its header), such as a value
	     consumed by an SFPCONFIG write in the preheader.  */
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
     live through the walk.  For a multi-block body this measures pressure
     across the whole region, including any inner loops.  */
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
	    && !candidate_roots.count (lhs)
	    && !creg_resident_p (lhs))
	  live.insert (lhs);

	/* With CC_TRANSIENTS, count the LREG temporaries CC lowering
	   creates at this statement (see rvtt_pressure_cc_transient); a
	   hoisted value live across them competes for the same
	   registers.  */
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
	     "Invariant SFPU immediate hoist refused: loop LREG pressure "
	     "%zu exceeds %u\n",
	     peak, limit);
  return false;
}

/* Public queries.  */

void
rvtt_pressure_compute (function *fn, unsigned capacity,
		       rvtt_pressure_model *m)
{
  engine_compute_lreg_pressure (fn, capacity, m);
}

/* Return the register file capacity minus the function-wide peak
   pressure of FN; negative if the peak already exceeds it.  The model is
   recomputed on every call.  */

int
rvtt_pressure_residual (function *fn)
{
  rvtt_pressure_model model;
  rvtt_pressure_compute (fn, rvtt_pressure_capacity (), &model);
  return (int) rvtt_pressure_capacity () - (int) model.peak;
}

/* Return an upper bound on the number of vector values live at once in
   BB (see engine_bb_peak).  */

unsigned
rvtt_pressure_bb_peak (basic_block bb)
{
  return engine_bb_peak (bb);
}

/* Return the peak pressure between statements FIRST and LAST, i.e. over
   the points immediately before each statement after FIRST up to and
   including LAST, where a value kept alive from FIRST to LAST adds one
   live range.  This uses the function-wide liveness model of
   engine_compute_lreg_pressure rather than engine_bb_peak, which assumes
   every value used outside the block is live throughout it and can
   greatly overstate pressure in a loop body.  */

unsigned
rvtt_pressure_window_peak (gimple *first, gimple *last)
{
  basic_block bb = gimple_bb (first);
  gcc_assert (bb && gimple_bb (last) == bb);

  rvtt_pressure_model m;
  rvtt_pressure_compute (cfun, rvtt_pressure_capacity (), &m);

  /* Live-out set of BB.  */
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

/* Return true if keeping every load in LOADS live across LOOP keeps the
   loop's peak vector pressure within the LREG file.  REPORT enables the
   dump line on refusal; CC_TRANSIENTS counts the LREG temporaries of CC
   lowering; EXEMPT_CREG_READS excludes constant-register reads from the
   count.  */

bool
rvtt_pressure_loop_legal_p (class loop *loop,
			    const auto_vec<gcall *> &loads,
			    bool report, bool cc_transients,
			    bool exempt_creg_reads)
{
  bool ok = engine_loop_legal_p (loop, loads, report, cc_transients,
				 exempt_creg_reads);
  if (flag_checking)
    /* Check the LUT slot table on every checking run, not only when
       EXEMPT_CREG_READS is set.  */
    rvtt_pressure_validate_lut_slot_facts ();
  return ok;
}

/* Incremental loop query (rvtt_loop_pressure).

   The base profile is one run of the loop counting above with no
   candidates and EXEMPT_CREG_READS off: the count at each sample point
   (sample 0 is loop entry, then one per statement walked) and each name's
   half-open [start, end) interval of samples where it is counted.

   For a candidate set S,
     peak (S) = |S| + max over samples of (base - sum of S's intervals)
   because every candidate is live across the whole loop instead of over
   its own interval.  Candidates satisfy rvtt_invariant_constant_load_p
   (defined in the loop body, all non-debug uses in the loop, no vector
   arguments), so removing one does not change how any other name is
   counted.  Under flag_checking each answer is checked against the full
   computation.  */

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
      /* A name enters the live set at most once: values live before the
	 walk are never released and each name has one definition.  */
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

/* Return true if every load in CANDIDATES can be kept live across the
   loop, using the base profile: subtract each candidate's interval with a
   difference array, take the maximum over samples and add one register
   per candidate.  Under flag_checking the answer is checked against
   rvtt_pressure_loop_legal_p.  */

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
      /* Candidates are defined in the loop body, so the base walk
	 recorded an interval for them.  */
      gcc_assert (iv);
      m_delta[iv->start] -= 1;
      m_delta[iv->end == UINT_MAX ? n : iv->end] += 1;
      /* The root and tail of a lowered 32-bit load are one value: remove
	 both intervals and count one register below.  */
      if (gcall *root = rvtt_chained_loadi_root (call))
	{
	  live_interval *riv = m_interval.get (gimple_call_lhs (root));
	  gcc_assert (riv);
	  m_delta[riv->start] -= 1;
	  m_delta[riv->end == UINT_MAX ? n : riv->end] += 1;
	}
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
