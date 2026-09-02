/* Tensix counted-loop trip-count facade (dual-oracle, stage A).
   Copyright (C) 2022-2026 Tenstorrent Inc.

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

/* One shared entry point for the
   constant-trip-count proof of counted single-block Tensix loops,
   re-founded on the classical in-tree analyses (RTL loop-iv's
   get_simple_loop_desc; GIMPLE SCEV's number_of_latch_executions)
   behind a dual-oracle facade.

   THE TWO ORACLES.  The LEGACY oracle is the bounded forward
   simulation the replay formation family has always used (moved here
   verbatim from rtl-rvtt-replay.cc): pattern-match a single
   `reg = reg + const' counter step, prove constant init and bound
   through the dedicated-preheader chain, then evaluate the counter
   directly -- wrapping at the register mode's precision -- until the
   continue condition first fails (TRIP_BOUND cap).  The CLASSICAL
   oracle is the symbolic niter computation upstream already ships:
   loop-iv at RTL, SCEV at GIMPLE, mapped to body executions as
   latch executions + 1 (the mapping the launch-flatten pass already
   uses in-tree).

   STAGE-A CONTRACT (CLASS-I).  The legacy oracle DECIDES: callers
   receive its verdict and outputs byte-identically to the pre-facade
   passes.  The classical oracle runs as a cross-check only.  Where
   both prove, the verdicts must agree; a disagreement is dumped under
   the diagnostic name `trip-oracle-divergence' and is a P1 -- the
   corpus census of that name must be EMPTY before stage B may flip
   the deciding oracle to the classical analysis.  Wrap-around
   counters -- the reason the forward evaluation was defensible -- are
   the class the divergence check exists to referee: loop-iv models
   modular arithmetic in the IV's mode, but the check is the proof,
   not an argument.  One-sided proofs are dumped as
   `trip-oracle-legacy-only' (a classical blind spot to close before
   any consumer switches oracles) and `trip-oracle-classical-only' (the
   widening class: shapes the classical analysis proves that the legacy
   matcher refuses).  Agreement is dumped as `trip-oracle-agree'.

   THE PREHEADER OBSTACLE, HANDLED.  Both the RTL and the GIMPLE
   consumers initialize loops with AVOID_CFG_MODIFICATIONS only
   (refusal paths must not mutate the CFG), so the classical entry
   points' asserted invariants (loop_preheader_edge's
   LOOPS_HAVE_PREHEADERS / no-multiple-latches; scev_initialize's
   LOOPS_NORMAL; single_exit's recorded exit lists) do not hold
   globally.  The facade queries the classical engines only for loops
   whose structure it has verified locally (single-block self-latch
   loop, exactly two header predecessors, the non-latch entry a
   dedicated preheader), flips the loops-state flags around the query
   -- sound because on this query path the flags gate assertions about
   exactly the structure just verified -- and at GIMPLE hands the
   structurally derived exit edge to number_of_iterations_exit (the
   SCEV niter engine; its number_of_latch_executions wrapper is
   unusable here because it consults the unrecorded exit lists and
   caches into the shared loop).  The engines' own dump chatter (df
   region dumps, `Loop N is simple') is suppressed for the duration of
   the query so the census lines below are the facade's single
   spelling in pass dumps.

   FINDING (banked for the oracle-switch stage): RTL loop-iv refuses
   HARD registers categorically (simple_reg_p, loop-iv.cc
   HARD_REGISTER_NUM_P arm), and the replay formation family runs
   post-reload where every counter is a hard register -- so the
   classical RTL oracle can never prove at this pipeline position and
   the RTL face of the census is expected to read 100%
   trip-oracle-legacy-only (pinned by the trips-facade-agree twin).
   Stage B therefore CANNOT flip the RTL deciding oracle to loop-iv
   in place: it needs the GIMPLE-side proof carried across expand, a
   pre-RA proof point, or upstream hard-register support in loop-iv.
   The GIMPLE face cross-checks for real (agreement pinned by the same
   twin), so the divergence census is live where the classical engine
   can speak.

   TESTING KNOB.  -mtt-tensix-trips-oracle-skew=N adds N trips to a
   proven classical verdict to manufacture a divergence: the twin for
   the `trip-oracle-divergence' diagnostic proves the name fires and
   the legacy verdict still decides.  Init(0); never set outside
   tests.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "tree-scalar-evolution.h"
#include "tree-ssa-loop.h"
#include "tree-ssa-loop-niter.h"
#include "rvtt-protos.h"
#include "rvtt-trips.h"

/* ====================  LEGACY ORACLE (deciding)  ====================

   Moved verbatim from rtl-rvtt-replay.cc.  Provable constant trip
   counts by a cycle-safe constant-chain evaluation keyed to the
   pass's own dedicated-preheader proof:

   - the loop is a single basic block ending in a two-way conditional jump;
   - exactly one comparison operand is a counter register with exactly one
     in-loop modification, a reg = reg + const step;
   - the other comparison operand is a constant, either immediate or a
     register with no in-loop modification whose last definition on the
     unique dedicated-preheader path is a constant load;
   - the counter's own last definition on that path is a constant load;
   - iteration is then evaluated directly, wrapping at the register mode's
     precision, until the continue condition first fails.

   Anything else -- including a merely estimated profile count -- is an
   unknown trip count and refuses.  This is pure structural RTL/dataflow
   matching; no operation identity, opcode calendar, coefficient pattern,
   or instruction-word fingerprint participates.  */

// Walk backwards from the end of PREHEADER through the unique-predecessor
// chain looking for the last definition of REG.  Return true and set *VALUE
// if that definition is a simple constant load; refuse on any other
// definition or on a call (potential clobber).  Only a unique predecessor is
// followed, so this remains a reaching-definition proof rather than a guess;
// the visited set makes malformed or cyclic predecessor chains fail closed
// without making the result depend on the number of harmless CFG splits.
bool
rvtt_constant_reaching_value (basic_block preheader, rtx reg, uint64_t *value)
{
  basic_block bb = preheader;
  auto_bitmap visited;
  while (bitmap_set_bit (visited, bb->index))
    {
      rtx_insn *insn;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn))
	    return false;
	  if (!reg_set_p (reg, insn))
	    continue;
	  rtx set = single_set (insn);
	  if (!set || !REG_P (SET_DEST (set))
	      || REGNO (SET_DEST (set)) != REGNO (reg)
	      || !CONST_INT_P (SET_SRC (set)))
	    return false;
	  *value = UINTVAL (SET_SRC (set));
	  return true;
	}
      if (!single_pred_p (bb))
	return false;
      bb = single_pred (bb);
    }
  return false;
}

// Evaluate an integer condition CODE on VAL0, VAL1, both already reduced to
// PREC-bit values.  Signed comparisons sign-extend from PREC.
static bool
eval_int_condition (rtx_code code, uint64_t val0, uint64_t val1,
		    unsigned prec)
{
  int64_t s0 = val0, s1 = val1;
  if (prec < 64)
    {
      uint64_t sign = uint64_t (1) << (prec - 1);
      s0 = int64_t ((val0 ^ sign) - sign);
      s1 = int64_t ((val1 ^ sign) - sign);
    }
  switch (code)
    {
    case EQ: return val0 == val1;
    case NE: return val0 != val1;
    case LT: return s0 < s1;
    case LE: return s0 <= s1;
    case GT: return s0 > s1;
    case GE: return s0 >= s1;
    case LTU: return val0 < val1;
    case LEU: return val0 <= val1;
    case GTU: return val0 > val1;
    case GEU: return val0 >= val1;
    default: return false;
    }
}

// Prove the constant trip count of single-block LOOP whose dedicated
// preheader is PREHEADER.  Return true and set *TRIPS (number of times the
// loop body executes) on success; any structural mismatch refuses.  On
// success the optional outputs receive the loop's single counter-step insn
// (*STEP_OUT) and the counter's proven value at loop exit (*FINAL_OUT,
// reduced to the counter mode's precision) -- the launch-loop unroll
// consumes them to replace the removed per-trip updates.
static bool
legacy_constant_trips (class loop *loop, basic_block preheader,
		       uint64_t *trips, rtx_insn **step_out,
		       uint64_t *final_out)
{
  basic_block header = loop->header;
  rtx_insn *jump = BB_END (header);
  if (!JUMP_P (jump) || !any_condjump_p (jump) || !onlyjump_p (jump)
      || EDGE_COUNT (header->succs) != 2)
    return false;

  edge e_branch = BRANCH_EDGE (header);
  edge e_fall = FALLTHRU_EDGE (header);
  bool taken_continues;
  if (e_branch->dest == header && e_fall->dest != header)
    taken_continues = true;
  else if (e_fall->dest == header && e_branch->dest != header)
    taken_continues = false;
  else
    return false;

  rtx set = pc_set (jump);
  if (!set)
    return false;
  rtx src = SET_SRC (set);
  if (GET_CODE (src) != IF_THEN_ELSE)
    return false;
  rtx cond = XEXP (src, 0);
  if (!COMPARISON_P (cond))
    return false;
  // Branch taken when the condition holds, unless the label is in the
  // else arm.
  bool taken_when_true = GET_CODE (XEXP (src, 1)) != PC;

  rtx op0 = XEXP (cond, 0);
  rtx op1 = XEXP (cond, 1);

  // Identify the counter operand: a hard register with exactly one in-loop
  // modification of the form reg = reg + const.
  rtx counter = nullptr, bound = nullptr;
  rtx_insn *step_insn = nullptr;
  for (int side = 0; side != 2; ++side)
    {
      rtx cand = side ? op1 : op0;
      if (!REG_P (cand))
	continue;
      rtx_insn *insn;
      rtx_insn *found = nullptr;
      bool bad = false;
      FOR_BB_INSNS (header, insn)
	if (NONDEBUG_INSN_P (insn) && insn != jump
	    && reg_set_p (cand, insn))
	  {
	    if (found)
	      bad = true;
	    found = insn;
	  }
      if (bad)
	return false;
      if (found)
	{
	  if (counter)
	    // Both operands are modified in the loop.
	    return false;
	  counter = cand;
	  bound = side ? op0 : op1;
	  step_insn = found;
	}
    }
  if (!counter)
    return false;

  rtx step_set = single_set (step_insn);
  if (!step_set || !REG_P (SET_DEST (step_set))
      || REGNO (SET_DEST (step_set)) != REGNO (counter)
      || GET_CODE (SET_SRC (step_set)) != PLUS
      || !REG_P (XEXP (SET_SRC (step_set), 0))
      || REGNO (XEXP (SET_SRC (step_set), 0)) != REGNO (counter)
      || !CONST_INT_P (XEXP (SET_SRC (step_set), 1)))
    return false;
  uint64_t step = UINTVAL (XEXP (SET_SRC (step_set), 1));

  scalar_int_mode mode;
  if (!is_a<scalar_int_mode> (GET_MODE (counter), &mode)
      || GET_MODE_PRECISION (mode) > 64)
    return false;
  unsigned prec = GET_MODE_PRECISION (mode);
  uint64_t mask = prec == 64 ? ~uint64_t (0)
    : (uint64_t (1) << prec) - 1;

  uint64_t init;
  if (!rvtt_constant_reaching_value (preheader, counter, &init))
    return false;

  uint64_t bound_val;
  if (CONST_INT_P (bound))
    bound_val = UINTVAL (bound);
  else if (REG_P (bound))
    {
      // The bound must be loop-invariant with a provable constant value.
      rtx_insn *insn;
      FOR_BB_INSNS (header, insn)
	if (NONDEBUG_INSN_P (insn) && insn != jump
	    && reg_set_p (bound, insn))
	  return false;
      if (!rvtt_constant_reaching_value (preheader, bound, &bound_val))
	return false;
    }
  else
    return false;
  bound_val &= mask;

  // Directly evaluate the counter chain, wrapping at the mode precision,
  // until the continue condition first fails.
  bool counter_is_op0 = rtx_equal_p (counter, op0);
  uint64_t c = init & mask;
  constexpr uint64_t TRIP_BOUND = uint64_t (1) << 16;
  for (uint64_t t = 1; t <= TRIP_BOUND; ++t)
    {
      c = (c + step) & mask;
      uint64_t v0 = counter_is_op0 ? c : bound_val;
      uint64_t v1 = counter_is_op0 ? bound_val : c;
      bool cond_holds = eval_int_condition (GET_CODE (cond), v0, v1, prec);
      bool taken = cond_holds == taken_when_true;
      bool continues = taken == taken_continues;
      if (!continues)
	{
	  *trips = t;
	  if (step_out)
	    *step_out = step_insn;
	  if (final_out)
	    *final_out = c;
	  return true;
	}
    }
  return false;
}

/* ==================  CLASSICAL ORACLE (cross-check)  ================== */

/* RTL: symbolic niter through loop-iv (get_simple_loop_desc).  Return
   true and set *TRIPS (body executions = latch executions + 1) when
   the analysis proves an unconditionally constant count.  Fail-closed:
   any assumption, no-loop condition, possible infiniteness, or
   structural mismatch with the verified dedicated-preheader shape
   refuses.  KNOWN BLIND POST-RELOAD: loop-iv's simple_reg_p refuses
   hard registers, so at the replay passes' position this oracle
   currently proves nothing (see the STAGE-A FINDING above); it is
   kept wired so the census stays honest and comes alive the moment a
   usable RTL engine exists.  */

static bool
classical_rtl_trips (class loop *loop, basic_block preheader,
		     uint64_t *trips)
{
  /* Verify locally the structure whose absence the loop-iv helpers
     would otherwise assert on: a single-block self-latch loop with
     exactly two header predecessors, entered from the caller's proven
     dedicated preheader.  */
  basic_block header = loop->header;
  if (!loop->latch || loop->latch != header || loop->num_nodes != 1)
    return false;
  if (EDGE_COUNT (header->preds) != 2)
    return false;
  edge entry = nullptr;
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, header->preds)
    if (e->src != header)
      entry = e;
  if (!entry || entry->src != preheader || (entry->flags & EDGE_ABNORMAL)
      || !single_succ_p (preheader))
    return false;

  /* The flags gate assertions about exactly the structure verified
     above; flip them for the duration of the query.  Silence the
     engines' own dump chatter: the facade's census lines are the one
     spelling this analysis has in pass dumps.  */
  bool flip_ph = !loops_state_satisfies_p (LOOPS_HAVE_PREHEADERS);
  bool flip_ml = loops_state_satisfies_p (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  if (flip_ph)
    loops_state_set (LOOPS_HAVE_PREHEADERS);
  if (flip_ml)
    loops_state_clear (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  FILE *saved_dump = dump_file;
  dump_file = nullptr;

  class niter_desc *desc = get_simple_loop_desc (loop);
  bool ok = desc && desc->simple_p && desc->const_iter
    && !desc->assumptions && !desc->noloop_assumptions && !desc->infinite
    && desc->niter != ~uint64_t (0);
  uint64_t n = ok ? desc->niter + 1 : 0;
  free_simple_loop_desc (loop);
  iv_analysis_done ();

  dump_file = saved_dump;
  if (flip_ml)
    loops_state_set (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  if (flip_ph)
    loops_state_clear (LOOPS_HAVE_PREHEADERS);

  if (!ok)
    return false;
  *trips = n;
  return true;
}

/* GIMPLE: symbolic niter through the SCEV engine
   (number_of_iterations_exit on the structurally derived exit edge).
   Return true and set *TRIPS (body executions = latch executions + 1,
   the launch-flatten mapping) when the niter folds to an
   unconditional constant (assumptions boolean_true, no may_be_zero
   residue).  */

static bool
classical_gimple_trips (class loop *loop, unsigned HOST_WIDE_INT *trips)
{
  /* Verify locally the structure the SCEV entry points would
     otherwise assert about (loop_preheader_edge): a single-block
     self-latch loop entered from a dedicated preheader.  This is also
     exactly the legacy oracle's admission domain, so the cross-check
     covers the loops the verdict can matter for.  */
  basic_block header = loop->header;
  if (!loop->latch || loop->latch != header || loop->num_nodes != 1)
    return false;
  if (EDGE_COUNT (header->preds) != 2)
    return false;
  edge entry = nullptr;
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, header->preds)
    if (e->src != header)
      entry = e;
  if (!entry || (entry->flags & EDGE_ABNORMAL)
      || !single_succ_p (entry->src))
    return false;
  /* The loop's one exit, derived structurally (these passes run with
     AVOID_CFG_MODIFICATIONS loops whose exit lists are not recorded,
     so single_exit is unanswerable; number_of_iterations_exit takes
     the edge directly and consults no exit list).  */
  if (EDGE_COUNT (header->succs) != 2)
    return false;
  edge exit = nullptr;
  FOR_EACH_EDGE (e, ei, header->succs)
    if (e->dest != header)
      exit = e;
  if (!exit || (exit->flags & (EDGE_ABNORMAL | EDGE_COMPLEX)))
    return false;

  /* scev_initialize asserts LOOPS_NORMAL and loop_preheader_edge
     asserts preheaders/single latches; these passes run
     AVOID_CFG_MODIFICATIONS loops, so the flags do not hold globally.
     Every property those assertions guard has just been verified
     structurally for THIS loop (dedicated preheader, unique self
     latch, reducible single-block body), and this query path consults
     the flags for assertions only -- flip them for the duration.  */
  unsigned to_set = 0;
  if (!loops_state_satisfies_p (LOOPS_HAVE_PREHEADERS))
    to_set |= LOOPS_HAVE_PREHEADERS;
  if (!loops_state_satisfies_p (LOOPS_HAVE_SIMPLE_LATCHES))
    to_set |= LOOPS_HAVE_SIMPLE_LATCHES;
  if (!loops_state_satisfies_p (LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS))
    to_set |= LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS;
  bool flip_ml = loops_state_satisfies_p (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  if (to_set)
    loops_state_set (to_set);
  if (flip_ml)
    loops_state_clear (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  FILE *saved_dump = dump_file;
  dump_file = nullptr;
  bool own_scev = !scev_initialized_p ();
  if (own_scev)
    scev_initialize ();

  class tree_niter_desc nd;
  bool ok = number_of_iterations_exit (loop, exit, &nd, false)
    && nd.niter && TREE_CODE (nd.niter) == INTEGER_CST
    && tree_fits_uhwi_p (nd.niter)
    && nd.assumptions && integer_onep (nd.assumptions)
    && (!nd.may_be_zero || integer_zerop (nd.may_be_zero));
  unsigned HOST_WIDE_INT n = ok ? tree_to_uhwi (nd.niter) + 1 : 0;

  if (own_scev)
    scev_finalize ();
  dump_file = saved_dump;
  if (flip_ml)
    loops_state_set (LOOPS_MAY_HAVE_MULTIPLE_LATCHES);
  if (to_set)
    loops_state_clear (to_set);

  if (!ok || n == 0)
    return false;
  *trips = n;
  return true;
}

/* =====================  DUAL-ORACLE FACADE  ===================== */

/* Dump one census fact for the completed dual query on LOOP.  FACE is
   "rtl" or "gimple".  Every line is greppable by its diagnostic name;
   `trip-oracle-divergence' is the P1 the stage-A corpus census must
   prove absent.  */

static void
report_oracles (const char *face, class loop *loop,
		bool legacy, uint64_t legacy_trips,
		bool classical, uint64_t classical_trips)
{
  if (!dump_file)
    return;
  if (legacy && classical)
    {
      if (legacy_trips == classical_trips)
	fprintf (dump_file,
		 "rvtt-trips: trip-oracle-agree loop %d n=%" PRIu64
		 " (%s)\n", loop->num, legacy_trips, face);
      else
	fprintf (dump_file,
		 "rvtt-trips: trip-oracle-divergence loop %d legacy=%" PRIu64
		 " classical=%" PRIu64 " (%s) -- legacy verdict kept\n",
		 loop->num, legacy_trips, classical_trips, face);
    }
  else if (legacy)
    fprintf (dump_file,
	     "rvtt-trips: trip-oracle-legacy-only loop %d n=%" PRIu64
	     " (%s)\n", loop->num, legacy_trips, face);
  else if (classical)
    fprintf (dump_file,
	     "rvtt-trips: trip-oracle-classical-only loop %d n=%" PRIu64
	     " (%s)\n", loop->num, classical_trips, face);
}

bool
rvtt_loop_trips (class loop *loop, basic_block preheader, uint64_t *trips,
		 rtx_insn **step_out, uint64_t *final_out)
{
  uint64_t legacy_trips = 0;
  rtx_insn *legacy_step = nullptr;
  uint64_t legacy_final = 0;
  bool legacy = legacy_constant_trips (loop, preheader, &legacy_trips,
				       &legacy_step, &legacy_final);

  uint64_t classical_trips = 0;
  bool classical = classical_rtl_trips (loop, preheader, &classical_trips);
  if (classical && riscv_tt_trips_oracle_skew)
    classical_trips += riscv_tt_trips_oracle_skew;

  report_oracles ("rtl", loop, legacy, legacy_trips,
		  classical, classical_trips);

  if (!legacy)
    return false;
  *trips = legacy_trips;
  if (step_out)
    *step_out = legacy_step;
  if (final_out)
    *final_out = legacy_final;
  return true;
}

bool
rvtt_loop_trips_gimple (class loop *loop, unsigned HOST_WIDE_INT *trips)
{
  unsigned HOST_WIDE_INT legacy_trips = 0;
  bool legacy = rvtt_replay_unroll_counted_trips (loop, &legacy_trips);

  unsigned HOST_WIDE_INT classical_trips = 0;
  bool classical = classical_gimple_trips (loop, &classical_trips);
  if (classical && riscv_tt_trips_oracle_skew)
    classical_trips += riscv_tt_trips_oracle_skew;

  report_oracles ("gimple", loop, legacy, legacy_trips,
		  classical, classical_trips);

  if (!legacy)
    return false;
  *trips = legacy_trips;
  return true;
}
