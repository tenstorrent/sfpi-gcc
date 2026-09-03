/* Private interface between the Tensix replay-formation units.
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* Shared data structures and cross-unit entry points of the
   replay-formation passes (rtl-rvtt-replay.cc and the
   rtl-rvtt-replay-*.cc units split from it).  Include it after
   the rtl/df headers.  Everything here is private to those units  */

#ifndef GCC_RTL_RVTT_REPLAY_INT_H
#define GCC_RTL_RVTT_REPLAY_INT_H

/* Minimum acceptable sequence length.  4 mirrors
   XTT_REPLAY_LOOP_UNROLL_MIN_WORDS (rvtt-cost.md): smaller rows cannot
   amortize a record/playback window.  Self-declared uncalibrated there --
   no hardware measurement separates 3 from 4 (a calibration
   experiment remains a follow-up).  */
constexpr unsigned MIN_SEQUENCE = 4;

/* Information about a tensix insn wrt replayability.  For an insn to be
   replayable it must be the same as the original and same generation.
   Sequences must not stradle a must_end insn.  Empty insns are ignored.  */
struct replay_info
{
  rtx_insn *insn;
  unsigned hash;       /* hash for insn, used in extending sequences */
  unsigned generation; /* Oldest SI value used (in synth insns) */
  bool must_end = true; /* Cannot be extended (followed by asm, non-Tensix) */
  bool empty = false; /* Is an empty tensix insn -- doesn't increase length */

  replay_info (rtx_insn *insn, unsigned gen, unsigned hash, bool empty)
    : insn (insn),  hash (hash), generation (gen), empty (empty) {}
};

/* The replay info about all instructions in a BB */
using replay_block = std::vector<replay_info>;

/* A half-open interval */
struct replay_span
{
  unsigned begin;
  unsigned end;

  replay_span () {}
  replay_span (unsigned b, unsigned e)
    : begin (b), end (e)
  {}
};

/* A sequence of insns, and all the clones of that instance.
   Each instance is its own clone.  */
struct replay_sequence
{
  unsigned parent; /* The 1-shorter sequence from whence this grew */
  unsigned hash;
  unsigned length; /* number of insns (does not include empty insns) */
  int companion_ok = -1; /* cached span_companion_sound_p verdict (-1 unset) */

  /* Instances of this sequence. By construction these are in increasing
     starting insn. During construction these might overlap.  We deal with that
     before use.  */
  std::vector<replay_span> clones;

  replay_sequence ()
    : parent (0), hash (0), length (0)
  {}
  replay_sequence (int parent, unsigned hash, unsigned length)
    : parent (parent), hash (hash), length (length)
  {}
};

/* Set of sequences, by contstruction these are in incressing length first and
   within each length by starting insn position.  */
using replay_list = std::vector<replay_sequence>;

/* Map from hash to set of sequences, used to find matches during
   construction */
using replay_map = std::map<unsigned, std::vector<unsigned>>;

/* It is cheaper to remove/copy pointers than sequence info itself.  */
using replay_active = std::vector<replay_sequence *>;

enum REPLAY_TYPE {REPLAY_none, REPLAY_playback, REPLAY_fixed_capture,
		  REPLAY_variable_capture};

struct peel_plan
{
  bool valid = false;
  rtx counter = nullptr;
  uint64_t new_init = 0;
  machine_mode mode = VOIDmode;
  uint64_t trips = 0;
};

struct hoist_lift_plan
{
  bool valid = false;
  basic_block placement = nullptr;
  unsigned levels = 0;
};

/* Defined in rtl-rvtt-replay.cc; see the block comment there.  */
extern bool reform_mode;

/* Formed-window bookkeeping shared by the units; cleared per
   function by the pass driver.  Defined in
   rtl-rvtt-replay-discover.cc and rtl-rvtt-replay-hoist.cc.  */
extern std::vector<rtx_insn *> formed_playback_launches;
extern std::vector<rtx_insn *> formed_noexec_captures;

/* rtl-rvtt-replay-discover.cc */
extern REPLAY_TYPE is_replay_insn (replay_span &span, rtx_insn *insn);
extern bool scan_insns (std::vector<replay_info> &info, basic_block bb);
extern unsigned build_sequences (replay_map &map, replay_list &list,
				 replay_block &block, unsigned max_length);
extern void active_triage (replay_block const &block, replay_active &active,
			   replay_list &list, unsigned from);
extern bool span_companion_sound_p (replay_block const &block,
				    replay_span span, bool sticky);
extern bool payload_contains_carried_p (replay_block const &block,
					replay_span span);
extern bool reform_carried_launch_arithmetic_ok (replay_block const &block,
						 replay_sequence const &seq);
extern replay_sequence *window_sizing_widen (replay_active &active,
					     replay_sequence *seq,
					     replay_block const &block,
					     unsigned free_span, bool sticky,
					     unsigned *trim_len_out,
					     unsigned *trim_end_out);
extern void shadow_discovery_census (replay_block const &block,
				     replay_active const &active,
				     unsigned legacy_seqs,
				     unsigned max_length,
				     unsigned pick_limit, bool sticky,
				     int bb_index);
extern replay_sequence *pick_replay (replay_active &active, unsigned limit,
				     replay_block const &block, bool sticky);
extern unsigned replace_sequence (replay_sequence &seq, replay_block &block,
				  unsigned replay_start);
extern bool fixed_replay_rtx_p (const_rtx x);
extern HOST_WIDE_INT exec_interlocked_slots (replay_block const &block,
					     replay_span span);
extern HOST_WIDE_INT delivered_words (replay_block const &block,
				      replay_span span);
extern unsigned max_contiguous_launch_run (replay_sequence const &seq,
					   replay_block const &block);
extern void window_sizing_commit_trim (replay_sequence &seq,
				       replay_block &block,
				       unsigned replay_start,
				       unsigned trim_len, unsigned trim_end);
extern bool active_invalidate (replay_active &active, replay_sequence *seq,
			       unsigned max_length);
extern std::vector<replay_span>
available_replay_spans (std::vector<replay_span> const &base,
			std::vector<bool> const &persistent);

/* rtl-rvtt-replay-hoist.cc */
extern basic_block hoist_preheader (replay_sequence const &seq,
				    replay_block const &block,
				    bitmap dirty_bbs, peel_plan *peel,
				    hoist_lift_plan *lift);
extern unsigned replace_hoisted_sequence (replay_sequence &seq,
					  replay_block &block,
					  unsigned replay_start,
					  basic_block preheader);
extern unsigned replace_hoisted_sequence_peel (replay_sequence &seq,
					       replay_block &block,
					       unsigned replay_start,
					       basic_block preheader,
					       peel_plan const &plan);
extern void hoist_counted_loops (function *cfn,
				 std::vector<replay_span> const &replay_spans,
				 std::vector<bool> &persistent_slots,
				 bitmap dirty_bbs, bool sticky);
extern void unroll_launch_loops (function *cfn, bitmap dirty_bbs);
extern bool conv_run_insn_p (rtx_insn *insn);
extern bool conv_reg_consumed_after_p (unsigned regno, rtx_insn *from,
				       basic_block bb);
extern void convert_isomorphic_runs (function *cfn, bitmap dirty_bbs);

/* rtl-rvtt-replay-crf.cc */
extern void canonicalize_counted_rows
  (function *cfn, std::vector<replay_span> const &replay_spans,
   std::vector<bool> &persistent_slots, bitmap dirty_bbs, bool sticky);

#endif /* GCC_RTL_RVTT_REPLAY_INT_H */
