/* Pass to re-roll replay-launch delivery into the Tensix MOP expander.
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

/* MOP-driven loop delivery (rvtt_mop_form).

   After replay formation and Dst auto-increment, a repeated payload often
   arrives as N identical REPLAY playback launches, either as a
   straight-line run (RUN) or as a counted single-block loop around one
   launch with a constant trip count (LOOP).  Each launch word is pushed
   by the RISC.  The Tensix MOP expander can issue them instead: a MOP
   whose template A0 slot holds the launch word replays it loop_count+1
   times with no per-iteration RISC work.  The template used is mop_type 0,
   flags 0, zmask 0 with A0 the playback word, plus up to three SETRWC
   step words in the flags&2 slots, the same shape as the
   ckernel_unpack_template::lA (REPLAY, NOP) template in Tenstorrent's
   kernel library.

   The template is programmed with the documented protocol
   (rvtt-mop-tables.h): a mop_sync store, MMIO writes of the flags and
   template words, MOP_CFG 0 to clear the zmask high half, then
   TTMOP 0, N-1, 0.  The launch must still fit the 32-slot replay buffer.

   Profitability (rvtt-cost.md, MOP section): a launch row costs
   max (len * SLOT, delivered_words * PUSH), a MOP row costs len * SLOT,
   and the configuration block is delivered serially.  Only
   delivery-bound rows benefit.

   The MOP template registers are shared thread state that outlives this
   function and cannot be read back by the RISC, so a caller's template
   cannot be saved and restored.  Formation therefore requires both that
   nothing in the function itself (call or asm) can touch MOP state and
   that no caller can launch a template of its own after a call to this
   function without reprogramming it first (rvtt_mop_outward_owned_p).
   The latter holds trivially for the kernel entry point, whose only
   caller is crt0, and otherwise relies on the kernel being a single
   translation unit so that every caller is visible; an unclassifiable
   statement in a caller is treated as a MOP launch.  Getting this wrong
   hangs the Tensix.

   One MOP is formed per function.  Quasar is not supported.

   See C. W. Fraser, E. W. Myers and A. L. Wendt, "Analyzing and
   compressing assembly code", SIGPLAN Symposium on Compiler
   Construction, 1984, for the general idea of issuing a run of identical
   sequences from one compact representation.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#define INCLUDE_MAP
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "print-rtl.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "expr.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "cgraph.h"
#include "attribs.h"
#include "langhooks.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-mop-tables.h"
#include "rvtt-ipa-summary.h"

namespace {

/* A candidate MOP formation: N iterations of a fixed playback launch of
   [start, start + len) followed by 0 to 3 SETRWC step words (identical
   in every iteration), which go in the template's flags&2 slots.  */
struct mop_candidate
{
  enum { RUN, LOOP } form;
  std::vector<rtx_insn *> launches; /* RUN: all launches; LOOP: the one */
  std::vector<unsigned HOST_WIDE_INT> step_words; /* per-iteration steps */
  std::vector<rtx_insn *> doomed;   /* every insn the MOP replaces */
  unsigned start = 0;
  unsigned len = 0;
  uint64_t iterations = 0;
  /* LOOP only */
  class loop *loop = nullptr;
  rtx_insn *step = nullptr;
  rtx_insn *jump = nullptr;
  uint64_t counter_final = 0;
};

/* Match a fixed-encoding REPLAY playback launch: replay of an
   already-recorded range, all fields constant in the instruction word
   (the rvtt_ttreplay_int immediate alternative), no capture, no
   execute-while-loading.  */

static bool
fixed_playback_launch_p (rtx_insn *insn, unsigned *start, unsigned *len)
{
  if (GET_CODE (insn) != INSN
      || recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
    return false;
  rtx pat = PATTERN (insn);
  rtx mem = XVECEXP (pat, 0, 0);
  rtx length = XVECEXP (pat, 0, 3);
  rtx begin = XVECEXP (pat, 0, 5);
  rtx exec = XVECEXP (pat, 0, 6);
  rtx load = XVECEXP (pat, 0, 7);
  if (mem != const0_rtx || !CONST_INT_P (length) || !CONST_INT_P (begin)
      || !CONST_INT_P (exec) || !CONST_INT_P (load))
    return false;
  if (INTVAL (load) != 0 || INTVAL (exec) != 0)
    return false;
  *start = UINTVAL (begin);
  *len = UINTVAL (length);
  return true;
}

/* Return true if INSN is a playback launch in any encoding (used only to
   report the mop-body-not-invariant refusal).  */

static bool
playback_launch_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN
      || recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
    return false;
  rtx pat = PATTERN (insn);
  rtx load = XVECEXP (pat, 0, 7);
  return CONST_INT_P (load) && INTVAL (load) == 0;
}

/* If INSN is an address-stepping insn whose words can go in the MOP
   template's flags&2 slots, append its words to WORDS and return true.
   These are the WH/BH SETRWC (whose operands are constants in TT_OP
   field order) and the Dst face advance, which emits two CR-mode
   Dst += 8 SETRWC words (rvtt_ttdstface_wh_bh in rvtt.md).  */

static bool
step_insn_words (rtx_insn *insn,
		 std::vector<unsigned HOST_WIDE_INT> &words)
{
  if (GET_CODE (insn) != INSN)
    return false;
  int code = recog_memoized (insn);
  if (code == CODE_FOR_rvtt_ttsetrwc_wh_bh)
    {
      rtx pat = PATTERN (insn);
      HOST_WIDE_INT f[6];
      for (int i = 0; i != 6; ++i)
	f[i] = INTVAL (XVECEXP (pat, 0, i));
      words.push_back (TARGET_XTT_TENSIX_WH
		       ? TT_OP_WH_SETRWC (f[0], f[1], f[2], f[3], f[4], f[5])
		       : TT_OP_BH_SETRWC (f[0], f[1], f[2], f[3], f[4],
					  f[5]));
      return true;
    }
  if (code == CODE_FOR_rvtt_ttdstface_wh_bh)
    {
      unsigned HOST_WIDE_INT w
	= TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SETRWC (0, 4, 8, 0, 0, 4)
	: TT_OP_BH_SETRWC (0, 4, 8, 0, 0, 4);
      words.push_back (w);
      words.push_back (w);
      return true;
    }
  return false;
}

/* Tensix NOP, used for unused template slots; the instruction FIFO
   discards it.  */
constexpr unsigned HOST_WIDE_INT XTT_TENSIX_NOP_WORD = 0x02000000;

/* Return true if INSN delivers no instruction word: a USE or CLOBBER, or
   a recognized insn of length zero.  Callers filter notes and debug
   insns.  */

static bool
non_delivering_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN)
    return false;
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
    return true;
  if (recog_memoized (insn) >= 0 && !get_attr_length (insn))
    return true;
  return false;
}

/* ---- Constant trip counts (LOOP form) ----
   The loop must be a single block ending in a two-way conditional jump,
   with one counter register stepped once by a constant, a constant bound
   and a constant initial value reaching it through the chain of unique
   predecessors from the preheader.  The trip count is computed by
   simulating the counter at the mode precision; estimated or profile
   counts are not used.  */

static bool
mop_constant_reaching_value (basic_block preheader, rtx reg, uint64_t *value)
{
  basic_block bb = preheader;
  /* Walk the unique-predecessor chain back from PREHEADER as far as
     the CFG allows; the visited set stops the walk on a cycle.  */
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

/* Evaluate the integer comparison CODE on VAL0 and VAL1 taken as
   PREC-bit values (sign-extended for the signed codes).  Any other
   code is false.  */

static bool
mop_eval_int_condition (rtx_code code, uint64_t val0, uint64_t val1,
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

/* Return true if the single-block LOOP entered through PREHEADER runs a
   constant number of trips: the header must step exactly
   one counter register by a constant, the comparison's other side must
   be constant (immediately or as a preheader-reaching constant
   definition), and simulating up to 2^16 iterations must exit.  On
   success set *TRIPS to the trip count, *STEP_OUT to the step insn and
   *FINAL_OUT to the counter's value at loop exit.  */

static bool
mop_provable_constant_trips (class loop *loop, basic_block preheader,
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
  bool taken_when_true = GET_CODE (XEXP (src, 1)) != PC;

  rtx op0 = XEXP (cond, 0);
  rtx op1 = XEXP (cond, 1);

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
  uint64_t mask = prec == 64 ? ~uint64_t (0) : (uint64_t (1) << prec) - 1;

  uint64_t init;
  if (!mop_constant_reaching_value (preheader, counter, &init))
    return false;

  uint64_t bound_val;
  if (CONST_INT_P (bound))
    bound_val = UINTVAL (bound);
  else if (REG_P (bound))
    {
      rtx_insn *insn;
      FOR_BB_INSNS (header, insn)
	if (NONDEBUG_INSN_P (insn) && insn != jump
	    && reg_set_p (bound, insn))
	  return false;
      if (!mop_constant_reaching_value (preheader, bound, &bound_val))
	return false;
    }
  else
    return false;
  bound_val &= mask;

  bool counter_is_op0 = rtx_equal_p (counter, op0);
  uint64_t c = init & mask;
  constexpr uint64_t TRIP_BOUND = uint64_t (1) << 16;
  for (uint64_t t = 1; t <= TRIP_BOUND; ++t)
    {
      c = (c + step) & mask;
      uint64_t v0 = counter_is_op0 ? c : bound_val;
      uint64_t v1 = counter_is_op0 ? bound_val : c;
      bool cond_holds = mop_eval_int_condition (GET_CODE (cond), v0, v1, prec);
      bool taken = cond_holds == taken_when_true;
      bool continues = taken == taken_continues;
      if (!continues)
	{
	  *trips = t;
	  *step_out = step_insn;
	  *final_out = c;
	  return true;
	}
    }
  return false;
}

/* Return LOOP's dedicated preheader: the unique block outside LOOP
   entering its header, provided the header is its only successor and the
   entry edge is not abnormal.  Return null otherwise.  */

static basic_block
mop_dedicated_loop_preheader (class loop *loop)
{
  basic_block preheader = nullptr;
  edge entry = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, loop->header->preds)
    if (!flow_bb_inside_loop_p (loop, e->src))
      {
	if (preheader)
	  return nullptr;
	preheader = e->src;
	entry = e;
      }

  return preheader && !(entry->flags & EDGE_ABNORMAL)
	 && single_succ_p (preheader)
    ? preheader : nullptr;
}

/* ---- Candidate discovery ---- */

/* State for collecting maximal straight-line runs of identical groups,
   each a fixed playback launch followed by 0 to 3 identical SETRWC step
   words.  Only non-delivering insns may appear between group members;
   any other delivered word, jump or different launch ends the run.  If
   the last launch lacks its step words, only the complete groups are
   used.  */

struct run_builder
{
  bool active = false;
  bool proto_set = false;
  unsigned start = 0, len = 0;
  std::vector<rtx_insn *> launches;
  /* Step INSNS between launches (for deletion), and their WORD
     sequences (for identity and encoding).  */
  std::vector<std::vector<rtx_insn *>> gaps;
  std::vector<rtx_insn *> cur_gap;
  std::vector<unsigned HOST_WIDE_INT> cur_gap_words;
  std::vector<unsigned HOST_WIDE_INT> proto; /* the group's step words */
};

/* Close the run being accumulated in RB and, if it has at least two
   complete groups, append a RUN-form candidate to OUT.  A final launch
   without its step words is left out.  Reset RB to inactive.  */

static void
run_finalize (run_builder &rb, std::vector<mop_candidate> &out)
{
  if (!rb.active)
    return;
  size_t m = rb.launches.size ();
  size_t k = rb.proto.size ();
  uint64_t iterations = 0;
  bool tail_complete = false;
  if (m >= 2)
    {
      if (k == 0)
	iterations = m;
      else if (rb.cur_gap_words == rb.proto)
	{
	  iterations = m;
	  tail_complete = true;
	}
      else if (m - 1 >= 2)
	iterations = m - 1;
    }
  if (iterations >= 2)
    {
      mop_candidate cand;
      cand.form = mop_candidate::RUN;
      cand.start = rb.start;
      cand.len = rb.len;
      cand.iterations = iterations;
      cand.step_words = rb.proto;
      for (uint64_t i = 0; i != iterations; ++i)
	{
	  cand.launches.push_back (rb.launches[i]);
	  cand.doomed.push_back (rb.launches[i]);
	  if (i + 1 < m)
	    for (rtx_insn *s : rb.gaps[i])
	      cand.doomed.push_back (s);
	  else if (tail_complete)
	    for (rtx_insn *s : rb.cur_gap)
	      cand.doomed.push_back (s);
	}
      out.push_back (cand);
    }
  rb = run_builder ();
}

/* Start a new run in RB whose first launch is INSN, a fixed playback
   launch of replay start S and length L.  */

static void
run_begin (run_builder &rb, rtx_insn *insn, unsigned s, unsigned l)
{
  rb = run_builder ();
  rb.active = true;
  rb.start = s;
  rb.len = l;
  rb.launches.push_back (insn);
}

/* Append to OUT every straight-line run in CFN of identical fixed
   playback launches separated by identical step words (see run_builder).
   The first gap fixes the step words; a different gap closes the run
   and starts a new one at the current launch.  */

static void
collect_runs (function *cfn, std::vector<mop_candidate> &out)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      run_builder rb;
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  unsigned s, l;
	  if (fixed_playback_launch_p (insn, &s, &l))
	    {
	      if (rb.active && s == rb.start && l == rb.len)
		{
		  /* Close the gap before this launch.  */
		  if (!rb.proto_set)
		    {
		      rb.proto = rb.cur_gap_words;
		      rb.proto_set = true;
		    }
		  if (rb.cur_gap_words == rb.proto)
		    {
		      rb.gaps.push_back (rb.cur_gap);
		      rb.cur_gap.clear ();
		      rb.cur_gap_words.clear ();
		      rb.launches.push_back (insn);
		      continue;
		    }
		  /* Gap mismatch: keep the complete groups, restart here.  */
		  run_finalize (rb, out);
		  run_begin (rb, insn, s, l);
		  continue;
		}
	      run_finalize (rb, out);
	      run_begin (rb, insn, s, l);
	      continue;
	    }
	  if (non_delivering_p (insn))
	    continue;
	  if (rb.active)
	    {
	      std::vector<unsigned HOST_WIDE_INT> words;
	      if (step_insn_words (insn, words)
		  && rb.cur_gap_words.size () + words.size () <= 3
		  && (!rb.proto_set
		      || rb.cur_gap_words.size () + words.size ()
			   <= rb.proto.size ()))
		{
		  rb.cur_gap.push_back (insn);
		  for (unsigned HOST_WIDE_INT w : words)
		    rb.cur_gap_words.push_back (w);
		  continue;
		}
	    }
	  run_finalize (rb, out);
	}
      run_finalize (rb, out);
    }
}

/* Append to OUT every single-block loop in CFN whose body is one fixed
   playback launch plus the counter step, with a constant trip count.
   Near misses are reported in the dump; unrelated loops are ignored.  */

static void
collect_loops (function *cfn, std::vector<mop_candidate> &out)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;
      if (loop->num_nodes != 1 || loop->header != loop->latch)
	continue;

      rtx_insn *jump = BB_END (bb);
      if (!JUMP_P (jump))
	continue;

      /* Classify the body.  */
      mop_candidate cand;
      cand.form = mop_candidate::LOOP;
      cand.loop = loop;
      cand.jump = jump;
      rtx_insn *scalar = nullptr;
      bool bad = false, extra = false, not_invariant = false;
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn) || insn == jump)
	    continue;
	  if (non_delivering_p (insn))
	    continue;
	  if (CALL_P (insn) || GET_CODE (insn) != INSN)
	    {
	      bad = true;
	      break;
	    }
	  unsigned s, l;
	  if (fixed_playback_launch_p (insn, &s, &l))
	    {
	      if (!cand.launches.empty ())
		{
		  extra = true; /* two launches */
		  break;
		}
	      cand.start = s;
	      cand.len = l;
	      cand.launches.push_back (insn);
	      continue;
	    }
	  if (playback_launch_p (insn))
	    {
	      not_invariant = true;
	      break;
	    }
	  if (recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    {
	      extra = true; /* other delivered Tensix work in the body */
	      break;
	    }
	  if (scalar)
	    {
	      extra = true; /* more scalar state than the counter step */
	      break;
	    }
	  scalar = insn;
	}
      if (bad || cand.launches.empty ())
	continue;
      if (not_invariant)
	{
	  rvtt_refuse (RVTT_REF_MOP_BODY_NOT_INVARIANT, dump_file,
		       "MOP-form refused (mop-body-not-invariant): loop bb %d"
		       " launch word is synthesized at run time\n", bb->index);
	  continue;
	}
      if (extra || !scalar)
	{
	  rvtt_refuse (RVTT_REF_MOP_BODY_EXTRA_DELIVERY, dump_file,
		       "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		       " body is not exactly one launch plus the counter"
		       " step\n", bb->index);
	  continue;
	}
      if (loop->unroll)
	{
	  rvtt_refuse (RVTT_REF_MOP_BODY_EXTRA_DELIVERY, dump_file,
		       "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		       " carries an explicit user unroll request\n", bb->index);
	  continue;
	}

      basic_block preheader = mop_dedicated_loop_preheader (loop);
      if (!preheader)
	{
	  rvtt_refuse (RVTT_REF_MOP_TRIPS_UNPROVED, dump_file,
		       "MOP-form refused (mop-trips-unproved): loop bb %d has"
		       " no dedicated preheader\n", bb->index);
	  continue;
	}

      uint64_t trips, final_value;
      rtx_insn *step;
      if (!mop_provable_constant_trips (loop, preheader, &trips, &step,
					&final_value))
	{
	  rvtt_refuse (RVTT_REF_MOP_TRIPS_UNPROVED, dump_file,
		       "MOP-form refused (mop-trips-unproved): loop bb %d trip"
		       " count is not provably constant\n", bb->index);
	  continue;
	}
      if (step != scalar)
	{
	  rvtt_refuse (RVTT_REF_MOP_BODY_EXTRA_DELIVERY, dump_file,
		       "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		       " scalar insn is not the proven counter step\n",
		       bb->index);
	  continue;
	}

      rtx step_set = single_set (step);
      rtx counter = SET_DEST (step_set);
      rtx final_rtx = gen_int_mode (final_value, GET_MODE (counter));
      if (!SMALL_OPERAND (INTVAL (final_rtx))
	  && !LUI_OPERAND (INTVAL (final_rtx)))
	{
	  rvtt_refuse (RVTT_REF_MOP_BODY_EXTRA_DELIVERY, dump_file,
		       "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		       " counter exit value is not materializable\n",
		       bb->index);
	  continue;
	}

      /* The commit removes the taken branch, which must be the
         backedge; check it here so that an exit edge is never
         removed.  */
      if (BRANCH_EDGE (bb)->dest != bb)
	continue;

      cand.iterations = trips;
      cand.step = step;
      cand.counter_final = final_value;
      cand.doomed = cand.launches;
      out.push_back (cand);
    }
}

/* ---- Profitability ----

   One launch row costs max (len * REPLAY_SLOT, delivered * RISC_PUSH)
   centislots, since RISC delivery overlaps replay playback (rvtt-cost.md,
   MOP section).  A MOP row delivers nothing.  The configuration block is
   delivered serially.  */

/* The MOP config register values a candidate programs, in store order:
   (index, value) with index 1 = flags and 3..6 the template slots.  */

static void
mop_config_values (mop_candidate const &cand,
		   std::vector<std::pair<unsigned, unsigned HOST_WIDE_INT>>
		     &out)
{
  unsigned HOST_WIDE_INT launch_word
    = TARGET_XTT_TENSIX_WH
    ? TT_OP_WH_REPLAY (cand.start, cand.len, 0, 0)
    : TT_OP_BH_REPLAY (cand.start, cand.len, 0, 0);
  size_t k = cand.step_words.size ();
  out.clear ();
  out.emplace_back (XTT_MOP_CFG_FLAGS_INDEX, k ? 2 : 0);
  out.emplace_back (XTT_MOP_CFG_A0_INDEX, launch_word);
  if (k)
    {
      /* flags&2 slots fire on every zmask==0 iteration; every consumed
         slot is written, unused ones with the FIFO-swallowed NOP.  */
      out.emplace_back (4, cand.step_words[0]);
      out.emplace_back (5, k > 1 ? cand.step_words[1]
				 : XTT_TENSIX_NOP_WORD);
      out.emplace_back (6, k > 2 ? cand.step_words[2]
				 : XTT_TENSIX_NOP_WORD);
    }
}

/* Return the number of instructions needed to load the 32-bit WORD into a
   GPR: one for a small or LUI operand, two (lui + addi) otherwise.  */

static HOST_WIDE_INT
mop_li_words (unsigned HOST_WIDE_INT word)
{
  HOST_WIDE_INT v = trunc_int_for_mode (word, SImode);
  return (LUI_OPERAND (v) || SMALL_OPERAND (v)) ? 1 : 2;
}

/* Return the number of RISC words of the MMIO configuration block for
   VALUES.  */

static HOST_WIDE_INT
mop_config_words (std::vector<std::pair<unsigned,
					unsigned HOST_WIDE_INT>> const
		    &values)
{
  /* mop_sync: lui + sw; config base lui; one store per value (zero
     stores from x0, a repeated value reuses the data register);
     MOP_CFG + MOP words.  */
  HOST_WIDE_INT words = 2 + 1 + 2;
  unsigned HOST_WIDE_INT last = 0;
  bool have_last = false;
  for (auto const &iv : values)
    {
      if (iv.second != 0 && (!have_last || iv.second != last))
	{
	  words += mop_li_words (iv.second);
	  last = iv.second;
	  have_last = true;
	}
      words += 1; /* the sw */
    }
  return words;
}

/* Return true if CAND is profitable given a configuration block of
   CONFIG_WORDS words: the saving is the words the MOP rows no longer
   push, less the configuration block.  Store the benefit in centislots
   in *BENEFIT_OUT and compare it with -mtt-tensix-mop-form-min-benefit=
   (or the cost-table default).  */

static bool
mop_profitable_p (mop_candidate const &cand, HOST_WIDE_INT config_words,
		  HOST_WIDE_INT *benefit_out)
{
  HOST_WIDE_INT k = (HOST_WIDE_INT) cand.step_words.size ();
  /* Executed rows at the slot rate; delivered words and the
     configuration block at the push rate.  */
  HOST_WIDE_INT exec_row = rvtt_dcost_words_to_centislots
    ((HOST_WIDE_INT) cand.len + k, rvtt_delivery_cost::PLANE_REPLAY_SLOT);
  /* RUN rows deliver the launch word plus their step words; LOOP trips
     additionally deliver the two loop-control words.  */
  HOST_WIDE_INT delivered = cand.form == mop_candidate::RUN ? 1 + k : 3;
  HOST_WIDE_INT before_row
    = MAX (exec_row,
	   rvtt_dcost_words_to_centislots
	     (delivered, rvtt_delivery_cost::PLANE_RISC_PUSH));
  HOST_WIDE_INT benefit = ((HOST_WIDE_INT) cand.iterations
			   * (before_row - exec_row))
    - rvtt_dcost_words_to_centislots (config_words,
				      rvtt_delivery_cost::PLANE_RISC_PUSH);
  *benefit_out = benefit;

  HOST_WIDE_INT min_benefit = (riscv_tt_mop_form_min_benefit >= 0
			       ? (HOST_WIDE_INT) riscv_tt_mop_form_min_benefit
			       : XTT_MOP_FORM_MIN_BENEFIT);
  return benefit >= min_benefit;
}

/* ---- Scratch registers for the MMIO configuration block ----

   After reload, the block needs two dead caller-saved GPRs (address base
   and data word), found by simulating liveness backwards from the end of
   the block to the insertion point.  */

static bool
find_scratch_gprs (rtx_insn *point, unsigned regs[2])
{
  basic_block bb = BLOCK_FOR_INSN (point);
  auto_bitmap live;
  bitmap_copy (live, DF_LR_OUT (bb));
  df_simulate_initialize_backwards (bb, live);
  for (rtx_insn *insn = BB_END (bb); insn; insn = PREV_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	df_simulate_one_insn_backwards (bb, insn, live);
      if (insn == point || insn == BB_HEAD (bb))
	break;
    }

  /* RISC-V temporaries t0-t2, t3-t6: caller-saved, never
     frame-critical.  */
  static const unsigned char cands[] = { 5, 6, 7, 28, 29, 30, 31 };
  unsigned found = 0;
  for (unsigned char r : cands)
    {
      if (fixed_regs[r] || bitmap_bit_p (live, r))
	continue;
      regs[found++] = r;
      if (found == 2)
	return true;
    }
  return false;
}

/* ---- Emission ---- */

static rtx_insn *
emit_volatile_store (rtx base, HOST_WIDE_INT offset, rtx src)
{
  rtx addr = offset ? gen_rtx_PLUS (SImode, base, GEN_INT (offset)) : base;
  rtx mem = gen_rtx_MEM (SImode, addr);
  MEM_VOLATILE_P (mem) = 1;
  return emit_insn (gen_rtx_SET (mem, src));
}

/* Build the configuration and MOP sequence.  Return null if any insn
   fails to be recognized.  */

static rtx_insn *
build_mop_sequence (mop_candidate const &cand, unsigned scratch[2])
{
  std::vector<std::pair<unsigned, unsigned HOST_WIDE_INT>> values;
  mop_config_values (cand, values);

  rtx base = gen_rtx_REG (SImode, scratch[0]);
  rtx data = gen_rtx_REG (SImode, scratch[1]);

  start_sequence ();
  /* Wait for any running MOP to finish before writing its template
     registers (rvtt-mop-tables.h).  */
  emit_move_insn (base, gen_int_mode (XTT_MOP_SYNC_MMIO_ADDR & ~0xfff,
				      SImode));
  emit_volatile_store (base, XTT_MOP_SYNC_MMIO_ADDR & 0xfff, const0_rtx);
  /* Template registers: flags, A0 = the launch word, and any flags&2
     step-word slots (mop_config_values).  */
  emit_move_insn (base, gen_int_mode (XTT_MOP_CFG_MMIO_BASE, SImode));
  unsigned HOST_WIDE_INT last = 0;
  bool have_last = false;
  for (auto const &iv : values)
    {
      rtx src = const0_rtx;
      if (iv.second != 0)
	{
	  if (!have_last || iv.second != last)
	    {
	      emit_move_insn (data, gen_int_mode (iv.second, SImode));
	      last = iv.second;
	      have_last = true;
	    }
	  src = data;
	}
      emit_volatile_store (base, 4 * iv.first, src);
    }
  /* Clear the zmask high half, which is persistent thread state, then
     run loop_count + 1 iterations.  */
  emit_insn (gen_rvtt_ttmopcfg_int (const0_rtx));
  emit_insn (gen_rvtt_ttmop_int (const0_rtx,
				 GEN_INT (cand.iterations - 1),
				 const0_rtx));
  rtx_insn *seq = get_insns ();
  end_sequence ();

  for (rtx_insn *insn = seq; insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn) && recog_memoized (insn) < 0)
      {
	if (dump_file)
	  {
	    rvtt_refuse (RVTT_REF_MOP_EMIT_UNRECOGNIZED, dump_file,
			 "MOP-form refused (mop-emit-unrecognized): emitted"
			 " configuration insn is not recognized:\n");
	    dump_insn_slim (dump_file, insn);
	  }
	return nullptr;
      }
  return seq;
}

/* Commit CAND: find two dead caller-saved GPRs, emit the MMIO
   configuration and TTMOPCFG/TTMOP sequence before the first launch, and
   delete the replaced launch and step insns.  For a counted loop, also
   set the counter to its exit value and remove the loop control.  Return
   false, changing nothing, if no scratch GPRs are free or an insn fails
   to be recognized.  */

static bool
commit_candidate (mop_candidate &cand)
{
  unsigned scratch[2];
  if (!find_scratch_gprs (cand.launches.front (), scratch))
    {
      rvtt_refuse (RVTT_REF_MOP_NO_SCRATCH_GPR, dump_file,
		   "MOP-form refused (mop-no-scratch-gpr): no two dead"
		   " temporaries before insn %d\n",
		   INSN_UID (cand.launches.front ()));
      return false;
    }

  rtx_insn *seq = build_mop_sequence (cand, scratch);
  if (!seq)
    return false;

  emit_insn_before (seq, cand.launches.front ());

  if (dump_file)
    {
      fprintf (dump_file,
	       "MOP formed (mop0-lA-replay, %s): %lu iterations of launch"
	       " [%u,+%u) -> TTMOP 0, %lu, 0 (scratch x%u, x%u)\n",
	       cand.form == mop_candidate::RUN ? "run" : "counted loop",
	       (unsigned long) cand.iterations, cand.start, cand.len,
	       (unsigned long) (cand.iterations - 1), scratch[0], scratch[1]);
      if (!cand.step_words.empty ())
	fprintf (dump_file,
		 "MOP template carries %u SETRWC step word(s) in the"
		 " flags&2 slots\n", unsigned (cand.step_words.size ()));
      fprintf (dump_file, "Replacing ");
      dump_insn_slim (dump_file, cand.launches.front ());
    }

  for (rtx_insn *doomed : cand.doomed)
    SET_INSN_DELETED (doomed);

  if (cand.form == mop_candidate::LOOP)
    {
      /* The loop now runs once: set the counter to its exit value and
         remove the loop control.  */
      rtx step_set = single_set (cand.step);
      rtx counter = SET_DEST (step_set);
      rtx final_rtx = gen_int_mode (cand.counter_final, GET_MODE (counter));
      emit_insn_before (gen_rtx_SET (counter, final_rtx), cand.step);
      delete_insn (cand.step);
      loops_state_set (LOOPS_NEED_FIXUP);
      remove_edge (BRANCH_EDGE (cand.loop->header));
      delete_insn (cand.jump);
      if (dump_file)
	fprintf (dump_file,
		 "Removed counted-loop control bb %d; counter set to its"
		 " proven exit value\n", cand.loop->header->index);
    }
  return true;
}

/* ---- Driver ---- */

static void
transform (function *cfn)
{
  /* MOP configuration is shared thread state that a call or asm may
     program or use (kernels program templates from C++ and inline
     asm), so refuse any function containing either.  */
  basic_block bb;
  bool unowned = false;
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	if (NONDEBUG_INSN_P (insn)
	    && (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0))
	  {
	    unowned = true;
	    break;
	  }
      if (unowned)
	break;
    }

  std::vector<mop_candidate> candidates;
  collect_runs (cfn, candidates);
  collect_loops (cfn, candidates);
  if (candidates.empty ())
    return;

  if (unowned)
    {
      rvtt_refuse (RVTT_REF_MOP_CONFIG_UNOWNED, dump_file,
		   "MOP-form refused (mop-config-unowned): function contains"
		   " a call or opaque asm; %u candidate(s) dropped\n",
		   unsigned (candidates.size ()));
      return;
    }

  /* The template outlives this function, so check that no caller can
     launch a template of its own after a call without reprogramming
     it (see the comment at the top of the file).  */
  {
    const char *why = nullptr, *why_fn = nullptr, *how = nullptr;
    if (!rvtt_mop_outward_owned_p (cfn, &why, &why_fn, &how))
      {
	if (dump_file)
	  {
	    if (why_fn)
	      rvtt_refuse (RVTT_REF_MOP_CALLER_TEMPLATE_LIVE_UNPROVEN,
			   dump_file,
			   "MOP-form refused (mop-caller-template-live-"
			   "unproven): %s (%s); %u candidate(s) dropped\n",
			   why, why_fn, unsigned (candidates.size ()));
	    else
	      rvtt_refuse (RVTT_REF_MOP_CALLER_TEMPLATE_LIVE_UNPROVEN,
			   dump_file,
			   "MOP-form refused (mop-caller-template-live-"
			   "unproven): %s; %u candidate(s) dropped\n",
			   why, unsigned (candidates.size ()));
	  }
	return;
      }
    if (dump_file)
      fprintf (dump_file, "MOP outward ownership proven: %s\n", how);
  }

  unsigned buffer_size = riscv_tt_replay_size;
  mop_candidate *best = nullptr;
  HOST_WIDE_INT best_benefit = 0;
  for (auto &cand : candidates)
    {
      if (cand.start + cand.len > buffer_size)
	{
	  rvtt_refuse (RVTT_REF_MOP_REPLAY_WINDOW_OVERFLOW, dump_file,
		       "MOP-form refused (mop-replay-window-overflow): launch"
		       " [%u,+%u) exceeds the %u-slot replay buffer"
		       " (S+L > %u)\n",
		       cand.start, cand.len, buffer_size, buffer_size);
	  continue;
	}
      if (cand.iterations < 2 || cand.iterations > XTT_MOP0_MAX_ITERATIONS)
	{
	  rvtt_refuse (RVTT_REF_MOP_LOOP_COUNT_RANGE, dump_file,
		       "MOP-form refused (mop-loop-count-range): %lu iterations"
		       " outside [2, %u]\n",
		       (unsigned long) cand.iterations,
		       XTT_MOP0_MAX_ITERATIONS);
	  continue;
	}

      std::vector<std::pair<unsigned, unsigned HOST_WIDE_INT>> values;
      mop_config_values (cand, values);
      HOST_WIDE_INT config_words = mop_config_words (values);
      HOST_WIDE_INT benefit;
      bool profitable = mop_profitable_p (cand, config_words, &benefit);
      if (dump_file)
	{
	  if (cand.step_words.empty ())
	    fprintf (dump_file,
		     "MOP-form candidate (%s): %lu x launch [%u,+%u), config"
		     " %ld words, modeled benefit %ld\n",
		     cand.form == mop_candidate::RUN ? "run" : "counted loop",
		     (unsigned long) cand.iterations, cand.start, cand.len,
		     (long) config_words, (long) benefit);
	  else
	    fprintf (dump_file,
		     "MOP-form candidate (%s): %lu x launch [%u,+%u) + %u"
		     " step word(s), config %ld words, modeled benefit %ld\n",
		     cand.form == mop_candidate::RUN ? "run" : "counted loop",
		     (unsigned long) cand.iterations, cand.start, cand.len,
		     unsigned (cand.step_words.size ()),
		     (long) config_words, (long) benefit);
	}
      if (!profitable)
	{
	  rvtt_refuse (RVTT_REF_MOP_PROFITABILITY, dump_file,
		       "MOP-form refused (mop-profitability): modeled benefit"
		       " %ld below threshold\n", (long) benefit);
	  continue;
	}
      if (!best || benefit > best_benefit)
	{
	  best = &cand;
	  best_benefit = benefit;
	}
    }

  if (!best)
    return;

  commit_candidate (*best);

  /* Form at most one MOP per function; a second one would reprogram
     the template, which is not tracked.  */
  if (dump_file)
    for (auto &cand : candidates)
      if (&cand != best && cand.start + cand.len <= buffer_size
	  && cand.iterations >= 2
	  && cand.iterations <= XTT_MOP0_MAX_ITERATIONS)
	rvtt_refuse (RVTT_REF_MOP_CONFIG_EPOCH, dump_file,
		     "MOP-form refused (mop-config-epoch): a MOP was already"
		     " formed in this function (candidate %lu x [%u,+%u))\n",
		     (unsigned long) cand.iterations, cand.start, cand.len);
}

const pass_data pass_data_rvtt_mop_form =
{
  RTL_PASS, /* type */
  "rvtt_mop_form", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_mop_form : public rtl_opt_pass
{
public:
  pass_rvtt_mop_form (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_mop_form, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    /* Quasar's MOP encoding and expander are not described in
       rvtt-mop-tables.h.  */
    return (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
      && riscv_tt_opt_mop_form > 0;
  }

  virtual unsigned execute (function *fn) override
  {
    df_analyze ();
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    transform (fn);
    loop_optimizer_finalize ();
    free_dominance_info (CDI_DOMINATORS);
    return 0;
  }
}; /* class pass_rvtt_mop_form */

} /* anon namespace */

/* Create the MOP-formation pass.  It runs after reload, after the replay
   and Dst auto-increment passes, and is enabled on Wormhole and
   Blackhole by -mtt-tensix-optimize-mop-form.  */

rtl_opt_pass *
make_pass_rvtt_mop_form (gcc::context *ctxt)
{
  return new pass_rvtt_mop_form (ctxt);
}
