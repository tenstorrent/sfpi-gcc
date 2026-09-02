/* IR-free delivery-cost arithmetic core for the Tensix backend.
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

/* THE ONE DELIVERY-COST API.

   Every PUSH/SLOT issue-word pricing formula of the replay/planner/
   residency families lives HERE, once.  The audited machine constants
   themselves stay in rvtt-cost.md `define_constants' (the GCC-idiomatic
   carrier, compiled into insn-constants.h); rvtt-delivery-cost.cc is
   the only place that turns them into a `cost_table', and every
   consumer prices through the functions below.  This header is
   deliberately IR-free (no rtx, no gimple, no options): the standalone
   unit test rvtt-delivery-cost-test.cc compiles it with a fixture
   table and pins the formulas -- including a recorded
   hardshrink-kernel refusal (-383) that validates the delivery-shape
   downstream mirror against the RTL gate's arithmetic.

   MODULE INVARIANT (refusal-biased one-sidedness): the
   formation-vs-replay arbitration prices the replay-delivered
   alternative at its STEADY-STATE LOWER BOUND -- every row instance
   re-executes its words at the slot rate with delivery hidden, and the
   record pass and launch words are charged at ZERO
   (`ims_replay_alt_cost_x100').  For the same rows, any achievable
   replay delivery costs at least that bound (record, launches, and
   turnaround are all non-negative charges on top of it), so a
   formation that cannot beat even the ideal replay delivery refuses --
   the arbitration can only over-refuse formation, never over-admit it.
   The unit test pins this ordering against `replay_pricing'.

   All arithmetic is exact integer arithmetic; re-hosting a formula
   here must be BIT-EQUAL to the consumer spelling it replaces
   (per-consumer flag_checking recompute-asserts held the old inline
   spellings against this module for one pin -- discharged with zero
   inequalities and deleted at pin 51).  */

#ifndef GCC_RVTT_DELIVERY_COST_CORE_H
#define GCC_RVTT_DELIVERY_COST_CORE_H

#include <stdint.h>

namespace rvtt_delivery_cost {

/* The audited centislot rates (rvtt-cost.md define_constants;
   hardware-calibrated: PUSH=123 measured RISC-pushed word, SLOT=100
   replay-delivered word, TURNAROUND=70 per-launch reissue turnaround,
   RECORD_OVERHEAD=300 per-record-pass engine overhead).  Constructed
   from XTT_REPLAY_COST_* by rvtt-delivery-cost.cc only.  */

struct cost_table
{
  int64_t push_x100;		/* XTT_REPLAY_COST_RISC_PUSH_X100 */
  int64_t slot_x100;		/* XTT_REPLAY_COST_REPLAY_SLOT_X100 */
  int64_t turnaround_x100;	/* XTT_REPLAY_COST_TURNAROUND_X100 */
  int64_t record_overhead_x100;	/* XTT_REPLAY_COST_RECORD_OVERHEAD_X100 */
};

/* The two delivery planes.  */

enum plane
{
  PLANE_RISC_PUSH,	/* word delivered by a RISC push */
  PLANE_REPLAY_SLOT	/* word delivered from the replay buffer */
};

/* Centislot price of WORDS issue words delivered on plane P.  */

inline int64_t
words_to_centislots (const cost_table &t, int64_t words, plane p)
{
  return words * (p == PLANE_RISC_PUSH ? t.push_x100 : t.slot_x100);
}

/* The delivery plane of a planner-formed launch calendar word:
   REPLAY_WRAPPED selects the planner-replay delivery increment
   (-mtt-tensix-macro-planner-replay wraps launches in automatic
   replay recording).  */

inline plane
planner_word_plane (bool replay_wrapped)
{
  return replay_wrapped ? PLANE_REPLAY_SLOT : PLANE_RISC_PUSH;
}

/* Keep the products of trip-weighted profitability inside 64 bits
   (the loop_trip_weight discipline, shared verbatim with the
   crosscall init-hoist caller weight): scale the body count down
   by octets until it fits 48 bits, scaling the entry count in
   lockstep (floor 1).  */

inline void
scale_trip_weight (int64_t *body, int64_t *entry)
{
  while (*body > (int64_t) 1 << 48)
    {
      *body >>= 8;
      *entry = *entry >> 8 ? *entry >> 8 : 1;
    }
}

/* The cross-multiplied run-amortization inequality, one spelling
   (macro-planner run/loop profitability; init-hoist-aware run
   pricing):

     entry_cost * entry_weight + per_run * body_weight
       < explicit_side * body_weight

   Unweighted per-run amortization (the frozen conservative-per-run
   discipline) is the same inequality at weights 1/1.  */

inline bool
run_amortized_p (uint64_t entry_cost, uint64_t per_run,
		 uint64_t explicit_side, uint64_t entry_weight,
		 uint64_t body_weight)
{
  return entry_cost * entry_weight + per_run * body_weight
    < explicit_side * body_weight;
}

/* SFPLOADI issue-word count of materializing the 32-bit value W
   through an LREG -- the one value-classification formula behind both
   prior spellings (rtl-rvtt-macro-planner.cc config_word_loadi_issues
   and gimple-rvtt-invariant.cc rvtt_sfpxloadi_materialization_cost;
   proven equivalent at migration).  Mirrors rvtt_emit_sfpxloadi's
   forms: one issue for a sign-extended 16-bit immediate, an upper-only
   image, or the FLOATA binary16-exponent encoding; two otherwise.
   This models only the target's immediate encodings -- no particular
   value or source pattern is recognized.  */

inline unsigned
loadi_issue_words (uint32_t w)
{
  if (w <= 0x7fff || w >= 0xffff8000u || w <= 0xffff || !(w & 0xffff))
    return 1;
  if (!(w & 0x1fff))
    {
      unsigned exp = (w >> 23) & 0xff;
      if (exp < 127 + 16 && exp >= 127 - 14)
	return 1;
    }
  return 2;
}

/* ------------------------------------------------------------------
   Replay window pricing: the one spelling of the before/after
   delivery arithmetic previously re-instantiated by
   hoist_profitable_p, counted_peel_profitable_p (rtl-rvtt-replay.cc)
   and the delivery-shape downstream mirrors (rvtt-bnb.cc).  */

/* The priced shape.  */

enum replay_shape
{
  /* Counted-loop capture (no re-record: the body launches a
     preheader-recorded window; also the shape of every
     body_rerecords=false hoist).  */
  SHAPE_COUNTED,
  /* Re-record body under the default saturation-calibrated model
     (execution-bound / delivery-bound split decided by the binding
     resource; the drain-inclusive completion contract charges the
     full record delivery on the execution-bound side).  */
  SHAPE_RERECORD,
  /* Re-record body under the record-hoist measurement model
     (-mtt-tensix-optimize-replay-record-hoist, proven trips): pure
     delivery delta; execution cancels by fixed-encoding admission.  */
  SHAPE_RECORD_HOIST,
  /* Record-hoist measurement model at a RUNTIME trip count (structural
     trips >= 1): admit at the 2-trip break-even.  */
  SHAPE_RECORD_HOIST_RUNTIME,
  /* Exec-while-record first-trip peel of a counted capture:
     the peel pays the capture word and the record-engine
     overhead once; every remaining trip becomes one launch.  */
  SHAPE_COUNTED_PEEL
};

/* The re-record shape selected by the flag pair, one spelling for the
   RTL gate and the downstream mirror (drift between them was the
   documented delivery-shape MODEL SEAM).  */

inline replay_shape
rerecord_shape (bool record_hoist_enabled, bool completion_guard,
		bool trips_proven)
{
  if (record_hoist_enabled && !completion_guard)
    return trips_proven ? SHAPE_RECORD_HOIST : SHAPE_RECORD_HOIST_RUNTIME;
  return SHAPE_RERECORD;
}

/* Every term a consumer dumps or asserts on, plus the verdict.  */

struct replay_price
{
  /* Shared derived quantities.  */
  int64_t exec;			/* exec_slots at the slot rate */
  int64_t deliver_body;		/* payload words at the push rate */
  int64_t deliver_record;	/* capture word + payload at push */
  /* Default-model terms.  */
  int64_t before;		/* per-trip cost, unhoisted world */
  int64_t after;		/* per-trip cost, hoisted world */
  int64_t record;		/* once-per-placement charge */
  int64_t surplus;		/* delivery-bound sibling-run surplus */
  bool exec_bound;		/* re-record binding resource */
  bool hidden;			/* record delivery hidden by surplus */
  /* Measurement-model terms.  */
  int64_t per_trip;		/* per-trip delivery saving */
  int64_t record_once;		/* once-per-placement delivery */
  int64_t exposure;		/* runtime-trip single-trip exposure */
  /* Verdict.  */
  int64_t benefit;		/* modeled centislot benefit */
  bool profitable;		/* benefit clears MIN_BENEFIT (and the
				   runtime shape's per_trip > 0) */
};

/* Price one replay window of WORDS delivered payload words executing
   in EXEC_SLOTS interlock-audited slots over TRIPS trips (ignored by
   the runtime shape), against MIN_BENEFIT centislots.  LAUNCH_RUN is
   the contiguous sibling-launch run feeding the delivery-bound
   saturation term; DRAIN_CONTRACT selects the completion guard's
   full-record charge on the execution-bound re-record side.  */

inline replay_price
replay_pricing (const cost_table &t, replay_shape shape, int64_t trips,
		int64_t words, int64_t exec_slots, int64_t launch_run,
		bool drain_contract, int64_t min_benefit)
{
  replay_price r = replay_price ();
  r.exec = words_to_centislots (t, exec_slots, PLANE_REPLAY_SLOT);
  r.deliver_body = words_to_centislots (t, words, PLANE_RISC_PUSH);
  r.deliver_record = words_to_centislots (t, 1 + words, PLANE_RISC_PUSH);

  if (shape == SHAPE_RECORD_HOIST || shape == SHAPE_RECORD_HOIST_RUNTIME)
    {
      /* Pure delivery delta (DX-F3 issue-side accounting): the hoisted
	 world converts the first clone to one more playback launch per
	 trip, charged at the audited turnaround constant.  */
      r.record_once = r.deliver_record + t.record_overhead_x100;
      r.per_trip = r.deliver_body - t.turnaround_x100;
      if (shape == SHAPE_RECORD_HOIST_RUNTIME)
	{
	  /* Monotone in the realized trip count: admit when the 2-trip
	     benefit clears the audited margin; the worst realized
	     outcome is the single-trip exposure.  */
	  r.benefit = 2 * r.per_trip - r.record_once;
	  r.exposure = r.record_once - r.per_trip;
	  r.profitable = r.per_trip > 0 && r.benefit >= min_benefit;
	}
      else
	{
	  r.benefit = trips * r.per_trip - r.record_once;
	  r.profitable = r.benefit >= min_benefit;
	}
      return r;
    }

  r.after = t.push_x100 > r.exec + t.turnaround_x100
    ? t.push_x100 : r.exec + t.turnaround_x100;

  switch (shape)
    {
    case SHAPE_COUNTED:
      r.before = r.deliver_body > r.exec ? r.deliver_body : r.exec;
      r.record = r.deliver_record + t.record_overhead_x100;
      r.benefit = trips * (r.before - r.after) - r.record;
      break;

    case SHAPE_COUNTED_PEEL:
      r.before = r.deliver_body > r.exec ? r.deliver_body : r.exec;
      r.record = t.push_x100 + t.record_overhead_x100;
      r.benefit = (trips - 1) * (r.before - r.after) - r.record;
      break;

    case SHAPE_RERECORD:
      r.exec_bound = r.exec >= r.deliver_record;
      if (r.exec_bound)
	{
	  /* Execution-bound: the record engine's per-pass overhead is
	     on the critical path; the hoisted pass's delivery hides
	     behind the loop's execution backlog -- unless the
	     completion contract charges it in full.  */
	  r.before = r.exec + t.record_overhead_x100;
	  r.record = t.record_overhead_x100;
	  if (drain_contract)
	    r.record += r.deliver_record;
	}
      else
	{
	  /* Delivery-bound, with the execution-saturation context
	     term: a contiguous sibling-launch run with enough
	     execution surplus hides the record pass's delivery.  */
	  r.before = r.deliver_record;
	  r.record = r.deliver_record + t.record_overhead_x100;
	  r.surplus = launch_run * (r.exec - t.push_x100);
	  if (r.surplus >= r.deliver_record)
	    {
	      r.before = r.after;
	      r.hidden = true;
	    }
	}
      r.benefit = trips * (r.before - r.after) - r.record;
      break;

    default:
      break;
    }
  r.profitable = r.benefit >= min_benefit;
  return r;
}

/* ------------------------------------------------------------------
   Formation-vs-replay arbitration terms (rtl-rvtt-macro-planner.cc
   -mtt-tensix-macro-ims).  */

/* Centislot price of a formed calendar: CONFIG_WORDS descriptor-prefix
   issues at the push rate, RUN_SLOTS calendar slots at the push rate
   (or the replay slot rate when the planner-replay delivery increment
   wraps the launches), DRAIN_SLOTS at the slot rate.  */

inline uint64_t
ims_formed_cost_x100 (const cost_table &t, uint64_t config_words,
		      uint64_t run_slots, uint64_t drain_slots,
		      bool replay_wrapped)
{
  return (uint64_t) words_to_centislots (t, config_words, PLANE_RISC_PUSH)
    + (uint64_t) words_to_centislots (t, run_slots,
				      planner_word_plane (replay_wrapped))
    + (uint64_t) words_to_centislots (t, drain_slots, PLANE_REPLAY_SLOT);
}

/* Steady-state LOWER BOUND of the replay-delivered explicit
   alternative (the module invariant above): every row instance
   re-executes its words at the slot rate with delivery hidden; record
   and launch words charged at zero (refusal-biased).  */

inline uint64_t
ims_replay_alt_cost_x100 (const cost_table &t, uint64_t row_instances,
			  uint64_t row_words)
{
  return row_instances
    * (uint64_t) words_to_centislots (t, row_words, PLANE_REPLAY_SLOT);
}

/* ------------------------------------------------------------------
   Residency peel break-even (gimple-rvtt-prgm-const.cc; rvtt-cost.md
   residency-peel model).  The loop saves the candidates' SUM_W
   materialization words at SLOT each on every iteration after the
   first; the programming costs PUSH per staged word plus PUSH per
   SFPCONFIG (NPROG); the peeled BODY_W-word body changes delivery
   class from replayed SLOT to pushed PUSH once.  Returns the proven
   trip count the break-even requires.  Deliberately in the original's
   32-bit unsigned domain (bit-equal re-hosting).  */

inline unsigned
residency_peel_break_even_trips (const cost_table &t, unsigned sum_w,
				 unsigned nprog, unsigned body_w)
{
  unsigned push = (unsigned) t.push_x100;
  unsigned slot = (unsigned) t.slot_x100;
  unsigned cost = push * (sum_w + nprog) + (push - slot) * body_w;
  return 1 + (cost + slot * sum_w - 1) / (slot * sum_w);
}

/* ------------------------------------------------------------------
   Hoisted-window per-trip delivered-issue words (rtl-rvtt-replay.cc
   window sizing): LAUNCHES full playback launches, one
   partial prefix launch when HAS_TRIM, and every word the shape
   leaves inline.  */

inline unsigned
window_trip_issue_words (unsigned launches, bool has_trim,
			 unsigned inline_words)
{
  return launches + (has_trim ? 1 : 0) + inline_words;
}

} /* namespace rvtt_delivery_cost */

#endif /* GCC_RVTT_DELIVERY_COST_CORE_H */
