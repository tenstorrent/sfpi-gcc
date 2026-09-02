/* Standalone unit tests for the delivery-cost arithmetic core.
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

/* Synthetic-fixture tests for the IR-free delivery-cost core,
   following the rvtt-macro-sched-test.cc pattern:

     g++ -std=c++11 -Wall -Wextra -Werror -I. \
         rvtt-delivery-cost-test.cc -o <out> && <out>

   The fixture table repeats the rvtt-cost.md define_constants values;
   a constant recalibration must update both (the compiled module reads
   insn-constants.h; this test pins the FORMULAS, and the recorded
   anchors below pin the composition).  */

#include <cstdio>
#include <cstdint>
#include "rvtt-delivery-cost-core.h"

using namespace rvtt_delivery_cost;

static int checks, failures;

static void
check (bool ok, const char *what)
{
  ++checks;
  if (!ok)
    {
      ++failures;
      std::fprintf (stderr, "FAIL: %s\n", what);
    }
}

/* The audited table (rvtt-cost.md define_constants, recorded values).  */
static const cost_table T = { 123, 100, 70, 300 };
static const int64_t MIN_BENEFIT = 60;	/* XTT_REPLAY_HOIST_MIN_BENEFIT */

int
main ()
{
  /* ---- Plane rates.  */
  check (words_to_centislots (T, 9, PLANE_RISC_PUSH) == 9 * 123,
	 "push plane rates at PUSH");
  check (words_to_centislots (T, 9, PLANE_REPLAY_SLOT) == 9 * 100,
	 "slot plane rates at SLOT");
  check (planner_word_plane (false) == PLANE_RISC_PUSH
	 && planner_word_plane (true) == PLANE_REPLAY_SLOT,
	 "planner-replay wrap selects the slot plane");

  /* ---- Recorded hardshrink-kernel refusal (the downstream-mirror
     validation anchor: trips 31, words 9, interlock exec 10 slots
     prices -383 exactly; rvtt-bnb.cc mirror_counted_hoist_fires).  */
  {
    replay_price p = replay_pricing (T, SHAPE_COUNTED, 31, 9, 10, 1,
				     false, MIN_BENEFIT);
    check (p.benefit == -383, "recorded hardshrink counted refusal -383");
    check (!p.profitable, "recorded hardshrink refuses");
    check (p.before == 1107 && p.after == 1070 && p.record == 1530,
	   "recorded hardshrink terms (before 1107 after 1070 record 1530)");
  }

  /* ---- Counted-peel spelling: same before/after, (trips-1) weighting,
     peel cost = one pushed capture word + record overhead.  */
  {
    replay_price h = replay_pricing (T, SHAPE_COUNTED, 31, 9, 10, 1,
				     false, MIN_BENEFIT);
    replay_price p = replay_pricing (T, SHAPE_COUNTED_PEEL, 31, 9, 10, 1,
				     false, MIN_BENEFIT);
    check (p.before == h.before && p.after == h.after,
	   "peel shares the counted before/after terms");
    check (p.record == 123 + 300, "peel cost = PUSH + RECORD_OVERHEAD");
    check (p.benefit == 30 * (1107 - 1070) - 423,
	   "peel weights (trips - 1) trips");
  }

  /* ---- Record-hoist measurement model: pure delivery delta.  */
  {
    replay_price p = replay_pricing (T, SHAPE_RECORD_HOIST, 7, 14, 0, 0,
				     false, MIN_BENEFIT);
    check (p.record_once == 15 * 123 + 300,
	   "record_once = deliver_record + overhead");
    check (p.per_trip == 14 * 123 - 70, "per_trip = deliver_body - turnaround");
    check (p.benefit == 7 * p.per_trip - p.record_once,
	   "measurement benefit = trips * per_trip - record_once");
    check (p.profitable, "14 words x 7 trips admits");
  }

  /* ---- Runtime-trip break-even: 2-trip benefit, single-trip
     exposure, per_trip must be positive.  */
  {
    replay_price p = replay_pricing (T, SHAPE_RECORD_HOIST_RUNTIME, 0, 14,
				     0, 0, false, MIN_BENEFIT);
    check (p.benefit == 2 * p.per_trip - p.record_once,
	   "runtime admits at the 2-trip break-even");
    check (p.exposure == p.record_once - p.per_trip,
	   "runtime single-trip exposure");
    check (p.profitable, "14-word runtime window admits");
    replay_price z = replay_pricing (T, SHAPE_RECORD_HOIST_RUNTIME, 0, 0,
				     0, 0, false, MIN_BENEFIT);
    check (!z.profitable && z.per_trip <= 0,
	   "non-positive per_trip refuses the runtime shape");
  }

  /* ---- Re-record binding-resource split + completion guard.  */
  {
    /* Execution-bound: exec (20 slots = 2000) >= deliver_record
       (10 * 123 = 1230).  */
    replay_price e = replay_pricing (T, SHAPE_RERECORD, 4, 9, 20, 1,
				     false, MIN_BENEFIT);
    check (e.exec_bound, "exec 2000 vs record delivery 1230 is exec-bound");
    check (e.before == 2000 + 300 && e.record == 300,
	   "exec-bound exposes the engine overhead only");
    replay_price g = replay_pricing (T, SHAPE_RERECORD, 4, 9, 20, 1,
				     true, MIN_BENEFIT);
    check (g.record == 300 + 1230,
	   "completion guard charges the full record delivery");
    check (g.benefit == e.benefit - 1230,
	   "guard is a monotone restriction of the exec-bound shape");

    /* Delivery-bound: exec (4 slots = 400) < deliver_record (1230);
       run 1 surplus 277 < 1230 keeps the record charged.  */
    replay_price d = replay_pricing (T, SHAPE_RERECORD, 4, 9, 4, 1,
				     false, MIN_BENEFIT);
    check (!d.exec_bound && !d.hidden && d.before == 1230,
	   "delivery-bound charges the record delivery per trip");
    check (d.surplus == 1 * (400 - 123), "sibling-run surplus term");
    /* Saturation: run 5 surplus 1385 >= 1230 hides the record.  */
    replay_price s = replay_pricing (T, SHAPE_RERECORD, 4, 9, 4, 5,
				     false, MIN_BENEFIT);
    check (s.hidden && s.before == s.after,
	   "launch-run surplus hides the record delivery");
    check (s.benefit == -s.record, "hidden record relieves nothing per trip");
  }

  /* ---- Re-record shape selection (the one flag-pair spelling shared
     by the RTL gate and the delivery-shape mirror).  */
  check (rerecord_shape (false, false, true) == SHAPE_RERECORD
	 && rerecord_shape (false, true, true) == SHAPE_RERECORD
	 && rerecord_shape (true, false, true) == SHAPE_RECORD_HOIST
	 && rerecord_shape (true, false, false) == SHAPE_RECORD_HOIST_RUNTIME
	 && rerecord_shape (true, true, true) == SHAPE_RERECORD,
	 "rerecord shape selection mirrors the flag pair");

  /* ---- Refusal-biased one-sidedness (module invariant): the
     replay alternative is priced at its steady-state lower bound --
     for every shape and every input in the sweep, the alternative's
     per-instance price never exceeds what the same rows cost through
     the priced replay model (record, launches, turnaround all charged
     on top of the same slot-rate execution).  */
  {
    bool one_sided = true;
    for (unsigned words = 1; words <= 32 && one_sided; ++words)
      for (unsigned trips = 2; trips <= 64; trips += 7)
	{
	  uint64_t alt = ims_replay_alt_cost_x100 (T, trips, words);
	  /* True replay delivery of the same rows: execution at the
	     slot rate (the bound's own term) plus the record pass and
	     per-trip turnaround the bound deliberately zeroes.  */
	  uint64_t priced
	    = (uint64_t) trips
	        * (words_to_centislots (T, words, PLANE_REPLAY_SLOT)
		   + T.turnaround_x100)
	      + (uint64_t) words_to_centislots (T, 1 + (int64_t) words,
						PLANE_RISC_PUSH)
	      + (uint64_t) T.record_overhead_x100;
	  if (alt > priced)
	    one_sided = false;
	}
    check (one_sided, "replay alternative is a lower bound (refusal-biased)");
  }

  /* ---- Formed-side arbitration composition.  */
  check (ims_formed_cost_x100 (T, 7, 12, 3, false)
	 == 7u * 123u + 12u * 123u + 3u * 100u,
	 "formed cost: prefix and calendar at PUSH, drain at SLOT");
  check (ims_formed_cost_x100 (T, 7, 12, 3, true)
	 == 7u * 123u + 12u * 100u + 3u * 100u,
	 "planner-replay wrap moves calendar words to SLOT");

  /* ---- Run amortization (cross-multiplied, refusal-biased strict <;
     weights 1/1 are the frozen per-run discipline).  */
  check (run_amortized_p (10, 20, 31, 1, 1), "10+20 < 31 amortizes");
  check (!run_amortized_p (10, 20, 30, 1, 1), "10+20 < 30 refuses (strict)");
  check (run_amortized_p (100, 10, 12, 1, 63),
	 "entry cost weighs by entry count only");
  check (!run_amortized_p (100, 10, 12, 1, 50),
	 "under-amortized entry cost refuses");

  /* ---- 48-bit trip-weight scaling.  */
  {
    int64_t b = (int64_t) 1 << 60, e = 5;
    scale_trip_weight (&b, &e);
    check (b <= (int64_t) 1 << 48 && b == (int64_t) 1 << 44 && e == 1,
	   "48-bit scaling walks octets, entry floored at 1");
    int64_t b2 = 1000, e2 = 3;
    scale_trip_weight (&b2, &e2);
    check (b2 == 1000 && e2 == 3, "small weights pass unscaled");
  }

  /* ---- SFPLOADI issue-word classification (both prior spellings'
     golden values).  */
  check (loadi_issue_words (0x00007fff) == 1, "sign-positive imm is 1");
  check (loadi_issue_words (0x0000ffff) == 1, "low-halfword image is 1");
  check (loadi_issue_words (0xffff8000u) == 1, "sign-negative imm is 1");
  check (loadi_issue_words (0x12340000u) == 1, "upper-only image is 1");
  check (loadi_issue_words (0x3f800000u) == 1, "1.0f FLOATA is 1");
  check (loadi_issue_words (0x3f80e000u) == 1,
	 "dirty-low-halfword FLOATA is 1");
  check (loadi_issue_words (0x3880e000u) == 1, "FLOATA exp floor 113 is 1");
  check (loadi_issue_words (0x3800e000u) == 2, "exp 112 is below FLOATA");
  check (loadi_issue_words (0x4700e000u) == 1, "FLOATA exp roof 142 is 1");
  check (loadi_issue_words (0x4780e000u) == 2, "exp 143 is above FLOATA");
  check (loadi_issue_words (0x3f801000u) == 2, "low-13 dirty is 2");
  check (loadi_issue_words (0x12345678u) == 2, "full word is 2");

  /* ---- Residency peel break-even (32-bit unsigned domain).  */
  check (residency_peel_break_even_trips (T, 2, 1, 10)
	 == 1 + (123u * 3 + 23u * 10 + 100u * 2 - 1) / (100u * 2),
	 "peel break-even reproduces the ceiling division");

  /* ---- Hoisted-window per-trip delivered issue words.  */
  check (window_trip_issue_words (3, true, 2) == 6,
	 "launches + partial + inline words");
  check (window_trip_issue_words (7, false, 0) == 7,
	 "no trim adds no partial launch");

  std::printf ("rvtt-delivery-cost-test: %d checks, %d failures\n",
	       checks, failures);
  return failures != 0;
}
