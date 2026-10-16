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

/* Delivery-cost arithmetic for the replay, macro-planner and residency
   transformations.  The machine constants are define_constants in
   rvtt-cost.md; rvtt-delivery-cost.cc builds a cost_table from them and
   passes price through the functions below.  This header uses no IR or
   options, so that the unit test rvtt-delivery-cost-test.cc can check the
   formulas against a fixture table.

   The formation-vs-replay comparison prices the replay alternative at a
   lower bound: every row instance executes its words at the replay slot
   rate with delivery hidden, and the record pass and launches cost nothing
   (ims_replay_alt_cost_x100).  Any real replay delivery costs at least
   that much, so the comparison can refuse a profitable formation but never
   accept an unprofitable one.  The unit test checks this ordering against
   replay_pricing.

   All arithmetic is exact integer arithmetic.  */

#ifndef GCC_RVTT_DELIVERY_COST_CORE_H
#define GCC_RVTT_DELIVERY_COST_CORE_H

#include <stdint.h>

namespace rvtt_delivery_cost {

/* Delivery rates in centislots, from the rvtt-cost.md define_constants:
   the cost of a RISC-pushed word, of a replay-delivered word, of the
   turnaround of each replay launch, and of the record engine's overhead
   per record pass.  */

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

/* Return the cost in centislots of WORDS issue words delivered on
   plane P.  */

inline int64_t
words_to_centislots (const cost_table &t, int64_t words, plane p)
{
  return words * (p == PLANE_RISC_PUSH ? t.push_x100 : t.slot_x100);
}

/* Return the delivery plane of a word in a planner-formed launch
   calendar.  REPLAY_WRAPPED is true when -mtt-tensix-macro-planner-replay
   wraps the launches in replay recording.  */

inline plane
planner_word_plane (bool replay_wrapped)
{
  return replay_wrapped ? PLANE_REPLAY_SLOT : PLANE_RISC_PUSH;
}

/* Scale the execution counts *BODY and *ENTRY down by the same factor
   until *BODY fits in 48 bits (keeping *ENTRY at least 1), so that
   trip-weighted profitability products fit in 64 bits.  */

inline void
scale_trip_weight (int64_t *body, int64_t *entry)
{
  while (*body > (int64_t) 1 << 48)
    {
      *body >>= 8;
      *entry = *entry >> 8 ? *entry >> 8 : 1;
    }
}

/* Return true if a setup cost ENTRY_COST paid ENTRY_WEIGHT times plus
   PER_RUN paid BODY_WEIGHT times is cheaper than EXPLICIT_SIDE paid
   BODY_WEIGHT times:

     entry_cost * entry_weight + per_run * body_weight
       < explicit_side * body_weight

   Unweighted amortization uses weights of 1.  */

inline bool
run_amortized_p (uint64_t entry_cost, uint64_t per_run,
		 uint64_t explicit_side, uint64_t entry_weight,
		 uint64_t body_weight)
{
  return entry_cost * entry_weight + per_run * body_weight
    < explicit_side * body_weight;
}

/* Return the number of SFPLOADI instructions needed to load the 32-bit
   value W into an LREG, following the forms rvtt_emit_sfpxloadi uses: one
   for a sign-extended or zero-extended 16-bit immediate, a value with only
   the upper half set, or a value representable as binary16; two
   otherwise.  */

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

/* Replay window pricing, shared by the replay pass's profitability tests
   and the delivery-shape estimates in rvtt-bnb.cc.  */

/* The kind of replay window being priced.  */

enum replay_shape
{
  /* Counted-loop capture: the window is recorded in the preheader and
     the body only launches it.  Also used for every hoist with
     body_rerecords false.  */
  SHAPE_COUNTED,
  /* The body re-records the window on every trip.  Priced as either
     execution-bound or delivery-bound, whichever resource limits; with
     a completion guard the execution-bound case also pays the full
     record delivery.  */
  SHAPE_RERECORD,
  /* Re-recording body whose record is hoisted, with a known trip count.
     Only delivery changes; execution is the same on both sides because
     only fixed-encoding words are accepted.  */
  SHAPE_RECORD_HOIST,
  /* As SHAPE_RECORD_HOIST with a runtime trip count known to be at
     least 1: profitable if it breaks even at 2 trips.  */
  SHAPE_RECORD_HOIST_RUNTIME,
  /* Counted capture whose first trip executes while recording: the
     capture word and record overhead are paid once and every remaining
     trip is one launch.  */
  SHAPE_COUNTED_PEEL
};

/* Return the shape to use for a re-recording window, so that the RTL
   pass and the estimates in rvtt-bnb.cc agree.  */

inline replay_shape
rerecord_shape (bool record_hoist_enabled, bool completion_guard,
		bool trips_proven)
{
  if (record_hoist_enabled && !completion_guard)
    return trips_proven ? SHAPE_RECORD_HOIST : SHAPE_RECORD_HOIST_RUNTIME;
  return SHAPE_RERECORD;
}

/* The priced terms of a replay window and the resulting decision.  */

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
  /* Result.  */
  int64_t benefit;		/* modeled centislot benefit */
  bool profitable;		/* benefit clears MIN_BENEFIT (and the
				   runtime shape's per_trip > 0) */
};

/* Price a replay window of shape SHAPE with WORDS payload words
   executing in EXEC_SLOTS slots over TRIPS trips (ignored by the runtime
   shape), against MIN_BENEFIT centislots.  LAUNCH_RUN is
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
      /* Only delivery changes: hoisting turns the first copy into one
	 more playback launch per trip, charged at the turnaround
	 rate.  */
      r.record_once = r.deliver_record + t.record_overhead_x100;
      r.per_trip = r.deliver_body - t.turnaround_x100;
      if (shape == SHAPE_RECORD_HOIST_RUNTIME)
	{
	  /* The benefit grows with the trip count: accept when 2 trips
	     clear the margin.  The worst case is a single trip.  */
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

/* Formation-vs-replay comparison for the macro planner's modulo-scheduled
   calendars.  */

/* Return the cost in centislots of a formed calendar: CONFIG_WORDS
   descriptor words at the push rate, RUN_SLOTS calendar slots at the push
   rate (or the slot rate if REPLAY_WRAPPED) and DRAIN_SLOTS at the slot
   rate.  */

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

/* Return the lower bound on the cost of delivering the explicit rows by
   replay instead (see the comment at the top of this file): ROW_INSTANCES
   instances of ROW_WORDS words at the slot rate, with record and launch
   words free.  */

inline uint64_t
ims_replay_alt_cost_x100 (const cost_table &t, uint64_t row_instances,
			  uint64_t row_words)
{
  return row_instances
    * (uint64_t) words_to_centislots (t, row_words, PLANE_REPLAY_SLOT);
}

/* Return the trip count at which a residency peel breaks even.  Each
   iteration after the first saves SUM_W load words at the slot rate.
   Programming costs one push per staged word plus one per SFPCONFIG
   (NPROG), and the peeled BODY_W-word body is pushed once instead of
   replayed.  Computed in 32-bit unsigned arithmetic.  */

inline unsigned
residency_peel_break_even_trips (const cost_table &t, unsigned sum_w,
				 unsigned nprog, unsigned body_w)
{
  unsigned push = (unsigned) t.push_x100;
  unsigned slot = (unsigned) t.slot_x100;
  unsigned cost = push * (sum_w + nprog) + (push - slot) * body_w;
  return 1 + (cost + slot * sum_w - 1) / (slot * sum_w);
}

/* Return the words issued per trip by a hoisted replay window: LAUNCHES
   full playback launches, one partial launch if HAS_TRIM, and
   INLINE_WORDS words left inline.  */

inline unsigned
window_trip_issue_words (unsigned launches, bool has_trim,
			 unsigned inline_words)
{
  return launches + (has_trim ? 1 : 0) + inline_words;
}

} /* namespace rvtt_delivery_cost */

#endif /* GCC_RVTT_DELIVERY_COST_CORE_H */
