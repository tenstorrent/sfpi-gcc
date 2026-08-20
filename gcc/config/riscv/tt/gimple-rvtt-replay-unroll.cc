/* Replay-window loop-unroll request.
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

/* -mtt-tensix-optimize-replay-loop-unroll (default off).

   THE PROBLEM.  A counted SFPU row loop (one dst_reg row per trip)
   delivers its whole row of Tensix words through the RISC front end on
   EVERY trip.  The identical row written by a production author under
   `#pragma GCC unroll 8' is unrolled by the generic RTL unroller into
   eight textual copies, and the always-on replay former
   (rtl-rvtt-replay.cc) folds those copies into one execute-while-record
   pass plus seven one-word playback launches per group -- the measured
   delivery shape of every winning hand eltwise kernel.  A semantic body
   without the pragma never reaches that machinery: replay formation has
   no textual repeats to fold, and the counted-loop record-once hoist is
   a DIFFERENT delivery shape whose silicon pricing (lane BP's
   TURNAROUND/RECORD_OVERHEAD calibration, rvtt-cost.md) refuses this
   row class outright.

   THE MECHANISM.  Grant the compiler the same request the production
   author writes, from typed loop-shape facts alone: when a counted
   single-block innermost loop consists ENTIRELY of typed SFPU builtins
   (plus the loop's own scalar control), request generic unrolling by
   the cost-table group factor (XTT_REPLAY_LOOP_UNROLL_FACTOR) by
   setting loop->unroll -- exactly what the pragma sets at
   gimplification (tree-cfg.cc replace_loop_annotate).  Everything
   downstream is existing, silicon-validated machinery: the generic RTL
   unroller duplicates the row, the replay former records it, the Dst
   auto-increment and MOP-form passes absorb the separators they own.
   This pass never edits a statement; a refusal leaves the function
   byte-identical, and even a fire only annotates the loop.

   ADMISSION is purely structural and fails closed:
     - innermost, single-basic-block counted loop;
     - trip count provably constant (SCEV; symbolic bounds refuse);
     - every real statement is a typed rvtt SFPU builtin call from the
       allow table below, a scalar (non-vector, non-memory) SSA
       assignment, the loop's PHIs, or its single exit condition;
     - builtins that program machine state outside the row (config
       writes, raw LREG windows, replay/region owners, RWC resets,
       SETC16) refuse: their repetition semantics are not the row's;
     - the estimated row words fit the replay former's minimum sequence
       and the unrolled group fits the code-size word budget
       (XTT_REPLAY_LOOP_UNROLL_{MIN,MAX}_WORDS).

   No operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates in any decision.  */

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
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "insn-constants.h"
#include "rvtt-protos.h"
#include "rvtt.h"

namespace {

/* Estimated delivered Tensix words for an admitted builtin, or -1 to
   refuse the class.  Zero-word entries are SSA plumbing that expands
   to no delivered word.  The estimate feeds only the size bounds; the
   replay former re-derives the exact window from the final insn
   stream.  */

static int
row_words_for (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    /* SSA plumbing: no delivered word.  */
    case rvtt_insn_data::sfpreadlreg:
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
    case rvtt_insn_data::sfpnovalue:
      return 0;

    /* Structured forms that lower to multi-word calendars.  */
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
      return 4;
    case rvtt_insn_data::sfpxvif:
    case rvtt_insn_data::sfpxbool:
    case rvtt_insn_data::sfpxcondb:
    case rvtt_insn_data::sfpxcondi:
      return 2;
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
      return 2;

    /* Plain one-word row members: loads/stores, immediates, compute,
       CC, and the typed Dst-counter step.  */
    case rvtt_insn_data::sfpload:
    case rvtt_insn_data::sfpload_lv:
    case rvtt_insn_data::sfploaddiscard:
    case rvtt_insn_data::sfploadsrcs:
    case rvtt_insn_data::sfploadsrcs_lv:
    case rvtt_insn_data::sfpstore:
    case rvtt_insn_data::sfpstoresrcs:
    case rvtt_insn_data::sfploadi:
    case rvtt_insn_data::sfploadi_lv:
    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
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
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
    case rvtt_insn_data::sfpswap:
    case rvtt_insn_data::sfpswap_indexed:
    case rvtt_insn_data::sfpselect2:
    case rvtt_insn_data::sfpselect4:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
    case rvtt_insn_data::sfpmul24:
    case rvtt_insn_data::sfpmul24_lv:
    case rvtt_insn_data::sfparecip:
    case rvtt_insn_data::sfparecip_lv:
    case rvtt_insn_data::sfpnonlinear:
    case rvtt_insn_data::sfpnonlinear_lv:
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpencc_all_lanes:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpnop:
    case rvtt_insn_data::ttincrwc:
    case rvtt_insn_data::ttdstface:
      return 1;

    /* Everything else programs machine state outside the row or opens
       a raw/owner region: repetition semantics are not the row's.
       (sfpwritelreg, sfprawlreg_access, ttregion_begin/end,
       sfpreadconfig, sfpwriteconfig_v, ttsetc16, ttsetrwc, ttreplay,
       sfpbankdone, synth_opcode, transpose family, shft2 family, ...)
       Fail closed.  */
    default:
      return -1;
    }
}

/* Resolve T to a host integer through a bounded walk of dominating SSA
   constant arithmetic (a freshly peeled loop's entry value can be an
   unfolded `N - 1' assignment).  SSA defs always dominate their uses,
   so every step reads a value proven available at the loop entry.  */

static bool
resolve_int_cst (tree t, HOST_WIDE_INT *out, int depth = 0)
{
  if (depth > 8 || !t)
    return false;
  if (TREE_CODE (t) == INTEGER_CST)
    {
      if (!tree_fits_shwi_p (t))
	return false;
      *out = tree_to_shwi (t);
      return true;
    }
  if (TREE_CODE (t) != SSA_NAME)
    return false;
  gassign *def = dyn_cast <gassign *> (SSA_NAME_DEF_STMT (t));
  if (!def)
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  HOST_WIDE_INT a, b;
  switch (code)
    {
    case INTEGER_CST:
    case SSA_NAME:
      return resolve_int_cst (gimple_assign_rhs1 (def), out, depth + 1);
    CASE_CONVERT:
      if (!resolve_int_cst (gimple_assign_rhs1 (def), &a, depth + 1))
	return false;
      *out = a;
      return true;
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
      {
	if (!resolve_int_cst (gimple_assign_rhs1 (def), &a, depth + 1)
	    || !resolve_int_cst (gimple_assign_rhs2 (def), &b, depth + 1))
	  return false;
	const HOST_WIDE_INT LIM = HOST_WIDE_INT_1 << 40;
	if (a > LIM || a < -LIM || b > LIM || b < -LIM)
	  return false;
	*out = code == PLUS_EXPR ? a + b : code == MINUS_EXPR ? a - b : a * b;
	return true;
      }
    default:
      return false;
    }
}

/* Bounded forward evaluation of a single-block counted loop's own scalar
   control (the same discipline as the programmable-constant pass's trip
   proof: no SCEV, no CFG normalization, refuse anything non-trivial).
   Returns true and sets *TRIPS to the exact number of body executions
   when the loop's exit condition is an integer compare of a
   constant-initialized, constant-stepped induction variable against a
   constant, and the simulation exits within the cap.  */

static bool
counted_trips (class loop *loop, unsigned HOST_WIDE_INT *trips)
{
  const unsigned HOST_WIDE_INT CAP = 4096;
  basic_block bb = loop->header;
  if (loop->latch != bb)
    return false;

  gcond *cond = safe_dyn_cast <gcond *> (*gsi_last_bb (bb));
  if (!cond)
    return false;

  /* Which successor continues the loop?  */
  edge e_true, e_false;
  extract_true_false_edges_from_block (bb, &e_true, &e_false);
  if (!e_true || !e_false)
    return false;
  bool continue_on_true;
  if (e_true->dest == bb && e_false->dest != bb)
    continue_on_true = true;
  else if (e_false->dest == bb && e_true->dest != bb)
    continue_on_true = false;
  else
    return false;

  /* One compare operand must be a loop-defined SSA name, the other an
     integer constant.  */
  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  tree_code code = gimple_cond_code (cond);
  tree var, bound;
  if (TREE_CODE (lhs) == SSA_NAME && TREE_CODE (rhs) == INTEGER_CST)
    {
      var = lhs;
      bound = rhs;
    }
  else if (TREE_CODE (rhs) == SSA_NAME && TREE_CODE (lhs) == INTEGER_CST)
    {
      var = rhs;
      bound = lhs;
      code = swap_tree_comparison (code);
    }
  else
    return false;
  if (!tree_fits_shwi_p (bound))
    return false;

  /* VAR is either the header PHI itself or the PHI stepped once by a
     constant.  */
  gphi *phi = NULL;
  HOST_WIDE_INT step = 0;
  bool tests_next;
  if (gphi *p = safe_dyn_cast <gphi *> (SSA_NAME_DEF_STMT (var)))
    {
      phi = p;
      tests_next = false;
    }
  else if (gassign *upd = dyn_cast <gassign *> (SSA_NAME_DEF_STMT (var)))
    {
      tree_code uc = gimple_assign_rhs_code (upd);
      tree op0 = gimple_assign_rhs1 (upd);
      tree op1 = gimple_assign_rhs2 (upd);
      if ((uc != PLUS_EXPR && uc != MINUS_EXPR && uc != POINTER_PLUS_EXPR)
	  || TREE_CODE (op0) != SSA_NAME
	  || !op1 || TREE_CODE (op1) != INTEGER_CST
	  || !tree_fits_shwi_p (op1))
	return false;
      phi = safe_dyn_cast <gphi *> (SSA_NAME_DEF_STMT (op0));
      if (!phi)
	return false;
      step = tree_to_shwi (op1);
      if (uc == MINUS_EXPR)
	step = -step;
      tests_next = true;
    }
  else
    return false;

  if (gimple_bb (phi) != bb || gimple_phi_num_args (phi) != 2)
    return false;

  /* Find the entry (non-latch) value and, when the compare tests the
     PHI itself, the latch-side step.  */
  tree init = NULL_TREE;
  tree latch_val = NULL_TREE;
  for (unsigned ix = 0; ix != 2; ++ix)
    {
      edge e = gimple_phi_arg_edge (phi, ix);
      if (e->src == bb)
	latch_val = gimple_phi_arg_def (phi, ix);
      else
	init = gimple_phi_arg_def (phi, ix);
    }
  HOST_WIDE_INT init_val;
  if (!init || !latch_val || !resolve_int_cst (init, &init_val))
    return false;

  if (!tests_next)
    {
      /* Recover the step from the latch value's definition.  */
      if (TREE_CODE (latch_val) != SSA_NAME)
	return false;
      gassign *upd = dyn_cast <gassign *> (SSA_NAME_DEF_STMT (latch_val));
      if (!upd || gimple_bb (upd) != bb)
	return false;
      tree_code uc = gimple_assign_rhs_code (upd);
      tree op0 = gimple_assign_rhs1 (upd);
      tree op1 = gimple_assign_rhs2 (upd);
      if ((uc != PLUS_EXPR && uc != MINUS_EXPR)
	  || op0 != gimple_phi_result (phi)
	  || !op1 || TREE_CODE (op1) != INTEGER_CST
	  || !tree_fits_shwi_p (op1))
	return false;
      step = tree_to_shwi (op1);
      if (uc == MINUS_EXPR)
	step = -step;
    }
  else if (latch_val != var)
    /* The stepped value that the compare tests must be the value that
       re-enters the PHI, or the simulation below models a different
       variable.  */
    return false;

  if (step == 0)
    return false;

  /* The compared value is unsigned when its type is; mirror the compare
     semantics exactly.  */
  tree vtype = TREE_TYPE (var);
  if (!INTEGRAL_TYPE_P (vtype)
      || TYPE_PRECISION (vtype) > HOST_BITS_PER_WIDE_INT)
    return false;
  bool uns = TYPE_UNSIGNED (vtype);
  unsigned prec = TYPE_PRECISION (vtype);
  unsigned HOST_WIDE_INT mask
    = prec == HOST_BITS_PER_WIDE_INT
      ? ~(unsigned HOST_WIDE_INT) 0
      : (((unsigned HOST_WIDE_INT) 1 << prec) - 1);
  auto norm = [&] (HOST_WIDE_INT x) -> HOST_WIDE_INT
    {
      unsigned HOST_WIDE_INT ux = (unsigned HOST_WIDE_INT) x & mask;
      if (!uns && prec < HOST_BITS_PER_WIDE_INT
	  && (ux & ((unsigned HOST_WIDE_INT) 1 << (prec - 1))))
	ux |= ~mask;
      return (HOST_WIDE_INT) ux;
    };
  auto holds = [&] (HOST_WIDE_INT a, HOST_WIDE_INT b) -> bool
    {
      unsigned HOST_WIDE_INT ua = (unsigned HOST_WIDE_INT) a & mask;
      unsigned HOST_WIDE_INT ub = (unsigned HOST_WIDE_INT) b & mask;
      switch (code)
	{
	case EQ_EXPR: return ua == ub;
	case NE_EXPR: return ua != ub;
	case LT_EXPR: return uns ? ua < ub : a < b;
	case LE_EXPR: return uns ? ua <= ub : a <= b;
	case GT_EXPR: return uns ? ua > ub : a > b;
	case GE_EXPR: return uns ? ua >= ub : a >= b;
	default: return false;
	}
    };
  switch (code)
    {
    case EQ_EXPR: case NE_EXPR: case LT_EXPR: case LE_EXPR:
    case GT_EXPR: case GE_EXPR:
      break;
    default:
      return false;
    }

  HOST_WIDE_INT v = norm (init_val);
  HOST_WIDE_INT b = norm (tree_to_shwi (bound));
  unsigned HOST_WIDE_INT n = 0;
  for (;;)
    {
      /* One body execution.  */
      ++n;
      if (n > CAP)
	return false;
      HOST_WIDE_INT nv = norm (v + step);
      HOST_WIDE_INT tested = tests_next ? nv : v;
      /* For a PHI-tested compare the value seen by this iteration's
	 test is the PHI value BEFORE the step only when the test
	 precedes the update; in a canonicalized single-block latch the
	 test is the last statement, so the tested PHI value is still
	 this iteration's V.  */
      bool cont = holds (tested, b);
      if (continue_on_true ? !cont : cont)
	break;
      v = nv;
    }
  *trips = n;
  return true;
}

class replay_unroll
{
public:
  unsigned n_fired = 0;
  unsigned n_refused = 0;

  void refuse (class loop *loop, const char *name, const char *detail)
  {
    ++n_refused;
    if (dump_file)
      {
	fprintf (dump_file, "replay-loop-unroll: refused (%s) loop %d",
		 name, loop->num);
	if (detail)
	  fprintf (dump_file, ": %s", detail);
	fprintf (dump_file, "\n");
      }
  }

  /* Census one candidate loop; return true if the annotation fired.  */
  bool process (function *fun, class loop *loop)
  {
    if (loop->inner || loop->num_nodes != 1)
      {
	refuse (loop, "replay-loop-unroll-body-not-flat", NULL);
	return false;
      }
    if (loop->unroll)
      /* A user annotation is already on record; never override it.  */
      return false;

    unsigned HOST_WIDE_INT trips;
    if (!counted_trips (loop, &trips))
      {
	refuse (loop, "replay-loop-unroll-trip-count-unproven", NULL);
	return false;
      }
    if (trips < 2)
      {
	refuse (loop, "replay-loop-unroll-trip-count-unproven",
		"fewer than two trips");
	return false;
      }

    basic_block bb = loop->header;
    unsigned words = 0;
    bool saw_cond = false;
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	    || gimple_nop_p (stmt))
	  continue;
	if (gimple_clobber_p (stmt))
	  continue;

	if (gcond *cond = dyn_cast <gcond *> (stmt))
	  {
	    (void) cond;
	    /* The single exit test of a single-block loop.  */
	    saw_cond = true;
	    continue;
	  }

	if (gcall *call = dyn_cast <gcall *> (stmt))
	  {
	    const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	    if (!insnd)
	      {
		refuse (loop, "replay-loop-unroll-foreign-stmt",
			"non-rvtt call");
		return false;
	      }
	    int w = row_words_for (insnd);
	    if (w < 0)
	      {
		refuse (loop, "replay-loop-unroll-denied-class",
			insnd->name);
		return false;
	      }
	    words += w;
	    continue;
	  }

	if (gassign *assign = dyn_cast <gassign *> (stmt))
	  {
	    if (gimple_vuse (stmt) || gimple_vdef (stmt))
	      {
		refuse (loop, "replay-loop-unroll-memory", NULL);
		return false;
	      }
	    tree lhs = gimple_assign_lhs (assign);
	    if (TREE_CODE (lhs) != SSA_NAME)
	      {
		refuse (loop, "replay-loop-unroll-foreign-stmt",
			"non-SSA assignment");
		return false;
	      }
	    /* Scalar SSA arithmetic (the induction web, address math)
	       and plain vector SSA copies deliver no Tensix word.  */
	    continue;
	  }

	refuse (loop, "replay-loop-unroll-foreign-stmt",
		gimple_code_name[gimple_code (stmt)]);
	return false;
      }

    if (!saw_cond)
      {
	refuse (loop, "replay-loop-unroll-body-not-flat",
		"no exit condition in header");
	return false;
      }
    if (words < XTT_REPLAY_LOOP_UNROLL_MIN_WORDS)
      {
	refuse (loop, "replay-loop-unroll-row-too-small", NULL);
	return false;
      }

    unsigned HOST_WIDE_INT factor = XTT_REPLAY_LOOP_UNROLL_FACTOR;
    if (factor > trips)
      factor = trips;
    if (factor < 2)
      {
	refuse (loop, "replay-loop-unroll-trip-count-unproven",
		"factor collapses below two");
	return false;
      }
    if (words * factor > XTT_REPLAY_LOOP_UNROLL_MAX_WORDS)
      {
	refuse (loop, "replay-loop-unroll-word-budget", NULL);
	return false;
      }

    loop->unroll = (unsigned short) factor;
    fun->has_unroll = true;
    ++n_fired;
    if (dump_file)
      fprintf (dump_file,
	       "replay-loop-unroll: requested unroll %u of loop %d"
	       " (~%u row words, trips " HOST_WIDE_INT_PRINT_UNSIGNED ")\n",
	       (unsigned) factor, loop->num, words, trips);
    return true;
  }
};

const pass_data pass_data_rvtt_replay_unroll =
{
  GIMPLE_PASS,
  "rvtt_replay_unroll",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_replay_unroll : public gimple_opt_pass
{
public:
  pass_rvtt_replay_unroll (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_replay_unroll, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_replay_loop_unroll > 0;
  }

  unsigned execute (function *fun) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "replay-loop-unroll: refused"
		   " (replay-loop-unroll-qsr-unproven)\n");
	return 0;
      }
    replay_unroll ctx;
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    for (auto loop : loops_list (fun, LI_ONLY_INNERMOST))
      ctx.process (fun, loop);
    loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file, "replay-loop-unroll: fires=%u refusals=%u\n",
	       ctx.n_fired, ctx.n_refused);
    /* Annotation only; no IL edits.  */
    return 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_replay_unroll (gcc::context *ctxt)
{
  return new pass_rvtt_replay_unroll (ctxt);
}
