/* Standalone reproduction tests for the timing-calendar derivation core.
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

/* THE EXECUTABLE FORM OF THE PAPER VALIDATION
   (docs/TIMING_CALENDAR_DERIVATION.md section 3): the derivation core, fed
   only each frozen calendar's SCHEDULE, must reproduce the
   independently recorded frozen sequence words bit-exactly, reproduce
   the handwritten MulInt32 descriptor's words, and refuse the shapes
   whose timing proofs fail.  This is the independence cross-check the
   in-tree verifier cannot provide (it shares the tables with
   synthesis).

     g++ -std=c++17 -Wall -Wextra -Werror -I. \
         rvtt-macro-derive-test.cc rvtt-macro-tables.cc -o <out> && <out>

   Raw words below are TEST EXPECTATIONS (their sanctioned home),
   transcribed from rvtt-macro-tables-{bh,wh}.def (frozen calendars)
   and tt_llk_blackhole ckernel_sfpu_mul_int.h (handwritten MulInt32).  */

#include <cstdio>
#include <cstring>
#include <initializer_list>

/* The WP12 event_spec extensions (template sharing, explicit-issue
   hazard bounds) are zero-meaning-unconstrained by design, so the
   frozen-row cases below intentionally leave them brace-omitted.  */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "rvtt-macro-derive-core.h"

using namespace rvtt_macro;
using namespace rvtt_macro_derive;

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

static void
check_word (uint32_t got, uint32_t want, const char *what)
{
  ++checks;
  if (got != want)
    {
      ++failures;
      std::fprintf (stderr, "FAIL: %s: got 0x%08x want 0x%08x\n", what,
		    got, want);
    }
}

static row_spec
fresh_row ()
{
  row_spec row;
  std::memset (&row, 0, sizeof (row));
  row.max_templates = 2;	/* owned template dests 0-1	       */
  row.max_macros = 3;		/* owned sequence dests 4-6	       */
  row.store_producer = -1;
  row.store_input_last_slot = -1;
  row.store_vd_next_write = -1;
  return row;
}

/* Frozen minmax (rvtt-macro-tables .def "minmax-binary-m0/m1",
   0x00dd008c / 0x53000000).  Schedule: launch m0 (load a) slot 0,
   explicit load b slot 1, launch m1 (store-only, absorbs stride)
   slot 2; SFPSWAP hosted on m0 consuming the explicit load; the
   staging copy and every delay must DERIVE.  */
static void
test_minmax (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 2;
  /* SFPSWAP: reads its VD (the launch value) and the planned L2.  */
  row.events[0] = { 0x92, false, 0, 0, 0, /*latest issued=load b*/ 1,
		    true, /*planned VC=L2*/ 2 };
  /* Delayed store on the store-only carrier.  */
  row.events[1] = { 0, true, 1, 2, /*dep=swap*/ 0x1, -1, false, 0 };
  row.n_macros = 2;
  row.macro_slot[0] = 0;
  row.macro_slot[1] = 2;
  row.ii = 3;
  row.last_issue_slot = 2;
  row.explicits[0] = { 1, 0 };	/* the explicit load	       */
  row.n_explicits = 1;
  row.vd_alternates = true;
  row.window_all_sfpu = true;
  row.store_event = 1;
  row.store_producer = 0;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "minmax derives");
  if (!ok || cal.refusal)
    return;
  check_word (cal.seq_words[0], 0x00dd008c, "minmax m0 sequence word");
  check_word (cal.seq_words[1], 0x53000000, "minmax m1 sequence word");
  check (cal.has_staging_copy && cal.staging_macro == 0
	 && cal.staging_delay == 3,
	 "minmax staging copy derives on macro 0 at delay 3 ((ddag) rule)");
  check (cal.delay_of[0] == 1,
	 "minmax swap delay 1 (waits for the explicit load)");
  check (cal.delay_of[1] == 2 && cal.store_reads_l16,
	 "minmax store delay 2 from LReg16");
  check (cal.drain == 3, "minmax drain 3");
  check (cal.delay_kind_mask == 0x1,
	 "minmax Simple counts instructions (forward-issued load b);"
	 " frozen 0x330 bit 9 (MAD) is a dead-bit convention");
}

/* Frozen signbit ("signbit-m0", 0x5384004d): one launch per row,
   kept separator, fixed VD; shift -> cast -> store.  */
static void
test_signbit (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 3;
  /* The SFPSHFT2 immediate realization of the explicit shift.  */
  row.events[0] = { 0x94, false, 0, 0, 0, 0, true, 0 };
  /* SFPCAST consuming the shift result in the carrier VD register.  */
  row.events[1] = { 0x90, false, 0, 0, 0x1, -1, true, 0 };
  row.events[2] = { 0, true, 0, 0, 0x2, -1, false, 0 };
  row.n_macros = 1;
  row.macro_slot[0] = 0;
  row.ii = 2;
  row.last_issue_slot = 1;
  row.explicits[0] = { 1, 0 };	/* the kept separator (non-SFPU)  */
  row.n_explicits = 1;
  row.vd_alternates = false;
  row.window_all_sfpu = false;
  row.store_event = 2;
  row.store_producer = 1;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "signbit derives");
  if (!ok || cal.refusal)
    return;
  check_word (cal.seq_words[0], 0x5384004d, "signbit m0 sequence word");
  check (cal.unit_of[0] == SEQ_UNIT_ROUND,
	 "signbit shift is Round-only (the ISA legality table)");
  check (cal.unit_of[1] == SEQ_UNIT_SIMPLE && cal.writes_l16[1],
	 "signbit cast on Simple staging through LReg16");
  check (cal.delay_of[2] == 2,
	 "signbit store field delay 2 (the .def's '3' was slots-after-"
	 "launch, not the field)");
  check (cal.delay_kind_mask == 0,
	 "signbit derives cycle counting (no forward-issued input);"
	 " frozen 0x110 bit 8 is a dead-bit convention");
}

/* Frozen cast-round ("cast-round-m0", 0x534d0004): the one shape with
   fully documented per-event delays -- Simple d0, Round d1, Store d2.  */
static void
test_cast_round (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 3;
  row.events[0] = { 0x90, false, 0, 0, 0, 0, true, 0 };
  row.events[1] = { 0x8e, false, 0, 0, 0x1, -1, true, 0 };
  row.events[2] = { 0, true, 0, 0, 0x2, -1, false, 0 };
  row.n_macros = 1;
  row.macro_slot[0] = 0;
  row.ii = 2;
  row.last_issue_slot = 1;
  row.explicits[0] = { 1, 0 };
  row.n_explicits = 1;
  row.vd_alternates = false;
  row.window_all_sfpu = false;
  row.store_event = 2;
  row.store_producer = 1;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "cast-round derives");
  if (!ok || cal.refusal)
    return;
  check_word (cal.seq_words[0], 0x534d0004, "cast-round m0 sequence word");
  check (cal.delay_of[0] == 0 && cal.delay_of[1] == 1
	 && cal.delay_of[2] == 2,
	 "cast-round reproduces the documented delay triple");
}

/* Frozen select ("select-m0" 0x13000004, "select-m1-encc" 0x00000005):
   condition launch slot 0, explicit payload slot 1, payload launch
   slot 2, kept separator slot 3; the merge is coalesced (no event).  */
static void
test_select (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 3;
  row.events[0] = { 0x7b, false, 0, 0, 0, 0, true, 0 };	  /* SETCC   */
  row.events[1] = { 0, true, 0, 0, 0, -1, false, 0 };	  /* store   */
  row.events[2] = { 0x8a, false, 1, 2, 0, -1, false, 0 }; /* ENCC    */
  row.n_macros = 2;
  row.macro_slot[0] = 0;
  row.macro_slot[1] = 2;
  row.ii = 4;
  row.last_issue_slot = 3;
  row.explicits[0] = { 1, 0 };	/* explicit payload load	       */
  row.explicits[1] = { 3, 0 };	/* kept separator		       */
  row.n_explicits = 2;
  row.vd_alternates = false;
  row.window_all_sfpu = false;
  row.store_event = 1;
  row.store_producer = -1;	/* data = the shared launch VD	       */
  row.store_input_last_slot = 2;
  row.store_vd_next_write = 4;	/* next row's first load	       */

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "select derives");
  if (!ok || cal.refusal)
    return;
  check_word (cal.seq_words[0], 0x13000004, "select m0 sequence word");
  check_word (cal.seq_words[1], 0x00000005, "select m1 sequence word");
  check (!cal.store_reads_l16, "select store reads the shared VD");
}

/* Handwritten MulInt32 (ckernel_sfpu_mul_int.h _init_mul_int_): the
   three-address variant's macro-1 program -- MAD byte 0xCC (template 0,
   delay 1, VD16, VB<-VD), store byte 0x5B (delay 3 = MAD result
   latency 2 past exec) -- from its schedule: explicit load a slot 0,
   launch (macro 1) slot 1, explicit load b slot 2.  */
static void
test_mul_int32_three_address (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 2;
  row.events[0] = { 0x98, false, 1, 1, 0, /*load b*/ 2, true,
		    /*planned VC=LCONST*/ 10 };
  row.events[1] = { 0, true, 1, 1, 0x1, -1, false, 0 };
  row.n_macros = 2;		/* macro 0 unused, macro 1 active      */
  row.macro_slot[0] = -1;
  row.macro_slot[1] = 1;
  row.ii = 3;
  row.last_issue_slot = 2;
  row.explicits[0] = { 0, 0 };
  row.explicits[1] = { 2, 0 };
  row.n_explicits = 2;
  row.vd_alternates = false;
  row.window_all_sfpu = true;
  row.store_event = 1;
  row.store_producer = 0;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "mul_int32 three-address derives");
  if (!ok || cal.refusal)
    return;
  check_word (cal.seq_words[1], 0x5b00cc00,
	      "mul_int32 macro-1 sequence word (MAD byte 0xCC, store"
	      " byte 0x5B; _init_mul_int_ packs store<<24|round<<16"
	      "|mad<<8|simple)");
  check (cal.delay_of[1] == 3,
	 "mul_int32 store delay = MAD exec + result latency 2");
  check (cal.delay_kind_mask == (1u << SEQ_UNIT_MAD),
	 "mul_int32 MAD counts instructions (forward-issued load b)");
}

/* The handwritten MulInt32 ONE-SLOT in-place variant (macro 0, ii=1)
   fails the LReg16 lifetime proof: the next row's MUL24 rewrites
   LReg16 strictly before this row's store executes.  The derivation
   REFUSES it (docs section 3/section 7 -- a finding about the hand kernel, whose
   one-slot case appears unexercised).  */
static void
test_mul_int32_one_slot_refuses (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 2;
  row.events[0] = { 0x98, false, 0, 0, 0, 0, true, 10 };
  row.events[1] = { 0, true, 0, 0, 0x1, -1, false, 0 };
  row.n_macros = 1;
  row.macro_slot[0] = 0;
  row.ii = 1;
  row.last_issue_slot = 0;
  row.vd_alternates = false;
  row.window_all_sfpu = true;
  row.store_event = 1;
  row.store_producer = 0;

  derived_calendar cal;
  derive_calendar (c, row, &cal);
  check (cal.refusal && !std::strcmp (cal.refusal,
				      refusal_hazard ()),
	 "mul_int32 one-slot in-place refuses the LReg16 lifetime");
}

/* The derived unary max/min calendar (the formation target): SWAP
   against a constant register, store demoted to its own carrier;
   ii=1 merged candidate refuses (ddag); ii=2 derives the staging copy.  */
static void
test_unary_maxmin (const caps *c)
{
  /* Candidate 0: one merged carrier, ii=1 -- SFPSWAP's own next-row
     instance violates the Simple-idle-next-cycle rule.  */
  {
    row_spec row = fresh_row ();
    row.n_events = 2;
    row.events[0] = { 0x92, false, 0, 0, 0, 0, true, 9 };
    row.events[1] = { 0, true, 0, 0, 0x1, -1, false, 0 };
    row.n_macros = 1;
    row.macro_slot[0] = 0;
    row.ii = 1;
    row.last_issue_slot = 0;
    row.vd_alternates = true;
    row.window_all_sfpu = true;
    row.store_event = 1;
    row.store_producer = 0;
    derived_calendar cal;
    derive_calendar (c, row, &cal);
    check (cal.refusal && !std::strcmp (cal.refusal, refusal_hazard ()),
	   "unary maxmin merged ii=1 refuses (ddag)");
  }
  /* Candidate 1: store demoted, ii=2.  */
  {
    row_spec row = fresh_row ();
    row.n_events = 2;
    row.events[0] = { 0x92, false, 0, 0, 0, 0, true, 9 };
    row.events[1] = { 0, true, 1, 1, 0x1, -1, false, 0 };
    row.n_macros = 2;
    row.macro_slot[0] = 0;
    row.macro_slot[1] = 1;
    row.ii = 2;
    row.last_issue_slot = 1;
    row.vd_alternates = true;
    row.window_all_sfpu = true;
    row.store_event = 1;
    row.store_producer = 0;
    derived_calendar cal;
    bool ok = derive_calendar (c, row, &cal);
    check (ok && !cal.refusal, "unary maxmin demoted derives");
    if (!ok || cal.refusal)
      return;
    check_word (cal.seq_words[0], 0x00d50084,
		"unary maxmin m0 word (swap d0, staged copy d2)");
    check_word (cal.seq_words[1], 0x53000000,
		"unary maxmin m1 word (the minmax store program, derived)");
    check (cal.has_staging_copy && cal.staging_macro == 0,
	   "unary maxmin staging copy on the swap's macro");
    check (cal.drain == 3, "unary maxmin drain 3");
    check (cal.delay_kind_mask == 0,
	   "unary maxmin needs no instruction counting");
  }
}

/* Placement capacity: four Simple-class events on two carriers (the
   add/sub_int 7-slot row under either grouping candidate) refuse by
   name -- the honest replacement for sequence-encoding-unproven.  */
static void
test_addsub_placement_refusal (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 5;
  row.events[0] = { 0x90, false, 0, 0, 0, 0, true, 0 };	  /* cast a  */
  row.events[1] = { 0x90, false, 1, 1, 0, 1, true, 0 };	  /* cast b  */
  row.events[2] = { 0x79, false, 0, 0, 0x3, -1, false, 0 }; /* iadd   */
  row.events[3] = { 0x90, false, 0, 0, 0x4, -1, false, 0 }; /* cast r */
  row.events[4] = { 0, true, 0, 0, 0x8, -1, false, 0 };
  row.n_macros = 2;
  row.macro_slot[0] = 0;
  row.macro_slot[1] = 1;
  row.ii = 2;
  row.last_issue_slot = 1;
  row.vd_alternates = true;
  row.window_all_sfpu = true;
  row.store_event = 4;
  row.store_producer = 3;

  derived_calendar cal;
  derive_calendar (c, row, &cal);
  check (cal.refusal && !std::strcmp (cal.refusal, refusal_placement ()),
	 "add_int row refuses subunit-placement-unproven");
}

/* Field-packer sanity: byte and misc encoders against hand-computed
   values; the undefined case refuses.  */
static void
test_packers ()
{
  uint8_t b = 0;
  check (encode_sequence_bits (SEQ_CASE_TEMPLATE0, 1, false, true, &b)
	 && b == 0x8c, "byte packer: template 0, delay 1, route");
  check (encode_sequence_bits (SEQ_CASE_STORE, 2, true, false, &b)
	 && b == 0x53, "byte packer: store, delay 2, VD16");
  check (!encode_sequence_bits (1, 0, false, false, &b),
	 "byte packer refuses the undefined case");
  check (!encode_sequence_bits (SEQ_CASE_STORE, 8, false, false, &b),
	 "byte packer refuses delay overflow");
  uint8_t bytes[4] = { 0x8c, 0, 0xdd, 0 };
  check_word (compose_sequence_word (bytes), 0x00dd008c, "word composer");
  check_word (encode_misc_fields (6, 0, 0x7), 0x706,
	      "misc packer reproduces the select live fields");
  check_word (encode_misc_fields (0, 0x2, 0x1), 0x120,
	      "misc packer: minmax live bits (frozen 0x330 dead bits"
	      " documented)");
}

/* WP12 template sharing: events with equal template_key share one
   InstructionTemplate slot and capacity counts DISTINCT slots (two
   in-place casts on two macros, one shared template; the second cast
   is the store's sole producer and routes through LReg16).  */
static void
test_wp12_template_sharing (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 3;
  row.events[0] = { 0x90, false, 0, 0, 0, -1, true, 0 };
  row.events[0].template_key = 1;
  row.events[1] = { 0x90, false, 1, 1, 0, -1, true, 0 };
  row.events[1].template_key = 1;
  row.events[2] = { 0, true, 1, 1, 0x2, -1, false, 0 };
  row.n_macros = 2;
  row.macro_slot[0] = 0;
  row.macro_slot[1] = 1;
  row.ii = 2;
  row.last_issue_slot = 1;
  row.vd_alternates = true;
  row.window_all_sfpu = true;
  row.store_event = 2;
  row.store_producer = 1;
  row.max_templates = 4;
  row.max_macros = 4;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "wp12 shared-template row derives");
  if (!ok || cal.refusal)
    return;
  check (cal.n_templates == 1, "wp12 equal keys share one template slot");
  check (cal.template_index_of[0] == 0 && cal.template_index_of[1] == 0,
	 "wp12 both events reference the shared slot");
}

/* WP12 explicit-issue hazards: the WAR floor delays an event past an
   earlier explicit reader of its written register, and an
   impossible overwrite deadline refuses.  */
static void
test_wp12_explicit_hazards (const caps *c)
{
  row_spec row = fresh_row ();
  row.n_events = 2;
  row.events[0] = { 0x79, false, 0, 0, 0, -1, true, 2 };
  row.events[0].war_floor_slot_p1 = 3;	/* explicit reader at slot 2   */
  row.events[1] = { 0, true, 0, 0, 0x1, -1, false, 0 };
  row.n_macros = 1;
  row.macro_slot[0] = 0;
  row.ii = 6;
  row.last_issue_slot = 5;
  row.explicits[0] = { 2, 0 };
  row.n_explicits = 1;
  row.vd_alternates = false;
  row.window_all_sfpu = true;
  row.store_event = 1;
  row.store_producer = 0;
  row.max_templates = 4;
  row.max_macros = 4;

  derived_calendar cal;
  bool ok = derive_calendar (c, row, &cal);
  check (ok && !cal.refusal, "wp12 war-floored row derives");
  if (ok && !cal.refusal)
    check (cal.exec_of[0] == 3, "wp12 war floor delays exec past the reader");

  /* Same row with an overwriter strictly before the floored exec: the
     deadline is unsatisfiable and refuses by the hazard name.  */
  row.events[0].issue_overwrite_slot_p1 = 3;	/* overwriter at slot 2 */
  ok = derive_calendar (c, row, &cal);
  check (!ok && cal.refusal
	 && std::strcmp (cal.refusal, refusal_hazard ()) == 0,
	 "wp12 impossible overwrite deadline refuses (hazard)");
}

int
main ()
{
  const caps *bh = rvtt_macro_caps_for_cpu (CPU_BH);
  const caps *wh = rvtt_macro_caps_for_cpu (CPU_WH);
  check (bh && wh, "capability tables exist");
  test_packers ();
  for (const caps *c : { bh, wh })
    {
      test_minmax (c);
      test_signbit (c);
      test_cast_round (c);
      test_select (c);
      test_mul_int32_three_address (c);
      test_mul_int32_one_slot_refuses (c);
      test_unary_maxmin (c);
      test_addsub_placement_refusal (c);
      test_wp12_template_sharing (c);
      test_wp12_explicit_hazards (c);
    }
  check (!rvtt_macro_caps_for_cpu (CPU_QSR),
	 "QSR stays table-absent");
  std::printf ("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
