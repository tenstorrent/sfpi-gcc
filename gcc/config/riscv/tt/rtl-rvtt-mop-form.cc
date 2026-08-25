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

/* MOP-driven loop delivery (rvtt_mop_form), first increment.

   After replay formation (and its hoist/unroll/conversion family) and
   the Dst auto-increment ownership pass have produced the final
   delivery stream, a repeated payload frequently arrives as N identical
   REPLAY playback launches: either a straight-line contiguous run (the
   image of a counted launch loop after the launch-loop unroll, or of a
   repeated static sequence) or a still-rolled counted loop around a
   single launch.  Every one of those N launch words is pushed by the
   RISC.  The Tensix MOP expander can issue them instead: one MOP
   instruction whose template's A0 slot holds the launch word replays it
   loop_count+1 times through the same replay expander, with no RISC
   involvement per iteration.

   This increment implements exactly one template class,

     mop0-lA-replay: mop_type 0, flags 0, zmask 0, A0 = fixed REPLAY
     playback word -- the production ckernel_unpack_template::lA
     (REPLAY, NOP) shape (topk_xl merge loops),

   over two delivery forms of the same fixed-iteration loop:

     RUN:  a maximal run of >= 2 identical fixed-encoding playback
           launches with no delivered word between them; and
     LOOP: a single-block counted loop whose body is exactly one such
           launch plus its counter step, with a provable constant trip
           count (the replay-hoist trip discipline: estimated or
           profile counts refuse).

   Emission is the production programming protocol (table facts with
   provenance in rvtt-mop-tables.h): mop_sync guard store, MMIO writes
   of the template's flags and A0 words, MOP_CFG 0 (proving the zmask
   high half), then TTMOP 0, N-1, 0.  The replay buffer is co-owned
   with REPLAY: the template's launch word references the same 32-slot
   buffer, so the pass re-verifies start + len <= buffer size at emit
   and refuses the overflow near-miss (the simulator models that
   overflow as UndefinedBehavior).

   Profitability follows the corrected concurrent-delivery accounting
   (rvtt-cost.md, MOP section): a launch row costs
   max (len * SLOT, delivered_words * PUSH), a MOP row costs
   len * SLOT, and the configuration block is serial delivery.  Only
   delivery-bound rows benefit; execution-bound rows model <= 0 and
   refuse byte-identically.  -mtt-tensix-mop-form-min-benefit= overrides
   the cost-table threshold in the same centislot units.

   Every decision here keys on loop/run structure, operand constancy,
   and cost-table constants -- no operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.

   ---- Outward ownership (caller-side template liveness) ----

   The MOP template registers are thread-shared mutable state that
   SURVIVES this function's return, and they are write-only from the
   RISC (rvtt-mop-tables.h, readback fact), so a formation can neither
   snapshot nor restore a caller's template.  The inward proof
   (mop-config-unowned below) only shows that nothing INSIDE the
   function disturbs the formed template; it says nothing about a
   caller that programmed its own template, calls this function inside
   a loop, and launches that template again after we return.  Silicon
   evidence (the 2026-08-17 minmax force-leg adjudication): a perf
   harness that hoists a type-1 template program out of its tile loop
   and issues MOP per tile around a call into the formed function hangs
   the Tensix deterministically -- the caller's post-return launch
   expands OUR template words.

   So formation additionally requires an OUTWARD ownership proof:
   either the function is provably the outermost Tensix owner, or every
   caller provably re-arms the template between any call to this
   function and its next MOP launch.  Concretely (refusing whenever any
   step cannot be discharged):

     - the kernel entry (`main') is outermost by the kernel link model:
       its only caller is crt0, which delivers no Tensix work (AXIOM
       crt0-benign; same startup model the raw-word census audits);
     - otherwise the whole thread program is this TU plus crt0 (AXIOM
       kernel-single-TU: one translation unit per TRISC image, the
       harness build convention), so every call site is a cgraph edge.
       The pass walks the transitive caller closure -- refusing on
       address-taken members, recursion, or a caller body no longer in
       gimple form -- and runs a must-dataflow over each caller root:
       a call to the forming function clobbers the template-word set
       the formation writes (flags, A0, the step slots, and the
       MOP_CFG zmask high half); a subsequent MOP launch is a hazard
       unless every word it consumes was rewritten on every path in
       between (type-1 launches consume the nine config words but not
       the zmask; rvtt-mop-tables.h).  Caller events are classified
       from gimple: canonical `.ttinsn' words by their frontend opcode,
       MMIO stores to the MOP config block by constant address (a
       rewrite = re-arm credit), and computed instruction-FIFO pushes
       by the constant opcode base of their composed word (AXIOM
       tt-op-field-discipline: runtime operands of a TT_OP composition
       stay inside their bit fields, the discipline the TT_OP macro
       family itself encodes).  Anything unclassifiable -- opaque asm,
       indirect calls, a computed delivery with no constant base --
       counts as a potential MOP launch consuming everything, so the
       proof fails closed.

   Refusal taxonomy (all refusals leave the function byte-identical):
     mop-replay-window-overflow  launch range start + len exceeds the
                                 replay buffer (S+L > 32 near-miss);
     mop-loop-count-range        iterations outside [2, 128] (the MOP
                                 loop_count field proves <= 127);
     mop-trips-unproved          counted-loop trip count not provably
                                 constant;
     mop-body-not-invariant      the loop's launch word is synthesized
                                 at run time (RISC-computed operand);
     mop-body-extra-delivery     the loop body delivers words beyond
                                 the launch + counter step;
     mop-config-unowned          a call or opaque asm in the function
                                 could own or clobber MOP config state;
     mop-caller-template-live-unproven
                                 the function may be called while a
                                 caller-programmed MOP template is
                                 live, and no caller-side re-arm is
                                 proven before the caller's next MOP
                                 launch (the outward ownership proof
                                 above could not be discharged);
     mop-config-epoch            a MOP was already formed in this
                                 function (single-epoch conservatism of
                                 this increment; ownership epochs are a
                                 later stage);
     mop-profitability           modeled benefit below the threshold;
     mop-no-scratch-gpr          no dead temporaries for the MMIO
                                 configuration block.
   QSR refuses by pass gate: its MOP encoding and expander semantics
   are not in the capability table.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
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
#include "rvtt.h"
#include "rvtt-mop-tables.h"

namespace {

/* A candidate MOP formation: N iterations of [fixed playback launch of
   [start, +len), then the group's step words] -- the step words are the
   typed SETRWC address-stepper insns production places in the MOP
   template's flags&2 slots (0 to 3 of them, identical across
   iterations).  */
struct mop_candidate
{
  enum { RUN, LOOP } form;
  std::vector<rtx_insn *> launches; // RUN: all launches; LOOP: the one
  std::vector<unsigned HOST_WIDE_INT> step_words; // per-iteration steps
  std::vector<rtx_insn *> doomed;   // every insn the MOP replaces
  unsigned start = 0;
  unsigned len = 0;
  uint64_t iterations = 0;
  // LOOP only
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

/* A playback launch in any encoding (used only to name the
   mop-body-not-invariant refusal precisely).  */

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

/* Typed address-stepper insns whose delivered words may ride in the MOP
   template's flags&2 slots (rvtt-mop-tables.h): the WH/BH typed SETRWC
   (operands are const_int by pattern; operand order == TT_OP field
   order) and the typed Dst face advance, whose emission data is exactly
   two CR-mode Dst += 8 SETRWC words (rvtt.md rvtt_ttdstface_wh_bh).
   Appends INSN's step words to WORDS and returns true, or returns false
   for any other insn.  */

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

/* Tensix NOP: template slots that exist but must deliver nothing
   (rvtt-mop-tables.h -- swallowed at the instruction FIFO).  */
constexpr unsigned HOST_WIDE_INT XTT_TENSIX_NOP_WORD = 0x02000000;

/* An insn that delivers no instruction word: notes and debug insns are
   filtered by the callers; here USE/CLOBBER markers and recognized
   zero-length ghosts.  */

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

/* ---- Provable constant trip counts (LOOP form) ----
   The same cycle-safe constant-chain discipline as the accepted
   replay-hoist prover (rtl-rvtt-replay.cc provable_constant_trips):
   single-block loop ending in a two-way conditional jump, one counter
   register with exactly one reg = reg + const step, constant bound,
   constant initial value on the unique dedicated-preheader chain,
   direct evaluation wrapping at the mode precision.  Anything else --
   including merely estimated profile counts -- refuses.  Purely
   structural; duplicated rather than exported to keep this pass's
   territory to its own file.  */

static bool
mop_constant_reaching_value (basic_block preheader, rtx reg, uint64_t *value)
{
  basic_block bb = preheader;
  /* Keep the unique-predecessor reaching-definition proof, but make its
     extent depend on the CFG rather than an arbitrary split-block count.  */
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

/* Collect maximal straight-line runs of the iteration group
   [identical fixed playback launch, identical fixed SETRWC step words]
   (0 to 3 steps; the flags&2 template slots).  Only non-delivering
   insns may otherwise separate group members; any other delivered
   word, jump, or unmatched launch closes the run.  A run whose last
   launch lacks the trailing step words re-rolls only its complete
   groups and leaves the tail in place.  */

struct run_builder
{
  bool active = false;
  bool proto_set = false;
  unsigned start = 0, len = 0;
  std::vector<rtx_insn *> launches;
  // Step INSNS between launches (for deletion), and their WORD
  // sequences (for identity and encoding).
  std::vector<std::vector<rtx_insn *>> gaps;
  std::vector<rtx_insn *> cur_gap;
  std::vector<unsigned HOST_WIDE_INT> cur_gap_words;
  std::vector<unsigned HOST_WIDE_INT> proto; // the group's step words
};

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

static void
run_begin (run_builder &rb, rtx_insn *insn, unsigned s, unsigned l)
{
  rb = run_builder ();
  rb.active = true;
  rb.start = s;
  rb.len = l;
  rb.launches.push_back (insn);
}

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
		  // Close the gap before this launch.
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
		  // Gap mismatch: keep the complete groups, restart here.
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

/* Collect counted-loop candidates: a single-block loop whose body is
   exactly one fixed playback launch plus the counter step, with a
   provable trip count.  Structural mismatches that name a refusal in
   the taxonomy are dumped; unrelated loops are silently out of
   scope.  */

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

      // Classify the body.
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
		  extra = true; // two launches: a later template class
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
	      extra = true; // other delivered Tensix work in the body
	      break;
	    }
	  if (scalar)
	    {
	      extra = true; // more scalar state than the counter step
	      break;
	    }
	  scalar = insn;
	}
      if (bad || cand.launches.empty ())
	continue;
      if (not_invariant)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-body-not-invariant): loop bb %d"
		     " launch word is synthesized at run time\n", bb->index);
	  continue;
	}
      if (extra || !scalar)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		     " body is not exactly one launch plus the counter"
		     " step\n", bb->index);
	  continue;
	}
      if (loop->unroll)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		     " carries an explicit user unroll request\n", bb->index);
	  continue;
	}

      basic_block preheader = mop_dedicated_loop_preheader (loop);
      if (!preheader)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-trips-unproved): loop bb %d has"
		     " no dedicated preheader\n", bb->index);
	  continue;
	}

      uint64_t trips, final_value;
      rtx_insn *step;
      if (!mop_provable_constant_trips (loop, preheader, &trips, &step,
					&final_value))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-trips-unproved): loop bb %d trip"
		     " count is not provably constant\n", bb->index);
	  continue;
	}
      if (step != scalar)
	{
	  if (dump_file)
	    fprintf (dump_file,
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
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-body-extra-delivery): loop bb %d"
		     " counter exit value is not materializable\n", bb->index);
	  continue;
	}

      // The commit removes the backedge as the taken branch; the prover
      // admits only that shape for a fallthrough-exit loop, but pin it
      // structurally here so the commit can never remove an exit edge.
      if (BRANCH_EDGE (bb)->dest != bb)
	continue;

      cand.iterations = trips;
      cand.step = step;
      cand.counter_final = final_value;
      cand.doomed = cand.launches;
      out.push_back (cand);
    }
}

/* ---- Profitability (corrected concurrent-delivery accounting) ----

   One launch row occupies the issue plane for
   max (len * REPLAY_SLOT, delivered * RISC_PUSH) centislots: during
   replay playback RISC delivery is concurrent with execution
   (rvtt-cost.md, MOP section), so delivered words are free once the
   row is execution-bound.  A MOP row delivers nothing.  The
   configuration block is serial delivery bought at full price.  */

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
      // flags&2 slots fire on every zmask==0 iteration; every consumed
      // slot is written, unused ones with the FIFO-swallowed NOP.
      out.emplace_back (4, cand.step_words[0]);
      out.emplace_back (5, k > 1 ? cand.step_words[1]
				 : XTT_TENSIX_NOP_WORD);
      out.emplace_back (6, k > 2 ? cand.step_words[2]
				 : XTT_TENSIX_NOP_WORD);
    }
}

static HOST_WIDE_INT
mop_li_words (unsigned HOST_WIDE_INT word)
{
  HOST_WIDE_INT v = trunc_int_for_mode (word, SImode);
  return (LUI_OPERAND (v) || SMALL_OPERAND (v)) ? 1 : 2;
}

static HOST_WIDE_INT
mop_config_words (std::vector<std::pair<unsigned,
					unsigned HOST_WIDE_INT>> const
		    &values)
{
  // mop_sync: lui + sw; config base lui; one store per value (zero
  // stores from x0, a repeated value reuses the data register);
  // MOP_CFG + MOP words.
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
      words += 1; // the sw
    }
  return words;
}

static bool
mop_profitable_p (mop_candidate const &cand, HOST_WIDE_INT config_words,
		  HOST_WIDE_INT *benefit_out)
{
  HOST_WIDE_INT push = XTT_REPLAY_COST_RISC_PUSH_X100;
  HOST_WIDE_INT slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
  HOST_WIDE_INT k = (HOST_WIDE_INT) cand.step_words.size ();
  HOST_WIDE_INT exec_row = ((HOST_WIDE_INT) cand.len + k) * slot;
  // RUN rows deliver the launch word plus their step words; LOOP trips
  // additionally deliver the two loop-control words.
  HOST_WIDE_INT delivered = cand.form == mop_candidate::RUN ? 1 + k : 3;
  HOST_WIDE_INT before_row = MAX (exec_row, delivered * push);
  HOST_WIDE_INT benefit = ((HOST_WIDE_INT) cand.iterations
			   * (before_row - exec_row))
    - config_words * push;
  *benefit_out = benefit;

  // The testing/measurement force flag bypasses only this pricing
  // refusal; structural, ownership, and buffer proofs still apply.
  if (riscv_tt_mop_form_force > 0)
    return true;

  HOST_WIDE_INT min_benefit = (riscv_tt_mop_form_min_benefit >= 0
			       ? (HOST_WIDE_INT) riscv_tt_mop_form_min_benefit
			       : XTT_MOP_FORM_MIN_BENEFIT);
  return benefit >= min_benefit;
}

/* ---- Scratch temporaries for the MMIO configuration block ----

   Post-reload, the block needs two dead caller-saved GPRs (address
   base and data word).  Backward DF simulation from the block end to
   the insertion point, the standard late-pass discipline.  */

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

  // RISC-V temporaries t0-t2, t3-t6: caller-saved, never
  // frame-critical.
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

/* Build the configuration + MOP sequence.  Returns null if any emitted
   insn fails recognition (defensive: nothing has been placed in the
   stream yet).  */

static rtx_insn *
build_mop_sequence (mop_candidate const &cand, unsigned scratch[2])
{
  std::vector<std::pair<unsigned, unsigned HOST_WIDE_INT>> values;
  mop_config_values (cand, values);

  rtx base = gen_rtx_REG (SImode, scratch[0]);
  rtx data = gen_rtx_REG (SImode, scratch[1]);

  start_sequence ();
  // Production reprogramming protocol (rvtt-mop-tables.h): drain any
  // still-streaming MOP before touching its template registers.
  emit_move_insn (base, gen_int_mode (XTT_MOP_SYNC_MMIO_ADDR & ~0xfff,
				      SImode));
  emit_volatile_store (base, XTT_MOP_SYNC_MMIO_ADDR & 0xfff, const0_rtx);
  // Template registers: flags, A0 = the launch word, and any flags&2
  // step-word slots (mop_config_values).
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
  // Prove zmask == 0 (high half is persistent thread state), then run
  // iterations = loop_count + 1 times.
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
	    fprintf (dump_file,
		     "MOP-form refused (mop-emit-unrecognized): emitted"
		     " configuration insn is not recognized:\n");
	    dump_insn_slim (dump_file, insn);
	  }
	return nullptr;
      }
  return seq;
}

static bool
commit_candidate (mop_candidate &cand)
{
  unsigned scratch[2];
  if (!find_scratch_gprs (cand.launches.front (), scratch))
    {
      if (dump_file)
	fprintf (dump_file,
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
      // The loop runs once through: materialize the counter's proven
      // exit value, remove the loop control.
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

/* ---- Outward ownership: caller-side MOP-template liveness ----

   See the file header for the proof obligation and the axioms.  The
   analysis runs over the GIMPLE bodies of the transitive caller
   closure of the forming function (callers expand after their callees,
   so those bodies are still gimple when this RTL pass runs; when they
   are not, the proof fails closed).

   Cover-state lattice: a 10-bit must-set -- bits 0..8 are the MOP
   config words at TENSIX_MOP_CFG_BASE + 4*i, bit 9 the MOP_CFG zmask
   high half -- of template state rewritten by the caller since the
   last call into the forming function.  Two states are tracked in
   parallel (entry assumed empty / entry assumed full) so a function's
   effect summarizes as a per-bit gen/pass-through transfer plus the
   entry bits its exposed launches require.  */

constexpr unsigned MOP_STATE_ZMASK = 1u << 9;
constexpr unsigned MOP_STATE_FULL = (1u << 10) - 1;

/* Template state the formed class writes: flags (word 1), A0 (word 3),
   the flags&2 step slots (words 4..6; written whenever any candidate
   carries steps -- the proof conservatively assumes the maximal
   class), and the MOP_CFG zmask high half (the emitted TTMOPCFG 0).  */
constexpr unsigned MOP_CLOBBER_SET
  = (1u << XTT_MOP_CFG_FLAGS_INDEX) | (1u << XTT_MOP_CFG_A0_INDEX)
    | (1u << 4) | (1u << 5) | (1u << 6) | MOP_STATE_ZMASK;

/* What a caller's launch consumes of the clobbered set.  A type-1 MOP
   reads the nine config words and never the zmask; a type-0 (or
   unclassifiable) launch additionally consumes the zmask high half
   (rvtt-mop-tables.h, expander facts).  */
constexpr unsigned MOP_REQ_TYPE1 = MOP_CLOBBER_SET & ~MOP_STATE_ZMASK;
constexpr unsigned MOP_REQ_ANY = MOP_CLOBBER_SET;

struct mop_caller_summary
{
  bool computed = false;
  bool in_progress = false;
  bool valid = false;
  const char *invalid_why = nullptr;
  /* Hazard regardless of entry state (an internal clobber reaches a
     launch without full re-arm).  */
  bool hazard = false;
  /* First hazard's classified event and the function carrying it.  */
  const char *hazard_what = nullptr;
  tree hazard_fn = NULL_TREE;
  /* First entry-exposed launch's classification (for the dump when a
     caller turns the exposure into a hazard).  */
  const char *exposed_what = nullptr;
  tree exposed_fn = NULL_TREE;
  /* Entry bits some reachable launch requires beyond internal cover.  */
  unsigned exposed_need = 0;
  /* Exit cover-state for entry == empty / entry == full (meet over
     exit paths; per-bit transfer out(in) = out_empty | (in & (out_full
     & ~out_empty))).  */
  unsigned out_empty = MOP_STATE_FULL;
  unsigned out_full = MOP_STATE_FULL;
};

struct mop_outward_ctx
{
  tree formee = NULL_TREE;
  cgraph_node *formee_node = nullptr;
  /* Node-stable storage: summaries are referenced across recursive
     insertions.  */
  std::map<tree, mop_caller_summary> summaries;
};

static mop_caller_summary &mop_analyze_fn (mop_outward_ctx &ctx, tree decl);

/* Fold PTR (a pointer value) to a constant byte address, following a
   short SSA chain of casts and constant pointer arithmetic.  */

static bool
mop_pointer_constant_address (tree ptr, unsigned HOST_WIDE_INT *addr,
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
  if (CONVERT_EXPR_CODE_P (code) || code == INTEGER_CST
      || code == SSA_NAME)
    return mop_pointer_constant_address (gimple_assign_rhs1 (def), addr,
					 depth + 1);
  if (code == POINTER_PLUS_EXPR || code == PLUS_EXPR)
    {
      tree off = gimple_assign_rhs2 (def);
      unsigned HOST_WIDE_INT base;
      if (TREE_CODE (off) != INTEGER_CST || !tree_fits_shwi_p (off)
	  || !mop_pointer_constant_address (gimple_assign_rhs1 (def),
					    &base, depth + 1))
	return false;
      *addr = (base + (unsigned HOST_WIDE_INT) tree_to_shwi (off))
	      & 0xffffffff;
      return true;
    }
  return false;
}

/* Fold REF (a store lhs) to a constant byte address if possible.  */

static bool
mop_ref_constant_address (tree ref, unsigned HOST_WIDE_INT *addr)
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
  if (!mop_pointer_constant_address (TREE_OPERAND (base, 0), &a))
    return false;
  a += (unsigned HOST_WIDE_INT) tree_to_shwi (moff);
  a += (unsigned HOST_WIDE_INT) (pos / BITS_PER_UNIT);
  *addr = a & 0xffffffff;
  return true;
}

/* Classify the 32-bit word VAL (a value stored toward a possible
   instruction-FIFO push) by the constant opcode base of its PLUS /
   BIT_IOR composition (AXIOM tt-op-field-discipline, file header).
   Returns the frontend opcode byte, or -1 when no constant base
   pins it.  *TYPE1 is set when bit 23 of the constant base is set.  */

static int
mop_pushed_word_base (tree val, bool *type1, unsigned depth = 0)
{
  if (depth > 12 || !val)
    return -1;
  if (TREE_CODE (val) == INTEGER_CST)
    {
      if (!tree_fits_uhwi_p (val) && !tree_fits_shwi_p (val))
	return -1;
      unsigned HOST_WIDE_INT w
	= TREE_INT_CST_LOW (val) & 0xffffffff;
      *type1 = (w >> 23) & 1;
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
	  bool t1a = false, t1b = false;
	  int a = mop_pushed_word_base (gimple_assign_rhs1 (def), &t1a,
					depth + 1);
	  int b = mop_pushed_word_base (gimple_assign_rhs2 (def), &t1b,
					depth + 1);
	  /* Exactly one side carries the opcode base; two competing
	     bases (or none) leave the word unclassified.  */
	  if (a > 0 && b <= 0)
	    {
	      *type1 = t1a;
	      return a;
	    }
	  if (b > 0 && a <= 0)
	    {
	      *type1 = t1b;
	      return b;
	    }
	  if (a == 0 && b == 0)
	    {
	      *type1 = t1a | t1b;
	      return 0;
	    }
	  return -1;
	}
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	  || code == NOP_EXPR)
	return mop_pushed_word_base (gimple_assign_rhs1 (def), type1,
				     depth + 1);
      /* Shifted single fields below the opcode byte cannot construct
	 an opcode by themselves under the discipline axiom.  */
      if (code == LSHIFT_EXPR || code == BIT_AND_EXPR
	  || code == RSHIFT_EXPR)
	return 0;
      return -1;
    }
  return -1;
}

/* One classified caller event.  */

struct mop_event
{
  enum { BENIGN, LAUNCH, COVER, CLOBBER, COMPOSE } kind = BENIGN;
  unsigned bits = 0;	 /* LAUNCH: required set; COVER: covered set */
  tree callee = NULL_TREE; /* COMPOSE */
  const char *what = nullptr; /* LAUNCH: classification for the dump */
};

/* Resolve one asm operand reference (`%N' digits or `%[name]') to the
   tree whose VALUE reaches the instruction: for an input, its value;
   for an output with a matching-digit input constraint (the `+r'
   split), the matching input's value.  Returns NULL_TREE when the
   value is unresolvable.  */

static tree
mop_asm_operand_value (const gasm *stmt, const char *ref, size_t len)
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
	      && atoi (TREE_STRING_POINTER (cst)) == idx
	      && ISDIGIT (TREE_STRING_POINTER (cst)[0]))
	    return TREE_VALUE (in);
	}
      return NULL_TREE;
    }
  if ((unsigned) idx < nout + nin)
    return TREE_VALUE (gimple_asm_input_op (stmt, idx - nout));
  return NULL_TREE;
}

/* Classify the word VAL delivered (or potentially delivered) by an
   asm; fills EV.  TTINSN_DIRECT marks a word directly issued by a
   `.ttinsn' directive (creditable as a MOP_CFG zmask rewrite).  */

static void
mop_classify_delivered_word (tree word, bool ttinsn_direct, mop_event &ev)
{
  bool type1 = false;
  int opc = mop_pushed_word_base (word, &type1);
  if (opc == (int) XTT_MOP_OPCODE)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = type1 ? MOP_REQ_TYPE1 : MOP_REQ_ANY;
      ev.what = "a raw MOP word in assembly";
    }
  else if (opc == (int) XTT_MOP_CFG_OPCODE && ttinsn_direct)
    {
      /* A directly delivered MOP_CFG rewrites the zmask high half.
	 (A computed 0x03-based word behind a store idiom is never
	 credited: its destination is not provably the FIFO.)  */
      ev.kind = mop_event::COVER;
      ev.bits = MOP_STATE_ZMASK;
    }
  else if (opc < 0)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "an unclassifiable delivered word in assembly";
    }
}

/* Classify a gimple asm.  The canonical raw delivery is a single
   `.ttinsn %0' with one constant input (the TTI_ macro shape the raw
   census audits); the audited scalar templates deliver nothing; and
   base-ISA store/load/consume compositions (the blocking-store and
   memcpy idioms) deliver exactly their stored operands, each
   classified by value.  Everything else is opaque and counts as a
   potential launch.  */

static mop_event
mop_classify_asm (const gasm *stmt)
{
  mop_event ev;
  const char *s = gimple_asm_string (stmt);
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return ev;			/* pure barrier */
  if (!strcmp (s, "fence") || !strcmp (s, "ebreak")
      || !strcmp (s, "la sp, %0")
      || !strcmp (s, ".option push\n.option norelax\n"
		     "la gp, __global_pointer$\n.option pop"))
    return ev;
  if (strncmp (s, ".ttinsn", 7) == 0)
    {
      s += 7;
      while (*s == ' ' || *s == '\t')
	++s;
      if (strcmp (s, "%0") == 0 && gimple_asm_ninputs (stmt) == 1
	  && gimple_asm_noutputs (stmt) == 0)
	{
	  mop_classify_delivered_word
	    (TREE_VALUE (gimple_asm_input_op (stmt, 0)), true, ev);
	  return ev;
	}
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "a non-canonical .ttinsn template";
      return ev;
    }

  /* Base-ISA memory templates: every line must be a load, a consume,
     or a store whose stored operand classifies benign.  */
  while (*s)
    {
      while (*s == ' ' || *s == '\t' || *s == '\n')
	++s;
      if (!*s)
	break;
      const char *tok = s;
      while (*s && *s != ' ' && *s != '\t' && *s != '\n')
	++s;
      size_t tlen = s - tok;
      bool is_store = (tlen == 2
		       && (strncmp (tok, "sw", 2) == 0
			   || strncmp (tok, "sh", 2) == 0
			   || strncmp (tok, "sb", 2) == 0));
      bool is_benign_op
	= ((tlen == 2 && strncmp (tok, "lw", 2) == 0)
	   || (tlen == 2 && strncmp (tok, "lh", 2) == 0)
	   || (tlen == 2 && strncmp (tok, "lb", 2) == 0)
	   || (tlen == 3 && strncmp (tok, "and", 3) == 0)
	   || (tlen == 3 && strncmp (tok, "lhu", 3) == 0)
	   || (tlen == 3 && strncmp (tok, "lbu", 3) == 0)
	   || (tlen == 5 && strncmp (tok, "fence", 5) == 0));
      if (!is_store && !is_benign_op)
	{
	  ev.kind = mop_event::LAUNCH;
	  ev.bits = MOP_REQ_ANY;
	  ev.what = "opaque assembly";
	  return ev;
	}
      if (is_store)
	{
	  /* First operand after the mnemonic is the stored value:
	     an operand reference or a hard register (opaque).  */
	  while (*s == ' ' || *s == '\t')
	    ++s;
	  if (*s != '%')
	    {
	      ev.kind = mop_event::LAUNCH;
	      ev.bits = MOP_REQ_ANY;
	      ev.what = "opaque assembly";
	      return ev;
	    }
	  ++s;
	  const char *ref = s;
	  if (*s == '[')
	    {
	      while (*s && *s != ']')
		++s;
	      if (*s == ']')
		++s;
	    }
	  else
	    while (ISDIGIT (*s))
	      ++s;
	  tree word = mop_asm_operand_value (stmt, ref, s - ref);
	  mop_event wev;
	  mop_classify_delivered_word (word, false, wev);
	  if (wev.kind == mop_event::LAUNCH)
	    return wev;
	}
      /* Skip the rest of the line.  */
      while (*s && *s != '\n')
	++s;
    }
  return ev;
}

/* Classify a gimple store.  */

static mop_event
mop_classify_store (gimple *stmt)
{
  mop_event ev;
  tree lhs = gimple_get_lhs (stmt);
  if (!lhs || TREE_CODE (lhs) == SSA_NAME)
    return ev;

  unsigned HOST_WIDE_INT addr;
  if (mop_ref_constant_address (lhs, &addr))
    {
      unsigned HOST_WIDE_INT base = XTT_MOP_CFG_MMIO_BASE & 0xffffffff;
      if (addr >= base && addr < base + 4 * 9 && (addr - base) % 4 == 0)
	{
	  ev.kind = mop_event::COVER;
	  ev.bits = 1u << ((addr - base) / 4);
	  return ev;
	}
      /* Any other constant MMIO address: it can only deliver an
	 instruction if it is an instruction-FIFO alias, so classify
	 the stored word.  */
    }
  else
    {
      tree base = get_base_address (lhs);
      /* A store into a known non-volatile object is memory, not MMIO
	 (hardware registers are declared volatile).  */
      if (!TREE_THIS_VOLATILE (lhs)
	  && (!base || !DECL_P (base) || !TREE_THIS_VOLATILE (base)))
	return ev;
    }

  tree val = gimple_assign_rhs1 (stmt);
  bool type1 = false;
  int opc = mop_pushed_word_base (val, &type1);
  if (opc == (int) XTT_MOP_OPCODE)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = type1 ? MOP_REQ_TYPE1 : MOP_REQ_ANY;
      ev.what = "a computed MOP push";
    }
  else if (opc < 0)
    {
      ev.kind = mop_event::LAUNCH;
      ev.bits = MOP_REQ_ANY;
      ev.what = "an unclassifiable volatile store";
    }
  /* opc == XTT_MOP_CFG_OPCODE: a zmask WRITE at worst -- never a
     template consumer, and not creditable as cover (the destination
     is not provably the FIFO).  Benign.  */
  return ev;
}

/* Classify one gimple statement of a caller body.  */

static mop_event
mop_classify_stmt (mop_outward_ctx &ctx, gimple *stmt)
{
  mop_event ev;
  if (is_gimple_debug (stmt))
    return ev;
  if (const gasm *a = dyn_cast<const gasm *> (stmt))
    return mop_classify_asm (a);
  if (is_gimple_call (stmt))
    {
      if (gimple_call_internal_p (stmt))
	return ev;
      tree fndecl = gimple_call_fndecl (stmt);
      if (!fndecl)
	{
	  /* Indirect call: cannot reach the forming function (the
	     closure refuses address-taken members), but its body is
	     unknown -- a potential launch.  */
	  ev.kind = mop_event::LAUNCH;
	  ev.bits = MOP_REQ_ANY;
	  ev.what = "an indirect call";
	  return ev;
	}
      if (fndecl == ctx.formee)
	{
	  ev.kind = mop_event::CLOBBER;
	  return ev;
	}
      /* A call through an alias or clone of the forming function is
	 the same clobber.  */
      if (ctx.formee_node)
	if (cgraph_node *cn = cgraph_node::get (fndecl))
	  if (cn->ultimate_alias_target () == ctx.formee_node)
	    {
	      ev.kind = mop_event::CLOBBER;
	      return ev;
	    }
      if (const rvtt_insn_data *d = rvtt_get_insn_data (stmt))
	{
	  /* rvtt builtins deliver typed non-MOP words (REPLAY, SETRWC,
	     SFPU, sync, region markers); no gimple-level builtin emits
	     MOP or MOP_CFG today.  Guard the name anyway.  */
	  if (strncmp (d->name, "ttmop", 5) == 0)
	    {
	      ev.kind = mop_event::LAUNCH;
	      ev.bits = MOP_REQ_ANY;
	      ev.what = "a ttmop builtin";
	    }
	  return ev;
	}
      if (fndecl_built_in_p (fndecl))
	return ev;
      if (cgraph_node *cn = cgraph_node::get (fndecl))
	if (cn->definition || DECL_STRUCT_FUNCTION (fndecl))
	  {
	    ev.kind = mop_event::COMPOSE;
	    ev.callee = fndecl;
	    return ev;
	  }
      /* Extern with no body in the TU: crt0/libc scalar code under the
	 kernel-single-TU axiom -- delivers no Tensix work.  */
      return ev;
    }
  if (gimple_store_p (stmt) && is_gimple_assign (stmt))
    return mop_classify_store (stmt);
  return ev;
}

/* Apply EV to the parallel states (SE = cover assuming empty entry,
   SF = assuming full entry).  When RECORD is non-null, accumulate
   hazards and entry requirements into it (FNDECL names the function
   being analyzed for the hazard detail).  Returns false when the
   analysis becomes invalid (unanalyzable callee).  */

static bool
mop_apply_event (mop_outward_ctx &ctx, const mop_event &ev,
		 unsigned *se, unsigned *sf,
		 mop_caller_summary *record, tree fndecl)
{
  switch (ev.kind)
    {
    case mop_event::BENIGN:
      return true;
    case mop_event::LAUNCH:
      if (record)
	{
	  if ((ev.bits & ~*sf) && !record->hazard)
	    {
	      record->hazard = true;
	      record->hazard_what = ev.what;
	      record->hazard_fn = fndecl;
	    }
	  if ((ev.bits & *sf & ~*se) && !record->exposed_what)
	    {
	      record->exposed_what = ev.what;
	      record->exposed_fn = fndecl;
	    }
	  record->exposed_need |= ev.bits & *sf & ~*se;
	}
      return true;
    case mop_event::COVER:
      *se |= ev.bits;
      *sf |= ev.bits;
      return true;
    case mop_event::CLOBBER:
      *se = 0;
      *sf = 0;
      return true;
    case mop_event::COMPOSE:
      {
	mop_caller_summary &sub = mop_analyze_fn (ctx, ev.callee);
	if (!sub.valid)
	  return false;
	if (record)
	  {
	    if (sub.hazard && !record->hazard)
	      {
		record->hazard = true;
		record->hazard_what = sub.hazard_what;
		record->hazard_fn = sub.hazard_fn;
	      }
	    if ((sub.exposed_need & ~*sf) && !record->hazard)
	      {
		record->hazard = true;
		record->hazard_what = sub.exposed_what
		  ? sub.exposed_what : "an exposed launch in a callee";
		record->hazard_fn = sub.exposed_fn
		  ? sub.exposed_fn : ev.callee;
	      }
	    if ((sub.exposed_need & *sf & ~*se) && !record->exposed_what)
	      {
		record->exposed_what = sub.exposed_what;
		record->exposed_fn = sub.exposed_fn;
	      }
	    record->exposed_need |= sub.exposed_need & *sf & ~*se;
	  }
	unsigned transp = sub.out_full & ~sub.out_empty;
	*se = sub.out_empty | (*se & transp);
	*sf = sub.out_empty | (*sf & transp);
	return true;
      }
    }
  return true;
}

/* Analyze DECL's gimple body: a forward must-dataflow over the pair
   state, then a recording pass.  Memoized in CTX; recursion and
   unavailable bodies invalidate.  */

static mop_caller_summary &
mop_analyze_fn (mop_outward_ctx &ctx, tree decl)
{
  mop_caller_summary &sum = ctx.summaries[decl];
  if (sum.computed)
    return sum;
  if (sum.in_progress)
    {
      /* Recursive caller chain: no epoch discipline is provable.  */
      sum.computed = true;
      sum.valid = false;
      sum.invalid_why = "recursive call chain";
      return sum;
    }
  sum.in_progress = true;

  function *fn = DECL_STRUCT_FUNCTION (decl);
  if (!fn || !fn->cfg || (fn->curr_properties & PROP_rtl))
    {
      sum.in_progress = false;
      sum.computed = true;
      sum.valid = false;
      sum.invalid_why = "body not analyzable at formation time";
      return sum;
    }

  unsigned n = last_basic_block_for_fn (fn);
  /* Per-BB IN states; TOP = all-ones on both tracks.  */
  std::vector<unsigned> in_se (n, MOP_STATE_FULL);
  std::vector<unsigned> in_sf (n, MOP_STATE_FULL);
  basic_block entry_bb = ENTRY_BLOCK_PTR_FOR_FN (fn);

  bool valid = true;
  const char *invalid_why = nullptr;

  /* Fixpoint (states only descend).  */
  bool changed = true;
  unsigned iter = 0;
  while (changed && valid && iter++ < 64)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned se, sf;
	  bool first = true;
	  edge e;
	  edge_iterator ei;
	  se = sf = MOP_STATE_FULL;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      unsigned pse, psf;
	      if (e->src == entry_bb)
		{
		  pse = 0;
		  psf = MOP_STATE_FULL;
		}
	      else
		{
		  /* Predecessor OUT: recompute cheaply by transfer of
		     its stored IN (bodies are small; correctness over
		     speed).  */
		  pse = in_se[e->src->index];
		  psf = in_sf[e->src->index];
		  for (gimple_stmt_iterator gsi
			 = gsi_start_bb (e->src);
		       !gsi_end_p (gsi); gsi_next (&gsi))
		    {
		      mop_event ev
			= mop_classify_stmt (ctx, gsi_stmt (gsi));
		      if (!mop_apply_event (ctx, ev, &pse, &psf,
					    nullptr, decl))
			{
			  valid = false;
			  invalid_why = "unanalyzable callee";
			}
		    }
		}
	      if (first)
		{
		  se = pse;
		  sf = psf;
		  first = false;
		}
	      else
		{
		  se &= pse;
		  sf &= psf;
		}
	    }
	  if (first)
	    {
	      /* Unreachable block; keep TOP.  */
	      continue;
	    }
	  if (se != in_se[bb->index] || sf != in_sf[bb->index])
	    {
	      /* Must-meet only descends.  */
	      in_se[bb->index] &= se;
	      in_sf[bb->index] &= sf;
	      changed = true;
	    }
	}
    }
  if (changed && valid)
    {
      /* Should be unreachable (a 10-bit must-lattice descends in
	 bounded steps); fail closed rather than trust an unconverged
	 state.  */
      valid = false;
      invalid_why = "cover dataflow did not converge";
    }

  /* Recording pass: hazards, entry requirements, exit meets.  */
  unsigned out_e = MOP_STATE_FULL, out_f = MOP_STATE_FULL;
  bool have_exit = false;
  if (valid)
    {
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned se = in_se[bb->index];
	  unsigned sf = in_sf[bb->index];
	  edge e;
	  edge_iterator ei;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      mop_event ev = mop_classify_stmt (ctx, gsi_stmt (gsi));
	      if (!mop_apply_event (ctx, ev, &se, &sf, &sum, decl))
		{
		  valid = false;
		  invalid_why = "unanalyzable callee";
		}
	    }
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->dest == EXIT_BLOCK_PTR_FOR_FN (fn))
	      {
		out_e &= se;
		out_f &= sf;
		have_exit = true;
	      }
	}
    }
  if (!have_exit)
    {
      out_e = MOP_STATE_FULL;
      out_f = MOP_STATE_FULL;
    }

  sum.in_progress = false;
  sum.computed = true;
  sum.valid = valid;
  sum.invalid_why = invalid_why;
  sum.out_empty = out_e;
  sum.out_full = out_f;
  return sum;
}

/* Discharge the outward ownership obligation for CFN.  On failure,
   *WHY and *WHY_FN carry the refusal detail; on success *HOW names the
   discharged form for the dump.  */

static bool
mop_outward_owned_p (function *cfn, const char **why, const char **why_fn,
		     const char **how)
{
  tree decl = cfn->decl;
  *why = nullptr;
  *why_fn = nullptr;

  if (DECL_NAME (decl) && MAIN_NAME_P (DECL_NAME (decl)))
    {
      /* The kernel entry: its only caller is crt0, which delivers no
	 Tensix work (AXIOM crt0-benign).  */
      *how = "kernel entry (crt0-benign axiom)";
      return true;
    }

  cgraph_node *node = cgraph_node::get (decl);
  if (!node)
    {
      *why = "no callgraph node for the function";
      return false;
    }

  /* Transitive caller closure under the kernel-single-TU axiom.  */
  auto_vec<cgraph_node *> closure;
  hash_set<cgraph_node *> seen;
  auto_vec<cgraph_node *> work;
  work.safe_push (node);
  seen.add (node);
  while (!work.is_empty ())
    {
      cgraph_node *cur = work.pop ();
      if (cur->address_taken)
	{
	  *why = "address-taken function on the caller chain";
	  *why_fn = cur->dump_name ();
	  return false;
	}
      for (cgraph_edge *e = cur->callers; e; e = e->next_caller)
	{
	  cgraph_node *c = e->caller;
	  if (c->inlined_to)
	    c = c->inlined_to;
	  if (seen.add (c))
	    continue;
	  closure.safe_push (c);
	  work.safe_push (c);
	}
    }

  if (closure.is_empty ())
    {
      /* No caller inside the thread program: outermost by the
	 kernel-single-TU + crt0-benign axioms.  */
      *how = "outermost (no caller in the TU)";
      return true;
    }

  mop_outward_ctx ctx;
  ctx.formee = decl;
  ctx.formee_node = node;

  unsigned roots = 0;
  for (cgraph_node *m : closure)
    {
      if (m->callers)
	continue;		/* analyzed via its own callers */
      ++roots;
      mop_caller_summary &sum = mop_analyze_fn (ctx, m->decl);
      if (!sum.valid)
	{
	  *why = sum.invalid_why ? sum.invalid_why
				 : "caller not analyzable";
	  *why_fn = m->dump_name ();
	  return false;
	}
      if (sum.hazard)
	{
	  static char detail[256];
	  const char *what = sum.hazard_what ? sum.hazard_what
					     : "a MOP launch";
	  const char *where
	    = sum.hazard_fn ? lang_hooks.decl_printable_name (sum.hazard_fn, 2)
			    : m->dump_name ();
	  snprintf (detail, sizeof (detail),
		    "%s in '%s' is reachable after a call to this"
		    " function without a full template re-arm",
		    what, where);
	  *why = detail;
	  *why_fn = m->dump_name ();
	  return false;
	}
      /* Root entry: no caller-programmed template can be live (the
	 axioms above), so exposed_need at a root is pre-existing
	 caller-owned state, not our clobber.  */
    }

  if (roots == 0)
    {
      /* Every closure member has callers: a cycle with no entry.  */
      *why = "recursive caller chain";
      return false;
    }

  *how = "every caller root re-arms the template before its next"
	 " post-return MOP launch";
  return true;
}

/* ---- Driver ---- */

static void
transform (function *cfn)
{
  // MOP config is thread-shared mutable state.  A call or opaque asm
  // can program or consume it invisibly (production kernels program
  // templates from ordinary C++ and inline asm); refuse the whole
  // function rather than claim ownership this increment cannot prove.
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
      if (dump_file)
	fprintf (dump_file,
		 "MOP-form refused (mop-config-unowned): function contains"
		 " a call or opaque asm; %u candidate(s) dropped\n",
		 unsigned (candidates.size ()));
      return;
    }

  // The formed template survives this function's return in
  // thread-shared, RISC-write-only registers: prove no caller can
  // launch a live template of its own after we return without fully
  // re-arming it first (file header, outward ownership).
  {
    const char *why = nullptr, *why_fn = nullptr, *how = nullptr;
    if (!mop_outward_owned_p (cfn, &why, &why_fn, &how))
      {
	if (dump_file)
	  {
	    if (why_fn)
	      fprintf (dump_file,
		       "MOP-form refused (mop-caller-template-live-"
		       "unproven): %s (%s); %u candidate(s) dropped\n",
		       why, why_fn, unsigned (candidates.size ()));
	    else
	      fprintf (dump_file,
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
	  if (dump_file)
	    fprintf (dump_file,
		     "MOP-form refused (mop-replay-window-overflow): launch"
		     " [%u,+%u) exceeds the %u-slot replay buffer"
		     " (S+L > %u)\n",
		     cand.start, cand.len, buffer_size, buffer_size);
	  continue;
	}
      if (cand.iterations < 2 || cand.iterations > XTT_MOP0_MAX_ITERATIONS)
	{
	  if (dump_file)
	    fprintf (dump_file,
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
	  if (dump_file)
	    fprintf (dump_file,
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

  // Single-epoch conservatism: reprogramming the template for a second
  // MOP inside one function needs the ownership-epoch machinery (a
  // later stage); every other profitable candidate refuses.
  if (dump_file)
    for (auto &cand : candidates)
      if (&cand != best && cand.start + cand.len <= buffer_size
	  && cand.iterations >= 2
	  && cand.iterations <= XTT_MOP0_MAX_ITERATIONS)
	fprintf (dump_file,
		 "MOP-form refused (mop-config-epoch): a MOP was already"
		 " formed in this function (candidate %lu x [%u,+%u))\n",
		 (unsigned long) cand.iterations, cand.start, cand.len);
}

const pass_data pass_data_rvtt_mop_form =
{
  RTL_PASS, /* type */
  "rvtt_mop_form", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
    // QSR hard-refuses: its MOP encoding and expander semantics are not
    // in the capability table (rvtt-mop-tables.h).
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
}; // class pass_rvtt_mop_form

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_mop_form (gcc::context *ctxt)
{
  return new pass_rvtt_mop_form (ctxt);
}
