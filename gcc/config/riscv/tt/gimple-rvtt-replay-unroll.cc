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
#include "tree-scalar-evolution.h"
#include "rvtt-protos.h"
#include "rvtt-trips.h"
#include "rvtt.h"

/* Estimated delivered Tensix words for an admitted builtin, or -1 to
   refuse the class.  Zero-word entries are SSA plumbing that expands
   to no delivered word.  The estimate feeds only the size bounds; the
   replay former re-derives the exact window from the final insn
   stream.  Exported (rvtt-protos.h): the delivery-shape solver pass
   shares this admission vocabulary so the two passes cannot drift.  */

int
rvtt_replay_unroll_row_words (const rvtt_insn_data *insnd)
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
       sfpreadconfig, sfpwriteconfig_v, sfpconfig_i, ttsetc16, ttsetrwc, ttreplay,
       sfpbankdone, synth_opcode, transpose family, shft2 family, ...)
       Fail closed.  */
    default:
      return -1;
    }
}

namespace {

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

} // anonymous namespace

/* Bounded forward evaluation of a single-block counted loop's own scalar
   control (the same discipline as the programmable-constant pass's trip
   proof: no SCEV, no CFG normalization, refuse anything non-trivial).
   Returns true and sets *TRIPS to the exact number of body executions
   when the loop's exit condition is an integer compare of a
   constant-initialized, constant-stepped induction variable against a
   constant, and the simulation exits within the cap.  Exported
   (rvtt-protos.h) as the stage-A LEGACY (deciding) oracle of the
   shared trip facade (rvtt_loop_trips_gimple, rvtt-trips.cc), through
   which this pass, the delivery-shape solver, and round-interleave
   all query so the admissions cannot drift.  */

bool
rvtt_replay_unroll_counted_trips (class loop *loop,
				  unsigned HOST_WIDE_INT *trips)
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

namespace {

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
    if (!rvtt_loop_trips_gimple (loop, &trips))
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
	    int w = rvtt_replay_unroll_row_words (insnd);
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

/* ---- Launch-flatten: complete-unroll request for delivery loops (lane HH) ----

   -mtt-tensix-optimize-launch-flatten (default off).

   THE PROBLEM.  A counted DELIVERY loop -- a body whose per-trip work is
   already record-plus-launch playback of user replay windows, or a fixed
   run of raw single-word Tensix instructions -- still drives its loop
   control through the RISC front end on every trip: the counter update
   and conditional branch ride the timed issue path between consecutive
   launches, and per-trip conditionals (a direction flip-flop selecting a
   config prologue, a record-once/launch-after init guard) cost a branch
   per trip even though every value they test is a proven function of the
   trip number.  The handwritten raw-word spelling of the SAME source
   unrolls completely at GIMPLE (its asm words estimate small), so the
   hand kernel issues a straight-line launch stream; a typed spelling of
   the identical windows inflates the size ESTIMATE (each one-word typed
   swap is more than a dozen GIMPLE statements), the complete unroller
   refuses on its size limits, and the loop-control words stay in the
   timed path -- the lane-HD topk replay-window-density gap.

   THE MECHANISM.  Grant the typed world the request the raw world gets
   from the size model: when a counted innermost loop's body consists
   entirely of delivery content plus its own scalar control, set
   loop->unroll to the proven trip count -- exactly what `#pragma GCC
   unroll TRIPS' sets -- placed BEFORE the GIMPLE complete unroller so
   cunroll both bypasses its size estimate (the pragma contract) and
   constant-folds every per-trip conditional at its proven value.  The
   transformation itself is the generic, unconditionally-sound complete
   unroll of a counted loop; this pass never edits a statement, and the
   dynamic word stream is unchanged by construction -- only the static
   spelling flattens, which is precisely what removes the per-trip
   loop-control words from the issue path and hands the replay former
   and the delivery passes the same flattened stream the hand world has
   always given them (no new window, record, or launch is created that
   the rolled world did not already deliver dynamically).

   ADMISSION is purely structural and fails closed (refusing by name):
     - innermost loop, no abnormal or EH edges, single exit;
     - trip count provably constant (SCEV latch count; symbolic refuses);
     - every non-debug statement in every body block is one of:
	 (a) a typed replay record or playback launch or another admitted
	     rvtt builtin (words from the shared replay-unroll table plus
	     the delivery additions below),
	 (b) a fixed raw `.ttinsn' asm word: no outputs, no clobbers, no
	     labels, constant-only inputs, single-word template,
	 (c) a computed-word delivery store: a VOLATILE store of a scalar
	     value (the LLK TT_ macro shape, `instrn_buffer[0] = word');
	     volatile loads refuse -- a spin-wait is not delivery,
	 (d) a scalar (non-memory) SSA assignment, PHI, or the loop's
	     conditionals;
     - at least one TYPED SFPU word is present (a body of owners, raw
       words, and computed-word stores only IS the raw-spelling world:
       its size pricing is already word-accurate, and the request must
       not grant raw code an unroll that pricing correctly refused --
       the topk_xl region-overflow/regression class);
     - the flattened total is bounded by the replay-unroll word budget
       (XTT_REPLAY_LOOP_UNROLL_MAX_WORDS: the same straight-line size
       class the row-group request already commits to), and a body
       below the row minimum (XTT_REPLAY_LOOP_UNROLL_MIN_WORDS) refuses:
       fewer delivered words per trip than that cannot price the two
       removed loop-control words against code growth.

   No operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates in any decision.  A user
   annotation (pragma) is never overridden.  QSR is refused wholesale,
   mirroring the row-request pass, until the QSR replay erratum
   machinery's interaction with flattened user records is audited.  */

/* Delivered-word estimate for the launch-flatten class: the shared row
   table, widened by the delivery spellings that class admits.  The
   estimate feeds only the size bounds; downstream passes re-derive
   exact windows from the final stream.  */

static int
launch_flatten_stmt_words (const rvtt_insn_data *insnd)
{
  int w = rvtt_replay_unroll_row_words (insnd);
  if (w >= 0)
    return w;
  switch (insnd->id)
    {
    /* A typed replay owner delivers exactly one TTREPLAY word, record
       and playback alike (the recorded payload's words are separate
       statements, counted on their own).  */
    case rvtt_insn_data::ttreplay:
      return 1;
    /* One-word typed spellings outside the row table.  */
    case rvtt_insn_data::ttsetrwc:
    case rvtt_insn_data::sfptransp:
    case rvtt_insn_data::sfptransp8:
      return 1;
    /* LReg-bank placement plumbing: no delivered word of its own (the
       moves it may force are register-allocation artifacts, not stream
       words this census can price).  */
    case rvtt_insn_data::sfpwritelreg:
      return 0;
    default:
      return -1;
    }
}

class launch_flatten
{
public:
  unsigned n_fired = 0;
  unsigned n_refused = 0;
  /* Accumulated estimated flattened words across this function's fires
     (words * trips per fired loop).  The per-loop budget bounds one
     loop's straight-line run; this bounds the FUNCTION: a vehicle that
     instantiates many admissible delivery loops (the topk_xl K=2048
     correctness TU) otherwise grows past the TRISC code region -- a
     loud link error, but a refusal-by-name is the honest form.  */
  unsigned HOST_WIDE_INT fn_words = 0;

  void refuse (class loop *loop, const char *name, const char *detail)
  {
    ++n_refused;
    if (dump_file)
      {
	fprintf (dump_file, "launch-flatten: refused (%s) loop %d",
		 name, loop->num);
	if (detail)
	  fprintf (dump_file, ": %s", detail);
	fprintf (dump_file, "\n");
      }
  }

  /* Census one innermost loop; return true if the annotation fired.  */
  bool process (function *fun, class loop *loop)
  {
    if (loop->unroll)
      /* A user annotation is already on record; never override it.  */
      return false;

    /* One exit: the proven trip count must govern every body block.  */
    if (!single_exit (loop))
      {
	refuse (loop, "launch-flatten-multi-exit", NULL);
	return false;
      }

    /* Proven constant trip count from the latch-execution chrec.  */
    tree niter = number_of_latch_executions (loop);
    if (!niter || TREE_CODE (niter) != INTEGER_CST
	|| !tree_fits_uhwi_p (niter))
      {
	refuse (loop, "launch-flatten-trip-count-unproven", NULL);
	return false;
      }
    unsigned HOST_WIDE_INT trips = tree_to_uhwi (niter) + 1;
    if (trips < 2)
      {
	refuse (loop, "launch-flatten-trip-count-unproven",
		"fewer than two trips");
	return false;
      }

    /* Body census over every block.  */
    unsigned words = 0;
    /* The request exists to give the TYPED spelling the flatten the raw
       spelling already gets from the size model: a typed SFPU word is a
       dozen-plus GIMPLE statements, so the estimate refuses loops the
       raw world unrolls.  A body with NO typed SFPU word (raw .ttinsn,
       computed-word stores, and replay owners only) IS the raw world --
       its size pricing is already word-accurate, and bypassing it
       grants raw code an unroll that pricing correctly refused (the
       topk_xl TRISC1_CODE overflow and its +3.8% e2e regression came
       exactly from such raw launch loops).  Require at least one typed
       SFPU word (the shared row table plus the transpose spellings;
       TTREPLAY/TTSETRWC owners and plumbing do not count -- the raw
       world spells those identically).  */
    bool typed_word_seen = false;
    basic_block *body = get_loop_body (loop);
    for (unsigned i = 0; i < loop->num_nodes; ++i)
      {
	basic_block bb = body[i];
	edge e;
	edge_iterator ei;
	FOR_EACH_EDGE (e, ei, bb->succs)
	  if (e->flags & (EDGE_ABNORMAL | EDGE_EH))
	    {
	      refuse (loop, "launch-flatten-abnormal-edge", NULL);
	      free (body);
	      return false;
	    }
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
		|| gimple_nop_p (stmt) || gimple_clobber_p (stmt))
	      continue;
	    if (gimple_code (stmt) == GIMPLE_COND)
	      /* The exit test or a per-trip conditional; the complete
		 unroller folds it at each trip's proven values.  */
	      continue;
	    if (gasm *a = dyn_cast <gasm *> (stmt))
	      {
		const char *s = gimple_asm_string (a);
		if (gimple_asm_noutputs (a) || gimple_asm_nclobbers (a)
		    || gimple_asm_nlabels (a)
		    || !s || !strstr (s, ".ttinsn")
		    || strchr (s, '\n') || strchr (s, ';'))
		  {
		    refuse (loop, "launch-flatten-foreign-asm", NULL);
		    free (body);
		    return false;
		  }
		bool ok = true;
		for (unsigned j = 0; j < gimple_asm_ninputs (a); ++j)
		  if (!is_gimple_min_invariant
		      (TREE_VALUE (gimple_asm_input_op (a, j))))
		    ok = false;
		if (!ok)
		  {
		    refuse (loop, "launch-flatten-foreign-asm",
			    "non-constant raw-word operand");
		    free (body);
		    return false;
		  }
		words += 1;
		continue;
	      }
	    if (gcall *call = dyn_cast <gcall *> (stmt))
	      {
		const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
		if (!insnd)
		  {
		    refuse (loop, "launch-flatten-foreign-stmt",
			    "non-rvtt call");
		    free (body);
		    return false;
		  }
		int w = launch_flatten_stmt_words (insnd);
		if (w < 0)
		  {
		    refuse (loop, "launch-flatten-unpriced-builtin",
			    insnd->name);
		    free (body);
		    return false;
		  }
		if (w > 0 && insnd->id != rvtt_insn_data::ttreplay
		    && insnd->id != rvtt_insn_data::ttsetrwc)
		  /* A typed SFPU word: the class the size model
		     over-prices (the raw world spells owners and
		     plumbing identically).  */
		  typed_word_seen = true;
		words += w;
		continue;
	      }
	    if (gassign *assign = dyn_cast <gassign *> (stmt))
	      {
		tree lhs = gimple_assign_lhs (assign);
		/* A VOLATILE store is the computed-word delivery spelling
		   (the LLK TT_ macros: `instrn_buffer[0] = word'): one
		   delivered word whose operand arithmetic is the scalar
		   SSA control already admitted above.  The rolled world
		   recomputes the word per trip; the flattened world folds
		   it to a constant, exactly as the raw-word arm's unroll
		   has always done.  Volatile LOADS stay refused (a
		   spin-wait or status read is not delivery).  */
		if (gimple_vdef (stmt) && !gimple_assign_load_p (assign)
		    && TREE_CODE (lhs) != SSA_NAME
		    && TREE_THIS_VOLATILE (lhs)
		    && gimple_assign_single_p (assign)
		    && is_gimple_val (gimple_assign_rhs1 (assign)))
		  {
		    words += 1;
		    continue;
		  }
		if (gimple_vuse (stmt) || gimple_vdef (stmt))
		  {
		    refuse (loop, "launch-flatten-memory", NULL);
		    free (body);
		    return false;
		  }
		if (TREE_CODE (lhs) != SSA_NAME)
		  {
		    refuse (loop, "launch-flatten-foreign-stmt",
			    "non-SSA assignment");
		    free (body);
		    return false;
		  }
		continue;
	      }
	    refuse (loop, "launch-flatten-foreign-stmt",
		    gimple_code_name[gimple_code (stmt)]);
	    free (body);
	    return false;
	  }
      }
    free (body);

    if (words < XTT_REPLAY_LOOP_UNROLL_MIN_WORDS)
      {
	refuse (loop, "launch-flatten-row-too-small", NULL);
	return false;
      }
    if ((unsigned HOST_WIDE_INT) words * trips
	> XTT_REPLAY_LOOP_UNROLL_MAX_WORDS)
      {
	refuse (loop, "launch-flatten-word-budget", NULL);
	return false;
      }
    if (fn_words + (unsigned HOST_WIDE_INT) words * trips
	> XTT_LAUNCH_FLATTEN_FN_BUDGET_WORDS)
      {
	refuse (loop, "launch-flatten-function-budget", NULL);
	return false;
      }
    if (!typed_word_seen)
      {
	refuse (loop, "launch-flatten-no-typed-content", NULL);
	return false;
      }
    /* loop->unroll is a narrow field; the word budget above already
       bounds trips far below its range, so this is belt only.  */
    if (trips > 1024)
      {
	refuse (loop, "launch-flatten-word-budget", "trip count");
	return false;
      }

    loop->unroll = (unsigned short) trips;
    fun->has_unroll = true;
    fn_words += (unsigned HOST_WIDE_INT) words * trips;
    ++n_fired;
    if (dump_file)
      fprintf (dump_file,
	       "launch-flatten: requested complete unroll of loop %d"
	       " (~%u delivery words/trip, trips "
	       HOST_WIDE_INT_PRINT_UNSIGNED ")\n",
	       loop->num, words, trips);
    return true;
  }
};

const pass_data pass_data_rvtt_launch_flatten =
{
  GIMPLE_PASS,
  "rvtt_launch_flatten",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_launch_flatten : public gimple_opt_pass
{
public:
  pass_rvtt_launch_flatten (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_launch_flatten, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_launch_flatten > 0;
  }

  unsigned execute (function *fun) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "launch-flatten: refused"
		   " (launch-flatten-qsr-unproven)\n");
	return 0;
      }
    /* This pass sits inside the GIMPLE loop pipeline (immediately before
       the complete unroller), where loops and SCEV are live; fall back
       to a local setup if it is ever scheduled outside it.  */
    bool own_loops = !current_loops;
    if (own_loops)
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    bool own_scev = !scev_initialized_p ();
    if (own_scev)
      scev_initialize ();
    launch_flatten ctx;
    for (auto loop : loops_list (fun, LI_ONLY_INNERMOST))
      ctx.process (fun, loop);
    if (own_scev)
      scev_finalize ();
    if (own_loops)
      loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file, "launch-flatten: fires=%u refusals=%u\n",
	       ctx.n_fired, ctx.n_refused);
    /* Annotation only; no IL edits.  */
    return 0;
  }
};

/* ---- Round-chain interleave: unroll-by-two request (lane EI) ----

   -mtt-tensix-optimize-round-interleave (default off).

   THE PROBLEM.  A counted round loop that is latency-bound carries the
   full result-latency stall of its dependence chain on every trip: two
   consecutive iterations' chains are ISOMORPHIC, and when the
   iterations are independent by dataflow the second chain is exactly
   the filler the first chain's stalls need (the dual-Horner interleave
   the list scheduler already performs on straight-line code) -- but a
   rolled loop never exposes the second copy, and the list scheduler
   defers self-loop rows by name.

   THE MECHANISM.  Two coupled increments behind one flag: (1) here,
   request generic unrolling by TWO (XTT_ROUND_INTERLEAVE_FACTOR) on a
   counted single-block innermost SFPU loop, under proof obligations
   that fail closed; (2) in rtl-rvtt-schedule.cc, the round-interleave
   cyclic extension lifts the self-loop deferral for exactly this
   shape, judging the reorder by the steady-state initiation interval
   of the wrapped (cyclic) dependence model with strict-decrease
   acceptance.

   PROOF OBLIGATIONS (refusing by name; annotation-only, so a refusal
   leaves the function byte-identical):
   - innermost single-block counted loop, trips proven by the same
     bounded forward evaluation as the replay-loop-unroll pass above
     and DIVISIBLE by two (the unrolled shape must stay a clean
     doubled-body loop);
   - every statement admitted by the replay-loop-unroll allow table
     (typed SFPU builtins plus the loop's own scalar control) -- the
     identical fail-closed census, including the denied config/raw/
     owner/replay classes whose repetition semantics are not the
     row's; word classes the post-RA scheduler treats as barriers
     (Dst traffic, CC writers, the next-slot-stall and
     unaudited-latency families, lane-state readers) additionally
     refuse round-interleave-body-barrier-class -- requesting the
     unroll there would double code the scheduler then refuses to
     touch;
   - ITERATION INDEPENDENCE: every loop-carried value (any header PHI
     that is not a proven induction variable) must have a recurrence
     circuit containing at most ONE word-delivering statement (a
     reduction-tail update; the scheduler's own dependence edges keep
     such updates in original order, so the interleave is a pure
     reorder and bit-exactness holds by construction).  A
     multi-statement recurrence circuit -- the Stein-round gcd/lcm
     class -- refuses round-interleave-dependent-recurrence:
     interleaving cannot overlap work each iteration serially consumes
     from its predecessor.  At least one word-delivering statement
     must sit OFF every circuit (the interleavable slack) or the same
     refusal fires;
   - PRESSURE: the interleaved doubled body must provably fit the
     8-LREG file.  Until the pre-RA pressure-scheduling gate (the
     pressure-sched lane) lands, the bound is deliberately
     conservative: peak(one body's vector live set) + peak(body-defined
     vector values only) <= 8 -- the union bound of one copy's full
     live set overlapping the sibling copy's private live set;
     exceeding it refuses round-interleave-pressure-exceeded.  (The
     measured lcm/gcd two-chain interleave needs 10 live registers and
     refuses here by design -- the honest current answer.)

   Never overrides a user annotation or a replay-loop-unroll request
   (this pass is registered after it and skips annotated loops).

   RENAMING PREREQUISITE: the RTL unroller duplicates the body on the
   SAME pseudos, and the allocator packs the copies' short lifetimes
   into the same LREGs -- a storage-induced false WAW/WAR recurrence
   that serializes the doubled row.  The cyclic scheduler therefore
   performs a region-scoped storage-collision rename (the lreg-rename
   pass's discipline, see ls_cyclic_rename_collisions in
   rtl-rvtt-schedule.cc) before judging the interleave, restoring the
   original registers exactly on refusal.

   No operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates in any decision.  */

/* Interleave admission for one word-delivering builtin: the subset of
   the replay-loop-unroll allow table whose expansions the post-RA list
   scheduler can actually reorder -- pure-LREG compute with no CC
   write, no Dst traffic, no RWC step, no config access, no next-slot
   acceptance stall, no unaudited-latency class (mirrors
   ls_admissible_p's fail-closed vocabulary in rtl-rvtt-schedule.cc;
   anything else in the row is an RTL barrier, so requesting the unroll
   would double code the scheduler then refuses to touch).  Returns
   true for admissible word classes, false to refuse by name.  */

static bool
interleave_word_class_p (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
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
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpmul24:
    case rvtt_insn_data::sfpmul24_lv:
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
    case rvtt_insn_data::sfploadi:
    case rvtt_insn_data::sfploadi_lv:
    case rvtt_insn_data::sfpxloadi:
      return true;
    /* Everything else the row table admits is a scheduler barrier
       class: Dst loads/stores and counter steps (dst-access/rwc-step),
       CC writers and structured-CC forms (cc-write), SFPSWAP
       (next-slot acceptance stall), LUT/ARECIP/NONLINEAR families
       (unaudited latency or effect-opaque), selects (lane-state
       readers).  Fail closed.  */
    default:
      return false;
    }
}

class round_interleave
{
public:
  unsigned n_fired = 0;
  unsigned n_refused = 0;

  void refuse (class loop *loop, const char *name, const char *detail)
  {
    ++n_refused;
    if (dump_file)
      {
	fprintf (dump_file, "round-interleave: refused (%s) loop %d",
		 name, loop->num);
	if (detail)
	  fprintf (dump_file, ": %s", detail);
	fprintf (dump_file, "\n");
      }
  }

  /* Is PHI a proven induction variable of its single-block loop: a
     scalar integral PHI whose latch value is the PHI stepped once by a
     constant inside the block?  */
  static bool
  induction_phi_p (gphi *phi, basic_block bb, edge latch_e)
  {
    tree res = gimple_phi_result (phi);
    if (!res || !INTEGRAL_TYPE_P (TREE_TYPE (res)))
      return false;
    tree lv = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
    if (TREE_CODE (lv) != SSA_NAME)
      return false;
    gassign *upd = dyn_cast <gassign *> (SSA_NAME_DEF_STMT (lv));
    if (!upd || gimple_bb (upd) != bb)
      return false;
    tree_code uc = gimple_assign_rhs_code (upd);
    return (uc == PLUS_EXPR || uc == MINUS_EXPR
	    || uc == POINTER_PLUS_EXPR)
	   && gimple_assign_rhs1 (upd) == res
	   && gimple_assign_rhs2 (upd)
	   && TREE_CODE (gimple_assign_rhs2 (upd)) == INTEGER_CST;
  }

  /* Forward closure: statements in BB reachable from SEED (an SSA name)
     through use-def chains.  */
  static void
  forward_closure (tree seed, basic_block bb, hash_set<gimple *> *out)
  {
    auto_vec<tree, 16> work;
    hash_set<tree> seen;
    work.safe_push (seed);
    seen.add (seed);
    while (!work.is_empty ())
      {
	tree name = work.pop ();
	gimple *use;
	imm_use_iterator it;
	FOR_EACH_IMM_USE_STMT (use, it, name)
	  {
	    if (is_gimple_debug (use) || gimple_bb (use) != bb
		|| gimple_code (use) == GIMPLE_PHI
		|| out->contains (use))
	      continue;
	    out->add (use);
	    tree lhs = gimple_get_lhs (use);
	    if (lhs && TREE_CODE (lhs) == SSA_NAME && !seen.contains (lhs))
	      {
		seen.add (lhs);
		work.safe_push (lhs);
	      }
	  }
      }
  }

  /* Backward closure: statements in BB feeding SEED (an SSA name)
     through operand chains, PHIs excluded (they terminate at the
     iteration boundary).  */
  static void
  backward_closure (tree seed, basic_block bb, hash_set<gimple *> *out)
  {
    auto_vec<tree, 16> work;
    hash_set<tree> seen;
    work.safe_push (seed);
    seen.add (seed);
    while (!work.is_empty ())
      {
	tree name = work.pop ();
	if (TREE_CODE (name) != SSA_NAME)
	  continue;
	gimple *def = SSA_NAME_DEF_STMT (name);
	if (!def || gimple_bb (def) != bb
	    || gimple_code (def) == GIMPLE_PHI || out->contains (def))
	  continue;
	out->add (def);
	ssa_op_iter it;
	tree op;
	FOR_EACH_SSA_TREE_OPERAND (op, def, it, SSA_OP_USE)
	  if (!seen.contains (op))
	    {
	      seen.add (op);
	      work.safe_push (op);
	    }
      }
  }

  /* Census one candidate loop; return true if the annotation fired.  */
  bool process (function *fun, class loop *loop)
  {
    if (loop->inner || loop->num_nodes != 1)
      {
	refuse (loop, "round-interleave-body-not-flat", NULL);
	return false;
      }
    if (loop->unroll)
      /* A user annotation or a replay-loop-unroll request is already
	 on record; never override it.  */
      return false;

    unsigned HOST_WIDE_INT trips;
    if (!rvtt_loop_trips_gimple (loop, &trips))
      {
	refuse (loop, "round-interleave-trip-count-unproven", NULL);
	return false;
      }
    if (trips < 2)
      {
	refuse (loop, "round-interleave-trip-count-unproven",
		"fewer than two trips");
	return false;
      }
    if (trips % XTT_ROUND_INTERLEAVE_FACTOR)
      {
	refuse (loop, "round-interleave-trips-not-divisible", NULL);
	return false;
      }

    basic_block bb = loop->header;
    edge latch_e = NULL;
    edge_iterator ei;
    edge e;
    FOR_EACH_EDGE (e, ei, bb->preds)
      if (e->src == bb)
	latch_e = e;
    if (!latch_e)
      {
	refuse (loop, "round-interleave-body-not-flat", "no self edge");
	return false;
      }

    /* Statement census: the replay-loop-unroll allow table, identical
       fail-closed vocabulary.  Word-delivering calls are collected for
       the circuit and slack analysis below.  */
    unsigned words = 0;
    bool saw_cond = false;
    auto_vec<gimple *, 32> word_stmts;
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	    || gimple_nop_p (stmt) || gimple_clobber_p (stmt))
	  continue;
	if (dyn_cast <gcond *> (stmt))
	  {
	    saw_cond = true;
	    continue;
	  }
	if (gcall *call = dyn_cast <gcall *> (stmt))
	  {
	    const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	    if (!insnd)
	      {
		refuse (loop, "round-interleave-foreign-stmt",
			"non-rvtt call");
		return false;
	      }
	    int w = rvtt_replay_unroll_row_words (insnd);
	    if (w < 0)
	      {
		refuse (loop, "round-interleave-denied-class", insnd->name);
		return false;
	      }
	    if (w > 0 && !interleave_word_class_p (insnd))
	      {
		refuse (loop, "round-interleave-body-barrier-class",
			insnd->name);
		return false;
	      }
	    words += w;
	    if (w > 0)
	      word_stmts.safe_push (stmt);
	    continue;
	  }
	if (gassign *assign = dyn_cast <gassign *> (stmt))
	  {
	    if (gimple_vuse (stmt) || gimple_vdef (stmt))
	      {
		refuse (loop, "round-interleave-memory", NULL);
		return false;
	      }
	    if (TREE_CODE (gimple_assign_lhs (assign)) != SSA_NAME)
	      {
		refuse (loop, "round-interleave-foreign-stmt",
			"non-SSA assignment");
		return false;
	      }
	    continue;
	  }
	refuse (loop, "round-interleave-foreign-stmt",
		gimple_code_name[gimple_code (stmt)]);
	return false;
      }
    if (!saw_cond)
      {
	refuse (loop, "round-interleave-body-not-flat",
		"no exit condition in header");
	return false;
      }
    if (words < XTT_ROUND_INTERLEAVE_MIN_WORDS)
      {
	refuse (loop, "round-interleave-row-too-small", NULL);
	return false;
      }
    if (words * XTT_ROUND_INTERLEAVE_FACTOR
	> XTT_ROUND_INTERLEAVE_MAX_WORDS)
      {
	refuse (loop, "round-interleave-word-budget", NULL);
	return false;
      }

    /* Iteration independence: every loop-carried PHI's recurrence
       circuit (statements both reachable from the PHI and feeding its
       latch value) must contain at most one word-delivering statement,
       and at least one word-delivering statement must sit off every
       circuit.  */
    hash_set<gimple *> circuit_union;
    for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	 gsi_next (&psi))
      {
	gphi *phi = psi.phi ();
	tree res = gimple_phi_result (phi);
	if (!res || virtual_operand_p (res))
	  continue;
	if (induction_phi_p (phi, bb, latch_e))
	  continue;
	tree lv = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
	hash_set<gimple *> fwd, back;
	forward_closure (res, bb, &fwd);
	if (TREE_CODE (lv) == SSA_NAME)
	  backward_closure (lv, bb, &back);
	unsigned circuit_words = 0;
	for (hash_set<gimple *>::iterator it = fwd.begin ();
	     it != fwd.end (); ++it)
	  if (back.contains (*it))
	    {
	      circuit_union.add (*it);
	      if (is_gimple_call (*it))
		{
		  const rvtt_insn_data *insnd
		    = rvtt_get_insn_data (as_a <gcall *> (*it));
		  if (insnd && rvtt_replay_unroll_row_words (insnd) > 0)
		    ++circuit_words;
		}
	    }
	if (circuit_words > 1)
	  {
	    refuse (loop, "round-interleave-dependent-recurrence",
		    "multi-statement recurrence circuit");
	    return false;
	  }
      }
    unsigned slack = 0;
    for (gimple *stmt : word_stmts)
      if (!circuit_union.contains (stmt))
	++slack;
    if (!slack)
      {
	refuse (loop, "round-interleave-dependent-recurrence",
		"no off-circuit work to interleave");
	return false;
      }

    /* Pressure: conservative union bound for the interleaved doubled
       body -- one copy's peak vector live set plus the sibling copy's
       peak of body-defined values.  Linear scan of the single block;
       a value used by a PHI latch argument or outside the loop stays
       live to the block end (the loop-carried/exposed class).  */
    {
      struct range { int def_pos; int last_use; bool inside_def; };
      hash_map<tree, range> ranges;
      int pos = 0;
      auto note_use = [&] (tree op, int at)
      {
	if (TREE_CODE (op) != SSA_NAME
	    || !VECTOR_TYPE_P (TREE_TYPE (op)))
	  return;
	gimple *def = SSA_NAME_DEF_STMT (op);
	bool inside = def && gimple_bb (def) == bb
		      && gimple_code (def) != GIMPLE_PHI;
	bool existed;
	range &r = ranges.get_or_insert (op, &existed);
	if (!existed)
	  {
	    r.def_pos = inside ? INT_MAX : 0;
	    r.last_use = at;
	    r.inside_def = inside;
	  }
	else if (at > r.last_use)
	  r.last_use = at;
	if (!inside)
	  /* Defined outside the loop (or by a PHI) and read in the body:
	     the next iteration reads it again, so it holds its LREG
	     through the backedge -- live for the whole body.  */
	  r.last_use = INT_MAX;
      };
      int n_pos = 0;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (is_gimple_debug (stmt))
	    continue;
	  ++n_pos;
	  ssa_op_iter it;
	  tree op;
	  FOR_EACH_SSA_TREE_OPERAND (op, stmt, it, SSA_OP_USE)
	    note_use (op, n_pos);
	  tree lhs = gimple_get_lhs (stmt);
	  if (lhs && TREE_CODE (lhs) == SSA_NAME
	      && VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    {
	      bool existed;
	      range &r = ranges.get_or_insert (lhs, &existed);
	      r.def_pos = n_pos;
	      r.inside_def = true;
	      if (!existed)
		r.last_use = n_pos;
	    }
	}
      pos = n_pos;
      /* PHI results are live from position 0; PHI latch arguments and
	 names used outside the loop are live to the end.  */
      for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
	   gsi_next (&psi))
	{
	  gphi *phi = psi.phi ();
	  tree res = gimple_phi_result (phi);
	  if (res && !virtual_operand_p (res)
	      && VECTOR_TYPE_P (TREE_TYPE (res)))
	    {
	      bool existed;
	      range &r = ranges.get_or_insert (res, &existed);
	      r.def_pos = 0;
	      r.inside_def = false;
	      if (!existed)
		r.last_use = 0;
	    }
	  tree lv = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
	  if (TREE_CODE (lv) == SSA_NAME
	      && VECTOR_TYPE_P (TREE_TYPE (lv)))
	    {
	      bool existed;
	      range &r = ranges.get_or_insert (lv, &existed);
	      if (!existed)
		{
		  gimple *def = SSA_NAME_DEF_STMT (lv);
		  r.inside_def = def && gimple_bb (def) == bb
				 && gimple_code (def) != GIMPLE_PHI;
		  r.def_pos = r.inside_def ? INT_MAX : 0;
		}
	      r.last_use = pos + 1;
	    }
	}
      for (hash_map<tree, range>::iterator it = ranges.begin ();
	   it != ranges.end (); ++it)
	{
	  tree name = (*it).first;
	  range &r = (*it).second;
	  gimple *use;
	  imm_use_iterator ui;
	  bool outside = false;
	  FOR_EACH_IMM_USE_STMT (use, ui, name)
	    if (!is_gimple_debug (use)
		&& (!gimple_bb (use) || gimple_bb (use) != bb))
	      {
		outside = true;
		break;
	      }
	  if (outside)
	    r.last_use = pos + 1;
	  if (r.def_pos == INT_MAX)
	    /* Used before any def we saw: defined by a PHI or outside;
	       treat as live from entry.  */
	    r.def_pos = 0;
	}
      unsigned peak_total = 0, peak_inside = 0;
      for (int p = 0; p <= pos + 1; ++p)
	{
	  unsigned total = 0, inside = 0;
	  for (hash_map<tree, range>::iterator it = ranges.begin ();
	       it != ranges.end (); ++it)
	    {
	      range &r = (*it).second;
	      if (r.def_pos <= p && p <= r.last_use)
		{
		  ++total;
		  if (r.inside_def)
		    ++inside;
		}
	    }
	  peak_total = MAX (peak_total, total);
	  peak_inside = MAX (peak_inside, inside);
	}
      if (peak_total + peak_inside > 8)
	{
	  if (dump_file)
	    fprintf (dump_file, "round-interleave: doubled-body bound "
		     "%u+%u exceeds the 8-LREG file\n",
		     peak_total, peak_inside);
	  refuse (loop, "round-interleave-pressure-exceeded", NULL);
	  return false;
	}
      if (dump_file)
	fprintf (dump_file, "round-interleave: pressure bound %u+%u "
		 "within the 8-LREG file\n", peak_total, peak_inside);
    }

    loop->unroll = (unsigned short) XTT_ROUND_INTERLEAVE_FACTOR;
    fun->has_unroll = true;
    ++n_fired;
    if (dump_file)
      fprintf (dump_file,
	       "round-interleave: requested unroll %u of loop %d"
	       " (~%u row words, slack %u, trips "
	       HOST_WIDE_INT_PRINT_UNSIGNED ")\n",
	       (unsigned) XTT_ROUND_INTERLEAVE_FACTOR, loop->num,
	       words, slack, trips);
    return true;
  }
};

const pass_data pass_data_rvtt_round_interleave =
{
  GIMPLE_PASS,
  "rvtt_round_interleave",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_round_interleave : public gimple_opt_pass
{
public:
  pass_rvtt_round_interleave (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_round_interleave, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_round_interleave > 0;
  }

  unsigned execute (function *fun) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "round-interleave: refused"
		   " (round-interleave-qsr-unproven)\n");
	return 0;
      }
    round_interleave ctx;
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    for (auto loop : loops_list (fun, LI_ONLY_INNERMOST))
      ctx.process (fun, loop);
    loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file, "round-interleave: fires=%u refusals=%u\n",
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

gimple_opt_pass *
make_pass_rvtt_launch_flatten (gcc::context *ctxt)
{
  return new pass_rvtt_launch_flatten (ctxt);
}

gimple_opt_pass *
make_pass_rvtt_round_interleave (gcc::context *ctxt)
{
  return new pass_rvtt_round_interleave (ctxt);
}
