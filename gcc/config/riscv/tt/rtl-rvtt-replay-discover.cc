/* Tensix replay formation: sequence discovery and replacement
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

/* The discovery half of the replay former: per-block sequence
   scanning and hashing, sequence building and triage, the
   companion and carried-payload soundness audits, window sizing,
   the shadow suffix-automaton discovery census, the knapsack pick,
   and the word-exact in-place replacement.  Split from
   rtl-rvtt-replay.cc; the algorithm essay lives there.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_MAP
#define INCLUDE_SET
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
#include "rvtt-protos.h"
#include "rvtt-trips.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-mop-tables.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rtl-rvtt-replay-int.h"

/* Classify INSN as a typed TTREPLAY word.  Returns REPLAY_none unless
   INSN is the typed replay pattern; otherwise fills SPAN with the raw
   operands -- begin = the start slot, end = the LENGTH (not a half-open
   bound; 0 when the length is not a compile-time constant) -- and
   returns REPLAY_playback (load operand 0), or REPLAY_fixed_capture vs
   REPLAY_variable_capture for a recording with a constant vs unknown
   length.  */

REPLAY_TYPE
is_replay_insn (replay_span &span, rtx_insn *insn)
{
  if (recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
    return REPLAY_none;

  auto pattern = PATTERN (insn);
  span.begin = INTVAL (XVECEXP (pattern, 0, 5));
  span.end = 0;
  auto len = XVECEXP (pattern, 0, 3);
  auto type = REPLAY_fixed_capture;
  if (CONST_INT_P (len))
    span.end = INTVAL (len);
  else
    type = REPLAY_variable_capture;
  if (!INTVAL (XVECEXP (pattern, 0, 7)))
    type = REPLAY_playback;
  return type;
}

/* Scan insns o block computing hashes and must_end.  */

bool
scan_insns (std::vector<replay_info> &info, basic_block bb)
{
  constexpr unsigned GR_REG_HWM = FP_REG_FIRST;
  unsigned reg_ages[GR_REG_HWM];
  rtx_insn *insn;
  bool may_continue = false;
  unsigned shadow = 0;

  info.clear ();
  memset (reg_ages, 0, sizeof (reg_ages));
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      rtx set = nullptr;
      
      if (GET_CODE (insn) != INSN)
	{
	not_tensix:
	  set = single_set (insn);
	  if (set)
	    {
	    clobber:
	      rtx dst = SET_DEST (set);
	      if (REG_P (dst) && REGNO (dst) < GR_REG_HWM)
		reg_ages[REGNO (dst)]++;
	    }

	  may_continue = false;
	  continue;
	}

	rtx pattern = PATTERN (insn);

	if (GET_CODE (pattern) == USE)
	  continue;
	if (GET_CODE (pattern) == CLOBBER)
	  {
	    set = pattern;
	    goto clobber;
	  }

	auto replay_class = get_attr_xtt_replay (insn);
	if (replay_class == XTT_REPLAY_OWNER)
	  {
	    replay_span span;
	    auto type = is_replay_insn (span, insn);
	    gcc_assert (type != REPLAY_none);
	    if (type == REPLAY_variable_capture)
	      /* We don't know where this ends, so have to stop scanning the
	         BB.  */
	      break;

	    if (type == REPLAY_fixed_capture)
	      shadow = span.end;
	    /* A replay owner is a slot-occupying word.  A sequence
	       spanning it would form a capture whose recording swallows
	       the owner word (a REPLAY issued while recording is
	       recorded, not executed) and the counted-row phase's inline
	       reference body along with it.  End sequence continuity
	       here: the owner and its recorded shadow separate runs.  */
	    may_continue = false;
	    continue;
	  }

	/* Only machine-described replay-safe instructions may enter a payload.
	   Explicit replay owners are handled above so their reserved slots
	   remain visible to the allocator.  In particular, an opaque asm
	   remains a boundary even if it happens to print a constant `.ttinsn'
	   word in the final assembly.  */
	if (get_attr_type (insn) != TYPE_TENSIX
	    || replay_class != XTT_REPLAY_SAFE)
	  goto not_tensix;

	bool is_empty = !get_attr_length (insn);
	if (shadow)
	  {
	    /* We're in the shadow of a replay capture */
	    if (!is_empty)
	      shadow--;
	    continue;
	  }

	if (may_continue)
	  info.back ().must_end = false;

	/* Just use crc32, it's right there */
	unsigned age = 0;
	auto hasher = [&reg_ages, &age] (auto &self, unsigned hash, rtx rtl)
	  -> unsigned
	{
	  hash = crc32_unsigned (hash, GET_CODE (rtl) + (GET_MODE (rtl) << 16));
	  switch (GET_CODE (rtl))
	    {
	    default:
	      gcc_unreachable ();

	    case UNSPEC:
	    case UNSPEC_VOLATILE:
	      hash = crc32_unsigned (hash, XINT (rtl, 1));
	      /* FALLTHROUGH */

	    case PARALLEL:
	      {
		/* All 3 have the vector at position 0 */
		auto &vec = XVEC (rtl, 0);
		for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
		  hash = self (self, hash, RTVEC_ELT (vec, ix));
	      }
	      break;

	    case SET:
	      hash = self (self, hash, SET_SRC (rtl));
	      hash = self (self, hash, SET_DEST (rtl));
	      break;

	    case REG:
	      hash = crc32_unsigned (hash, REGNO (rtl));
	      if (REGNO (rtl) < GR_REG_HWM)
		age += reg_ages[REGNO (rtl)];
	      break;

	    case CONST_INT:
	      hash = crc32_unsigned (hash, unsigned (INTVAL (rtl)));
	      break;

	    case MEM:
	      /* MEMs are to store a synthesized insn.  All are equivalent.
	         In broken code, we could meet simple sets moving to/from
	         MEM.  */
	      break;

	    case CLOBBER:
	      {
		rtx dst = SET_DEST (rtl);
		if (REG_P (dst) && REGNO (dst) < GR_REG_HWM)
		  reg_ages[REGNO (dst)]++;
	      }
	      break;

	    case USE:
	    case SCRATCH:
	      break;
	    }

	  return hash;
	};

	unsigned hash = hasher (hasher, recog_memoized (insn), pattern);
	info.emplace_back (insn, age, hash, is_empty);

	may_continue = true;
    }
  return !info.empty ();
}

/* The pattern-equality relaxation used by the discovery's rtx_equal_p
   re-check: MEM operands (synthesized-insn spill slots) and
   clobber/scratch outputs compare equal by class, everything else
   compares structurally.  Extracted verbatim from extend_sequence so the
   suffix-automaton shadow discovery can form its
   symbol classes under EXACTLY the legacy predicate -- symbol equality
   diverging from this callback is the failure mode the stage-A superset
   assertion exists to catch, so the two must not be spelled twice.  */

static void
extend_sequence (replay_map &map, replay_list &list, replay_block &block,
		 unsigned parent, unsigned length, unsigned begin, unsigned end)
{
  auto &insn = block[end - 1];

  unsigned hash
    = parent ? crc32_unsigned (list[parent].hash, insn.hash) : insn.hash;
  auto slot = map.emplace (hash, std::vector<unsigned> ());
  for (auto ix : slot.first->second)
    {
      auto &seq = list[ix];
      gcc_assert (length == seq.length);
      if (parent != seq.parent)
	continue;
      auto &seq_insn = block[seq.clones.front ().end - 1];
      if (seq_insn.generation != insn.generation)
	continue;
      /* The one word-exact comparator (rvtt-delivery-cost.cc):
	 pattern equality under the
	 scratch-operand tolerance.  */
      if (!rvtt_dcost_replay_word_equal_p (seq_insn.insn, insn.insn))
	continue;

      /* Clones must be in ascending order (the invalidation presumes that) */
      gcc_assert (begin > seq.clones.back ().begin);

      /* This might create overlapping clones, but we still need this as a
         later extension could only apply to one of these.  */
      seq.clones.emplace_back (begin, end);
      return;
    }

  slot.first->second.emplace_back (unsigned (list.size ()));

  /* New sequence */
  list.emplace_back (parent, hash, length);
  /* It is its own clone */
  list.back ().clones.emplace_back (begin, end);  
}

/* Build sequences of insns and their copies.  This is fundamentally O(N^2).
   Return number index of first sequence >= MIN_SEQUENCE.  */

unsigned
build_sequences (replay_map &map, replay_list &list, replay_block &block,
		 unsigned max_length)
{
  list.clear ();
  list.push_back (replay_sequence ()); /* null sequence */
  map.clear ();

  /* Initialize sequences of length 1.  These are the seeds from whence
     sequences grow. Historically we started sequences at load insns (those
     being the first of a loop), to further reduce N.  */
  for (unsigned ix = 0, end_ix = block.size (); ix != end_ix; ++ix)
    {
      if (block[ix].empty)
	continue;

      extend_sequence (map, list, block, 0, 1, ix, ix + 1);
    }
  unsigned lwm = unsigned (list.size ());

  /* Grow each sequence by 1, until we can grow no more, or we get too long */
  unsigned from = 1, length = 1;
  while (length++ < max_length)
    {
      map.clear ();

      unsigned seq_end = list.size ();
      for (unsigned seq_ix = from; seq_ix != seq_end; seq_ix++)
	{
	  if (list[seq_ix].clones.size () == 1)
	    /* There is only one instance, no point extending this.  */
	    continue;

	  /* Warning, list is extended inside this loop. Beware iterator
	     invalidation */
	  for (unsigned clone_ix = 0, clone_end = list[seq_ix].clones.size ();
	       clone_ix != clone_end; ++clone_ix)
	    {
	      auto &seq = list[seq_ix];
	      replay_span span = seq.clones[clone_ix];

	    skip_empty:
	      if (block[span.end - 1].must_end)
		continue;
	      if (span.end == block.size ())
		continue;
	      if (block[span.end].empty)
		{
		  span.end++;
		  goto skip_empty;
		}

	      extend_sequence (map, list, block, seq_ix, length, span.begin,
			       span.end + 1);
	    }
	}

      if (length < MIN_SEQUENCE)
	lwm = list.size ();
      from = seq_end;
    }

  return lwm;
}

/* Dump SPAN's insns to STREAM, one per line, numbering the
   slot-occupying entries upward from BASE (a replay-buffer slot or a
   block position, per caller).  With IGNORE_EMPTY, zero-length entries
   print an unnumbered "-:"; otherwise they consume a number but are
   marked with '-' instead of ':'.  */

static void
dump_sequence (FILE *stream, replay_block const &block, replay_span span,
	       unsigned base, bool ignore_empty = true)
{
  for (auto pos = block.data () + span.begin, end = block.data () + span.end;
       pos != end; pos++)
    {
      if (ignore_empty && pos->empty)
	fprintf (stream, "-: ");
      else
	fprintf (stream, "%u%c ", base++, pos->empty ? '-' : ':');
      dump_insn_slim (stream, pos->insn);
    }
}

/* LIST has been computed, but sequences might contain overlapping runs.
   Remove overlaps, and push a pointer to valid ones into the ACTIVE array.  */

void
active_triage (replay_block const &block, replay_active &active,
	       replay_list &list, unsigned from)
{
  active.clear ();
  for (; from != list.size (); from++)
    {
      auto &seq = list[from];
      auto end = seq.clones.end (), write = seq.clones.begin ();
      unsigned bound = 0;
      for (auto pos = write; pos != end; ++pos)
	{
	  if (bound > pos->begin)
	    continue;

	  bound = pos->end;
	  *write = *pos;
	  ++write;
	}
      seq.clones.erase (write, end);

      /* Remember this if it has more than one instance.  */
      if (seq.clones.size () > 1)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "Sequence [%u,%u) length %u, %u instances\n",
		       seq.clones.front ().begin,
		       seq.clones.front ().end,
		       seq.length, unsigned (seq.clones.size ()));
	      dump_sequence (dump_file, block, seq.clones.front (),
			     seq.clones.front ().begin, false);
	      fprintf (dump_file, "\n");
	    }
	  active.push_back (&seq);
	}
    }
  if (dump_file)
    fprintf (dump_file, "%u candidates\n\n", unsigned (active.size ()));
}

/* Companion-group soundness of a candidate capture span (TOP3-2 layer 2,
   the value+companion pairing contract).

   STICKY is the function's shadow-coupling possibility
   (rvtt_shadow_coupling_possible): index tracking couples every
   value-bank move to the companion bank, so under a possibly-enabled
   coupling a capture may only contain instructions whose companion
   behaviour is typed.  Refusals, each by name, leaving code
   byte-identical:

   - shadow-state-unproved: an effect-opaque member under possibly-enabled
     index tracking (its value-bank behaviour is unprovable);
   - multiresult-companion-split: a member writes the value bank without
     typed companion results, or the span boundary separates a
     multi-result member from its adjacent zero-length companion
     markers (deleting a clone must never orphan the markers that carry
     the group's fixed-LREG dataflow).  */

bool
span_companion_sound_p (replay_block const &block, replay_span span,
			bool sticky)
{
  int first_real = -1;
  for (unsigned ix = span.begin; ix != span.end; ++ix)
    {
      if (block[ix].empty)
	continue;
      if (first_real < 0)
	first_real = ix;

      rtx_insn *insn = block[ix].insn;
      xtt_effect_set e = rvtt_insn_effects (insn);
      xtt_multiresult_group group;
      bool multi = rvtt_multiresult_group (insn, e, &group);
      if (sticky)
	{
	  if (e.opaque)
	    {
	      rvtt_refuse (RVTT_REF_SHADOW_STATE_UNPROVED, dump_file,
			   "Refusing capture: shadow-state-unproved:"
			   " insn %d is effect-opaque under possibly-enabled"
			   " index tracking\n", INSN_UID (insn));
	      return false;
	    }
	  if (!multi && (e.lreg_write & 0xF))
	    {
	      rvtt_refuse (RVTT_REF_MULTIRESULT_COMPANION_SPLIT, dump_file,
			   "Refusing capture: multiresult-companion-split:"
			   " insn %d writes the value bank without typed"
			   " companion results under possibly-enabled"
			   " index tracking\n", INSN_UID (insn));
	      return false;
	    }
	}

      if (multi && (ix == unsigned (first_real) || ix + 1 >= span.end))
	{
	  uint32_t group_mask
	    = group.value_write_mask | group.companion_write_mask;
	  /* Boundary integrity: adjacent zero-length companion markers
	     outside the span must not be split from their instruction.  */
	  if (ix + 1 >= span.end)
	    for (unsigned probe = span.end; probe != block.size (); ++probe)
	      {
		if (!block[probe].empty)
		  break;
		uint32_t mask;
		if (rvtt_lreg_marker (block[probe].insn, &mask)
		    && (mask & group_mask))
		  {
		    rvtt_refuse (RVTT_REF_MULTIRESULT_COMPANION_SPLIT,
				 dump_file,
				 "Refusing capture:"
				 " multiresult-companion-split: boundary"
				 " separates insn %d from companion marker"
				 " insn %d\n", INSN_UID (insn),
				 INSN_UID (block[probe].insn));
		    return false;
		  }
	      }
	  if (ix == unsigned (first_real))
	    for (unsigned probe = span.begin; probe-- != 0;)
	      {
		if (!block[probe].empty)
		  break;
		uint32_t mask;
		if (rvtt_lreg_marker (block[probe].insn, &mask)
		    && (mask & group_mask))
		  {
		    rvtt_refuse (RVTT_REF_MULTIRESULT_COMPANION_SPLIT,
				 dump_file,
				 "Refusing capture:"
				 " multiresult-companion-split: boundary"
				 " separates insn %d from companion marker"
				 " insn %d\n", INSN_UID (insn),
				 INSN_UID (block[probe].insn));
		    return false;
		  }
	      }
	}
    }
  return true;
}

/* Does the candidate payload SPAN contain a CARRIED access -- a typed
   Dst access the auto-increment pass retargeted to the compiler-owned
   scratch modifier?  Meaningful only in reform_mode (the retarget
   happens after the first formation).  */

bool
payload_contains_carried_p (replay_block const &block, replay_span span)
{
  for (unsigned ix = span.begin; ix != span.end; ++ix)
    if (!block[ix].empty
	&& rvtt_dst_autoincr_carried_access_p (block[ix].insn))
      return true;
  return false;
}

/* Launch-arithmetic audit for a carried-payload candidate (reform_mode;
   see the block comment at reform_mode above).  The replacement paths
   deliver exactly one payload execution per replaced clone -- in-block:
   one exec-while-record pass plus one launch per remaining clone (on
   QSR the capture swallows the first clone and every clone launches);
   hoisted: a no-exec record whose preheader payload is ingested, never
   executed, plus one launch per clone.  Either way,

       delivered executions == clones.size () == replaced row sites,

   PROVIDED the clones are non-overlapping and word-exact: only then is
   the delivered word at each site the site's own word, so the carried
   access executes exactly as often -- and in the same stream positions
   -- as the fold's payload-coverage proof accounted.  This function
   re-verifies both premises structurally over the final clone list and
   refuses by name on any violation.  The scratch-operand tolerance
   mirrors the discovery's own equality (compiler GPR scratch and
   synthesized-word MEMs do not reach the delivered Tensix word).  */

/* The ONE clone word-exact lockstep walk (rvtt-delivery-cost.cc),
   shared by the reform-mode launch-arithmetic audit and the
   window-sizing re-verification below (previously two per-site
   spellings; the per-word comparator itself is the module's
   rvtt_dcost_replay_word_equal_p).  Verifies that SEQ's clones are
   non-overlapping, ascending, word-exact copies of the recorded
   window, pairing non-empty members in lockstep.  REQUIRE_INSN_CODE
   preserves the window-sizing spelling's stricter plain-INSN check
   (the reform audit tolerates identical non-INSN patterns -- the one
   asymmetry between the prior spellings, preserved bug-compatibly).
   Callers map the verdict onto their own refusal texts.  */

enum clone_walk_verdict
{
  CLONE_WALK_OK,
  CLONE_WALK_EMPTY,	/* no clones */
  CLONE_WALK_OVERLAP,	/* overlapping or unordered clone spans */
  CLONE_WALK_WORD,	/* clone word differs from recorded word */
  CLONE_WALK_COUNT	/* clone word count differs from recorded length */
};

static clone_walk_verdict
clones_word_exact_walk (replay_block const &block,
			replay_sequence const &seq, bool require_insn_code)
{
  if (seq.clones.empty ())
    return CLONE_WALK_EMPTY;

  /* Non-overlap, ascending: each site is replaced exactly once.  */
  unsigned bound = 0;
  for (auto const &clone : seq.clones)
    {
      if (clone.begin < bound || clone.end <= clone.begin)
	return CLONE_WALK_OVERLAP;
      bound = clone.end;
    }

  /* Word-exactness: the k-th delivered word of every clone equals the
     k-th recorded word.  Pair non-empty members in lockstep.  */
  auto const &first = seq.clones.front ();
  for (unsigned cx = 1; cx != seq.clones.size (); ++cx)
    {
      auto const &clone = seq.clones[cx];
      unsigned fx = first.begin, ox = clone.begin;
      unsigned matched = 0;
      for (;;)
	{
	  while (fx != first.end && block[fx].empty)
	    ++fx;
	  while (ox != clone.end && block[ox].empty)
	    ++ox;
	  if (fx == first.end || ox == clone.end)
	    break;
	  if ((require_insn_code
	       && (GET_CODE (block[fx].insn) != INSN
		   || GET_CODE (block[ox].insn) != INSN))
	      || !rvtt_dcost_replay_word_equal_p (block[fx].insn,
						  block[ox].insn))
	    return CLONE_WALK_WORD;
	  ++fx, ++ox, ++matched;
	}
      if (matched != seq.length)
	return CLONE_WALK_COUNT;
    }
  return CLONE_WALK_OK;
}

/* The reform-mode launch-arithmetic audit for a carried-payload
   candidate SEQ (contract in the design comment before
   clones_word_exact_walk): run the shared clone walk and map each
   failing verdict onto the named refusal
   post-autoincr-window-launch-arithmetic-skew.  True means one delivery
   per replaced site is structurally proven, so the carried access
   executes exactly as often as the fold accounted.  */

bool
reform_carried_launch_arithmetic_ok (replay_block const &block,
				     replay_sequence const &seq)
{
  auto refuse = [] (const char *why) -> bool
    {
      rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_LAUNCH_ARITHMETIC_SKEW,
		   dump_file,
		   "Replay re-formation refusal:"
		   " post-autoincr-window-launch-arithmetic-skew: %s\n", why);
      return false;
    };

  switch (clones_word_exact_walk (block, seq, /*require_insn_code=*/false))
    {
    case CLONE_WALK_EMPTY:
      return refuse ("no clones");
    case CLONE_WALK_OVERLAP:
      return refuse ("overlapping or unordered clone spans"
		     " (a site would be delivered twice)");
    case CLONE_WALK_WORD:
      return refuse ("clone word differs from recorded word"
		     " (delivered carried execution would not be"
		     " the replaced site's word)");
    case CLONE_WALK_COUNT:
      return refuse ("clone word count differs from recorded length");
    case CLONE_WALK_OK:
      break;
    }

  if (dump_file)
    fprintf (dump_file,
	     "post-autoincr-window: carried payload launch arithmetic"
	     " proven: %u sites == %u deliveries"
	     " (word-exact non-overlapping clones,"
	     " one delivery per replaced site)\n",
	     unsigned (seq.clones.size ()), unsigned (seq.clones.size ()));
  return true;
}

/* Lane IM (hoisted-record window sizing;
   -mtt-tensix-optimize-replay-window-sizing, Init(0); rvtt-cost.md
   "REPLAY WINDOW SIZING UNDER A HOISTED RECORD").

   pick_replay's saving key prices IN-BLOCK exec-while-record economics:
   each replaced clone saves (length - 1) words against one launch word,
   and the record itself stays in the body -- so a shorter window with
   more instances wins (lcm-fresh: 14 words x 7 instances, saving 77,
   over 28 words x 3 instances, saving 53) -- a key measured RIGHT
   for in-block windows.  A record the driver then HOISTS out
   of the loop (record-hoist / replay-hoist preheader placement) changes
   the economics: the record is delivered once per placement and the
   per-trip cost is the LAUNCH WORDS alone, so the widest word-exact
   window that fits the free slots minimizes per-trip issue.  The hand
   kernels witness exactly this discipline (gcd/lcm: TTI_REPLAY(0,28,0,1)
   once per kernel, then 3x REPLAY(0,28) + 1x REPLAY(0,13) per row --
   the last launch a PARTIAL playback of the recorded window's prefix,
   a shape the formation vocabulary could not express at all: every
   launch it emits carries the full recorded length).

   The helpers below re-pick the window AFTER the hoist admission of the
   original pick has succeeded (and only then -- in-block picks keep
   pick_replay's measured-right key):

   - window_sizing_clones_exact_p: structural re-verification that a
     candidate's clones are non-overlapping, ascending, word-exact
     copies of its recorded window (discovery matches by hash; a launch
     delivers the RECORDED words at every site, so word-exactness is
     the launch-arithmetic premise -- the reform-mode audit's contract).

   - window_sizing_prefix_trim: the longest trailing run after the last
     clone that is a word-exact PREFIX of the recorded window, delivered
     as one partial launch (ISA: a REPLAY launch emits
     ReplayBuffer[(Index+i)%32] for i in [0,Count) -- a pure prefix of
     the recorded program, independent of the recorded length; both
     functional models, and the hand kernels' REPLAY(0,13) on
     hardware).
     The walk mirrors sequence-growth continuity exactly: it never
     crosses a must_end word, a deleted insn, or the block end, and
     stops one word short of a full extra clone.

   - window_sizing_widen: among same-anchor wider candidates that fit
     the largest free slot span, pick the one that strictly minimizes
     per-trip delivered issue words over the covered span
     (launches + partial launch + words left inline); every premise
     failing keeps the original pick by name.

   The caller then re-runs the FULL hoist admission (hoist_preheader:
   oracle, pricing, placement, lift) on the widened candidate itself and
   falls back to the original pick if any of it refuses.  The committed
   transform is the existing hoisted no-exec capture at the existing
   placement machinery -- only WIDER, plus one partial launch whose
   soundness premises are the full launch's (record dominates the
   launch; slots persistent and disjoint per the FS model; the window
   checker resolves sub-span launches).  The partial launch replaces
   words that executed inline with the same delivered words in the same
   stream position: stream-identity, like every full clone replacement.
   Deliberately NOT taken: widening in reform_mode (a widened carried
   payload would need the launch-arithmetic audit re-derived for the
   trim; refused by name window-sizing-reform-composition-unaudited),
   and widening of exec-while-record in-block picks (pick_replay's key
   is measured right there).  */

static bool
window_sizing_clones_exact_p (replay_block const &block,
			      replay_sequence const &cand)
{
  auto refuse = [] (const char *why) -> bool
    {
      rvtt_refuse (RVTT_REF_WINDOW_SIZING_CLONE_ARITHMETIC, dump_file,
		   "window-sizing candidate refused:"
		   " window-sizing-clone-arithmetic: %s\n", why);
      return false;
    };

  /* The shared clone walk (clones_word_exact_walk above), at this
     spelling's stricter plain-INSN check.  */
  switch (clones_word_exact_walk (block, cand, /*require_insn_code=*/true))
    {
    case CLONE_WALK_EMPTY:
      /* A discovered candidate is its own first clone.  */
      gcc_unreachable ();
    case CLONE_WALK_OVERLAP:
      return refuse ("overlapping or unordered clone spans");
    case CLONE_WALK_WORD:
      return refuse ("clone word differs from recorded word");
    case CLONE_WALK_COUNT:
      return refuse ("clone word count differs from recorded length");
    case CLONE_WALK_OK:
      break;
    }
  return true;
}

/* Measure CAND's trailing prefix run (second bullet of the
   window-sizing design comment above): the longest run after the last
   clone that is a word-exact prefix of the recorded window, stopping
   one word short of a full extra clone and never crossing a must_end
   word or the block end.  Returns the run's word count (0: no trim) and
   stores the block position one past the run in *TRIM_END_OUT.  */

static unsigned
window_sizing_prefix_trim (replay_block const &block,
			   replay_sequence const &cand,
			   unsigned *trim_end_out)
{
  auto const &first = cand.clones.front ();
  unsigned pos = cand.clones.back ().end;
  unsigned fx = first.begin;
  unsigned matched = 0;
  unsigned end = pos;

  /* A full-length trailing run would be another clone, not a trim.  */
  while (matched + 1 < cand.length && pos != block.size ())
    {
      while (fx != first.end && block[fx].empty)
	++fx;
      if (fx == first.end)
	break;
      if (block[pos].empty)
	{
	  ++pos;
	  continue;
	}
      if (GET_CODE (block[pos].insn) != INSN
	  || GET_CODE (block[fx].insn) != INSN
	  || !rvtt_dcost_replay_word_equal_p (block[fx].insn,
					      block[pos].insn))
	break;
      /* Sequence-growth continuity: never walk past a must_end word.  */
      bool stop = block[pos].must_end;
      ++matched;
      ++pos;
      ++fx;
      end = pos;
      if (stop)
	break;
    }

  *trim_end_out = end;
  return matched;
}

/* Re-pick an admitted hoist's window (third bullet of the window-sizing
   design comment above): among ACTIVE candidates longer than SEQ that
   share its anchor position, fit in FREE_SPAN slots, and pass the
   companion (under STICKY) and clone-exactness checks, return the one
   whose covered span costs strictly the fewest delivered per-trip issue
   words -- counting launches, one partial prefix launch for the
   trailing trim, and the words left inline -- with its trim reported
   through *TRIM_LEN_OUT/*TRIM_END_OUT.  Null (with a named refusal)
   keeps SEQ, the original pick.  */

replay_sequence *
window_sizing_widen (replay_active &active, replay_sequence *seq,
		     replay_block const &block, unsigned free_span,
		     bool sticky, unsigned *trim_len_out,
		     unsigned *trim_end_out)
{
  unsigned begin = seq->clones.front ().begin;
  unsigned seq_cov_end = seq->clones.back ().end;

  replay_sequence *best = nullptr;
  unsigned best_cost = 0, best_trim = 0, best_trim_end = 0;
  bool saw_overflow = false;

  for (auto *cand : active)
    {
      if (cand == seq
	  || cand->length <= seq->length
	  || cand->clones.front ().begin != begin)
	continue;
      if (cand->length > free_span)
	{
	  /* A wider same-anchor candidate exists but the free replay
	     slots cannot hold its record.  */
	  saw_overflow = true;
	  continue;
	}
      if (cand->companion_ok < 0)
	cand->companion_ok
	  = span_companion_sound_p (block, cand->clones.front (), sticky);
      if (!cand->companion_ok)
	continue;
      if (!window_sizing_clones_exact_p (block, *cand))
	continue;

      unsigned trim_end = 0;
      unsigned trim = window_sizing_prefix_trim (block, *cand, &trim_end);
      unsigned cov_end = trim ? trim_end : cand->clones.back ().end;
      if (cov_end < seq_cov_end)
	{
	  rvtt_refuse (RVTT_REF_WINDOW_SIZING_COVERAGE_SHORT, dump_file,
		       "window-sizing candidate refused:"
		       " window-sizing-coverage-short: [%u,+%u) x%u + %u-word"
		       " trim ends at %u, before the picked window's %u\n",
		       begin, cand->length, unsigned (cand->clones.size ()),
		       trim, cov_end, seq_cov_end);
	  continue;
	}

      /* Delivered per-trip issue words over the covered span, both
	 shapes: launches, plus one partial launch, plus every word the
	 shape leaves inline.  The current pick keeps its trailing run
	 inline (this mechanism adds no partial launch to it).  */
      unsigned ne = 0;
      for (unsigned ix = begin; ix != cov_end; ++ix)
	if (!block[ix].empty)
	  ++ne;
      unsigned covered = cand->length * unsigned (cand->clones.size ()) + trim;
      gcc_checking_assert (ne >= covered);
      /* The one delivered-issue spelling (rvtt-delivery-cost-core.h
	 window_trip_issue_words).  */
      unsigned cost_cand = rvtt_delivery_cost::window_trip_issue_words
	(unsigned (cand->clones.size ()), trim != 0, ne - covered);
      unsigned cur_covered = seq->length * unsigned (seq->clones.size ());
      unsigned cost_cur = rvtt_delivery_cost::window_trip_issue_words
	(unsigned (seq->clones.size ()), false, ne - cur_covered);
      if (cost_cand >= cost_cur)
	{
	  rvtt_refuse (RVTT_REF_WINDOW_SIZING_NO_CHEAPER_DELIVERY, dump_file,
		       "window-sizing candidate refused:"
		       " window-sizing-no-cheaper-delivery: [%u,+%u) x%u +"
		       " %u-word trim delivers %u issue words vs %u\n",
		       begin, cand->length, unsigned (cand->clones.size ()),
		       trim, cost_cand, cost_cur);
	  continue;
	}
      if (!best || cost_cand < best_cost
	  || (cost_cand == best_cost && cand->length < best->length))
	{
	  best = cand;
	  best_cost = cost_cand;
	  best_trim = trim;
	  best_trim_end = trim_end;
	}
    }

  if (!best)
    {
      if (dump_file)
	{
	  if (saw_overflow)
	    rvtt_refuse (RVTT_REF_WINDOW_SIZING_SLOT_EXHAUSTED, dump_file,
			 "window-sizing refused: window-sizing-slot-exhausted:"
			 " every wider same-anchor candidate exceeds the free"
			 " slot span %u; keeping the picked [%u,+%u) window\n",
			 free_span, begin, seq->length);
	  else
	    rvtt_refuse (RVTT_REF_WINDOW_SIZING_NO_WIDER_CANDIDATE, dump_file,
			 "window-sizing refused:"
			 " window-sizing-no-wider-candidate:"
			 " no admissible wider same-anchor"
			 " candidate; keeping the"
			 " picked [%u,+%u) window\n", begin, seq->length);
	}
      return nullptr;
    }

  unsigned cur_covered = seq->length * unsigned (seq->clones.size ());
  if (dump_file)
    fprintf (dump_file,
	     "window-sizing: widened [%u,+%u) x%u (covering %u words) to"
	     " [%u,+%u) x%u + %u-word prefix trim (per-trip deliveries"
	     " %u -> %u)\n",
	     begin, seq->length, unsigned (seq->clones.size ()), cur_covered,
	     begin, best->length, unsigned (best->clones.size ()), best_trim,
	     unsigned (seq->clones.size ()),
	     unsigned (best->clones.size ()) + (best_trim != 0));

  *trim_len_out = best_trim;
  *trim_end_out = best_trim_end;
  return best;
}

/* ==================================================================
   SUFFIX-AUTOMATON MAXIMAL-REPEAT DISCOVERY, RUN AS A SHADOW.

   The legacy discovery (build_sequences) grows every candidate by one
   insn at a time and is self-admittedly O(N^2) (":448"); the classical
   structure for the same question -- enumerate every factor of a mapped
   instruction stream that repeats -- is the suffix automaton (LLVM's
   MachineOutliner uses the equivalent suffix tree).  This is stage A of
   the two-stage plan: the automaton ENUMERATES, the legacy candidate
   list and the existing greedy picker still DECIDE, and nothing about
   the formed windows moves.  Stage B (admitting the extra candidates)
   is a separate ceremony.

   SYMBOLS.  One integer symbol per non-empty entry of the scanned
   block, formed under EXACTLY the legacy length-1 sequence classes:
   same crc32 class hash (which folds in the register GENERATION counter
   reg_ages, the piece that makes textual equality value-safe), same
   generation equality, same word-equality re-check through the same
   rvtt_dcost_replay_word_equal_p comparator (rvtt-delivery-cost.cc),
   first-match-wins in increasing block order.  Two insns therefore get
   one symbol iff the legacy discovery would have merged them.

   BOUNDARIES.  Wherever the legacy extension refuses to walk (a
   must_end entry -- asm, non-Tensix, a replay owner, a variable
   capture, the end of the block, or any empty entry in between that is
   itself must_end), a UNIQUE separator symbol is spliced into the
   string.  A unique symbol occurs once, so no factor containing it can
   have two occurrences: candidates never cross a boundary BY
   CONSTRUCTION, which is why admission semantics cannot move.

   CANDIDATES.  Each automaton state is a right-extension-maximal class
   of factors sharing one endpos set; its occurrence count is the endpos
   size.  A candidate is a (state, length) pair with length in
   (len[link], len[state]], length >= MIN_SEQUENCE, length <=
   max_length (the replay buffer bound the legacy discovery also
   applies), and two or more occurrences surviving the same greedy
   overlap triage active_triage performs.

   WHAT THIS PRINTS.  Under -mtt-tensix-replay-shadow-discovery (a
   measurement knob, default off, no behaviour attached), per block:

     replay-maximal-repeats: <census line>
     replay-maximal-repeats-missed: <candidate the picker was never offered>
     replay-maximal-repeats-truncated: <maximal repeat longer than the buffer>

   THE STAGE-A OBLIGATION.  Under -fchecking -- independently of the
   knob, so an ordinary checking build discharges it corpus-wide -- every
   legacy candidate is looked up in the automaton and asserted to be
   enumerable there WITH THE IDENTICAL TRIAGED CLONE SET.  The risk this
   catches is not hash collision (the legacy path re-checks with
   rtx_equal_p and so does the symbol mapping) but the reverse: a symbol
   mapping that DISTINGUISHES what the legacy path merged would silently
   shrink the candidate set.  The picker's input is untouched in stage A,
   so the selected windows are identical by construction.  */

namespace replay_sa {

/* Largest symbol string this shadow will build.  The census is a
   measurement instrument: a pathological block is reported and skipped
   rather than paying for endpos materialisation.  */
constexpr unsigned MAX_SHADOW_SYMBOLS = 4096;

struct state
{
  int len;
  int link;
  std::map<int, int> next;

  state (int l, int lk) : len (l), link (lk) {}
};

/* Textbook online suffix-automaton construction: O(n) states and
   transitions in the alphabet-independent formulation, one clone split
   per divergent transition.  */

struct automaton
{
  std::vector<state> st;
  int last;

  automaton () : last (0) { st.emplace_back (0, -1); }

  void extend (int c)
  {
    int cur = int (st.size ());
    st.emplace_back (st[last].len + 1, -1);
    int p = last;
    while (p != -1 && !st[p].next.count (c))
      {
	st[p].next[c] = cur;
	p = st[p].link;
      }
    if (p == -1)
      st[cur].link = 0;
    else
      {
	int q = st[p].next[c];
	if (st[p].len + 1 == st[q].len)
	  st[cur].link = q;
	else
	  {
	    int clone = int (st.size ());
	    st.emplace_back (st[p].len + 1, st[q].link);
	    st[clone].next = st[q].next;
	    while (p != -1)
	      {
		auto it = st[p].next.find (c);
		if (it == st[p].next.end () || it->second != q)
		  break;
		it->second = clone;
		p = st[p].link;
	      }
	    st[q].link = clone;
	    st[cur].link = clone;
	  }
      }
    last = cur;
  }

  /* Walk SYMS from the root.  Returns the state whose class contains
     that factor, or -1 if the factor does not occur at all.  */
  int walk (std::vector<int> const &syms) const
  {
    int v = 0;
    for (int c : syms)
      {
	auto it = st[v].next.find (c);
	if (it == st[v].next.end ())
	  return -1;
	v = it->second;
      }
    return v;
  }
};

/* The whole shadow view of one scanned block.  */

struct view
{
  /* Symbol string, and the block index each real symbol came from
     (UINT_MAX for a separator).  */
  std::vector<int> str;
  std::vector<unsigned> at;
  /* Symbol per non-empty block entry (UINT_MAX-indexed lookup by block
     position); -1 where the entry is empty.  */
  std::vector<int> sym_of;
  unsigned classes = 0;
  unsigned segments = 1;

  automaton sa;
  /* Occurrence END positions in STR, per state, ascending.  */
  std::vector<std::vector<int> > endpos;
  bool built = false;
};

/* Form the symbol string for BLOCK.  Mirrors scan_insns' own notion of
   continuity: EMPTY entries never carry a symbol (they do not count
   toward a sequence's length and the legacy extension walks over them),
   and a break is spliced wherever the legacy extension's must_end check
   would have refused to walk from one real entry to the next.  */

static void
build_symbols (view &v, replay_block const &block)
{
  std::map<unsigned, std::vector<unsigned> > buckets; /* hash -> class ids */
  /* class id -> block index */
  std::vector<unsigned> rep;
  int next_sep = -1;

  v.sym_of.assign (block.size (), -1);

  unsigned prev = UINT_MAX;
  for (unsigned ix = 0; ix != block.size (); ++ix)
    {
      if (block[ix].empty)
	continue;

      /* Legacy continuity: from the previous real entry the extension
	 walks forward over empties, refusing at the first must_end it
	 meets (the entry itself, or any empty it steps over).  */
      if (prev != UINT_MAX)
	{
	  bool linked = true;
	  for (unsigned j = prev; j != ix; ++j)
	    if (block[j].must_end)
	      {
		linked = false;
		break;
	      }
	  if (!linked)
	    {
	      v.str.push_back (next_sep--);
	      v.at.push_back (UINT_MAX);
	      v.segments++;
	    }
	}

      /* Class formation, byte-for-byte the legacy length-1 rule.  */
      int sym = -1;
      auto slot = buckets.emplace (block[ix].hash, std::vector<unsigned> ());
      for (auto id : slot.first->second)
	{
	  auto &other = block[rep[id]];
	  if (other.generation != block[ix].generation)
	    continue;
	  if (!rvtt_dcost_replay_word_equal_p (other.insn,
						block[ix].insn))
	    continue;
	  sym = int (id);
	  break;
	}
      if (sym < 0)
	{
	  sym = int (rep.size ());
	  rep.push_back (ix);
	  slot.first->second.push_back (unsigned (sym));
	}

      v.sym_of[ix] = sym;
      v.str.push_back (sym);
      v.at.push_back (ix);
      prev = ix;
    }

  v.classes = unsigned (rep.size ());
}

/* Build the automaton and materialise every state's endpos set (the
   occurrence ends), by one pass down the suffix-link tree in decreasing
   state length -- the standard linear counting-sort order.  */

static bool
build (view &v, replay_block const &block)
{
  build_symbols (v, block);
  if (v.str.empty () || v.str.size () > MAX_SHADOW_SYMBOLS)
    return false;

  std::vector<int> prefix_state;
  prefix_state.reserve (v.str.size ());
  for (int c : v.str)
    {
      v.sa.extend (c);
      prefix_state.push_back (v.sa.last);
    }

  unsigned n = unsigned (v.sa.st.size ());
  v.endpos.assign (n, std::vector<int> ());
  for (unsigned i = 0; i != v.str.size (); ++i)
    v.endpos[prefix_state[i]].push_back (int (i));

  /* Counting sort of the states by len.  */
  unsigned maxlen = 0;
  for (auto const &s : v.sa.st)
    maxlen = MAX (maxlen, unsigned (s.len));
  std::vector<unsigned> bucket (maxlen + 2, 0);
  for (auto const &s : v.sa.st)
    bucket[s.len]++;
  for (unsigned i = 1; i <= maxlen; ++i)
    bucket[i] += bucket[i - 1];
  std::vector<unsigned> order (n);
  for (unsigned i = n; i--;)
    order[--bucket[v.sa.st[i].len]] = i;

  for (unsigned i = n; i-- > 1;)
    {
      unsigned s = order[i];
      int link = v.sa.st[s].link;
      if (link <= 0)
	continue;
      auto &dst = v.endpos[link];
      dst.insert (dst.end (), v.endpos[s].begin (), v.endpos[s].end ());
    }
  for (unsigned i = 1; i != n; ++i)
    std::sort (v.endpos[i].begin (), v.endpos[i].end ());

  v.built = true;
  return true;
}

/* Occurrences of the LENGTH-symbol factor whose class is STATE, after
   the same greedy left-to-right overlap triage active_triage runs on
   the legacy clone lists.  */

static std::vector<replay_span>
triaged_clones (view const &v, int st, unsigned length)
{
  std::vector<replay_span> out;
  unsigned bound = 0;
  for (int end_ix : v.endpos[st])
    {
      unsigned begin_ix = unsigned (end_ix) + 1 - length;
      unsigned begin = v.at[begin_ix];
      unsigned end = v.at[unsigned (end_ix)] + 1;
      gcc_checking_assert (begin != UINT_MAX && end != UINT_MAX);
      if (!out.empty () && bound > begin)
	continue;
      bound = end;
      out.emplace_back (begin, end);
    }
  return out;
}

/* The factor a legacy candidate span carries, as symbols.  */

static std::vector<int>
span_symbols (view const &v, replay_block const &block, replay_span span)
{
  std::vector<int> out;
  for (unsigned ix = span.begin; ix != span.end; ++ix)
    if (!block[ix].empty)
      out.push_back (v.sym_of[ix]);
  return out;
}

} /* namespace replay_sa */

/* Run the shadow.  BLOCK/ACTIVE are the legacy scan and its triaged
   candidate list, MAX_LENGTH the replay-buffer bound the legacy
   discovery grew to, PICK_LIMIT the free-span length the picker will
   actually honour.  Prints nothing unless the measurement knob is on;
   asserts the stage-A superset obligation whenever -fchecking is on.  */

void
shadow_discovery_census (replay_block const &block,
			 replay_active const &active, unsigned legacy_seqs,
			 unsigned max_length, unsigned pick_limit,
			 bool sticky, int bb_index)
{
  bool talk = riscv_tt_replay_shadow_discovery > 0 && dump_file;
  replay_sa::view v;

  if (!replay_sa::build (v, block))
    {
      if (talk && !v.str.empty ())
	fprintf (dump_file,
		 "replay-maximal-repeats: bb %d: shadow skipped"
		 " (%u symbols over the %u bound)\n",
		 bb_index, unsigned (v.str.size ()),
		 replay_sa::MAX_SHADOW_SYMBOLS);
      return;
    }

  unsigned nstates = unsigned (v.sa.st.size ());

  /* -------- the stage-A obligation: legacy candidates are a subset. */
  unsigned legacy_n = 0, violations = 0;
  std::set<uint64_t> legacy_key; /* (first clone begin << 32) | length */
  unsigned legacy_best_saving = 0;
  for (auto *seq : active)
    {
      legacy_n++;
      legacy_key.insert ((uint64_t (seq->clones.front ().begin) << 32)
			 | seq->length);

      if (seq->length <= pick_limit
	  && span_companion_sound_p (block, seq->clones.front (), sticky))
	{
	  unsigned saving = (unsigned (seq->clones.size ()) - 1)
	    * (seq->length - 1) - !(riscv_tt_fix_qsr_replay > 0);
	  legacy_best_saving = MAX (legacy_best_saving, saving);
	}

      auto syms = replay_sa::span_symbols (v, block, seq->clones.front ());
      bool ok = syms.size () == seq->length;
      int st = ok ? v.sa.walk (syms) : -1;
      if (st > 0)
	{
	  /* The factor must live in this state's class (its length must
	     exceed the suffix link's longest), and its triaged
	     occurrences must be the legacy clone list verbatim.  */
	  ok = unsigned (v.sa.st[st].len) >= seq->length
	    && unsigned (v.sa.st[v.sa.st[st].link].len) < seq->length;
	  if (ok)
	    {
	      auto clones = replay_sa::triaged_clones (v, st, seq->length);
	      ok = clones.size () == seq->clones.size ();
	      for (unsigned i = 0; ok && i != clones.size (); ++i)
		ok = clones[i].begin == seq->clones[i].begin
		  && clones[i].end == seq->clones[i].end;
	    }
	}
      else
	ok = false;

      if (!ok)
	{
	  violations++;
	  if (talk)
	    fprintf (dump_file,
		     "replay-maximal-repeats-superset-violation: bb %d:"
		     " legacy candidate [%u,%u) length %u x%u is not"
		     " enumerable from the automaton with the same clones\n",
		     bb_index, seq->clones.front ().begin,
		     seq->clones.front ().end, seq->length,
		     unsigned (seq->clones.size ()));
	}
    }

  /* -------- the automaton's own candidate set. */
  unsigned auto_n = 0, missed = 0, truncated = 0, truncated_max = 0;
  unsigned maximal = 0, left_maximal = 0;
  for (unsigned st = 1; st != nstates; ++st)
    {
      if (v.endpos[st].size () < 2)
	continue;

      unsigned own = unsigned (v.sa.st[st].len);
      unsigned parent = unsigned (v.sa.st[v.sa.st[st].link].len);

      /* Classification of the state's OWN (right-extension-maximal)
	 repeat: left-extension-maximal iff its occurrences are not all
	 preceded by one and the same symbol -- otherwise the longer
	 repeat one symbol to the left carries the same occurrences and
	 this one is a pure suffix of it.  */
      if (own >= unsigned (MIN_SEQUENCE))
	{
	  maximal++;
	  bool left_max = false;
	  int prev_sym = 0;
	  bool first = true;
	  for (int end_ix : v.endpos[st])
	    {
	      int begin_ix = end_ix + 1 - int (own);
	      if (begin_ix == 0)
		{
		  left_max = true;
		  break;
		}
	      int c = v.str[begin_ix - 1];
	      if (first)
		{
		  prev_sym = c;
		  first = false;
		}
	      else if (c != prev_sym)
		{
		  left_max = true;
		  break;
		}
	    }
	  if (left_max)
	    left_maximal++;
	}

      if (own > max_length)
	{
	  truncated++;
	  truncated_max = MAX (truncated_max, own);
	}

      unsigned hi = MIN (own, max_length);
      unsigned lo = MAX (parent + 1, unsigned (MIN_SEQUENCE));
      for (unsigned len = lo; len <= hi; ++len)
	{
	  auto clones = replay_sa::triaged_clones (v, int (st), len);
	  if (clones.size () < 2)
	    continue;
	  auto_n++;

	  if (legacy_key.count ((uint64_t (clones.front ().begin) << 32) | len))
	    continue;

	  /* A candidate the legacy discovery never offered the picker.
	     Price it with the picker's own key so the census is a value
	     estimate for stage B, and run the companion contract so an
	     inadmissible one is not counted as an opportunity.  */
	  missed++;
	  if (!talk)
	    continue;
	  unsigned saving = (unsigned (clones.size ()) - 1) * (len - 1)
	    - !(riscv_tt_fix_qsr_replay > 0);
	  bool sound = span_companion_sound_p (block, clones.front (), sticky);
	  fprintf (dump_file,
		   "replay-maximal-repeats-missed: bb %d: [%u,%u) length %u"
		   " x%u saving %u%s%s (legacy best saving %u)\n",
		   bb_index, clones.front ().begin, clones.front ().end,
		   len, unsigned (clones.size ()), saving,
		   sound ? "" : " companion-refused",
		   len <= pick_limit ? "" : " over-slot-limit",
		   legacy_best_saving);
	}
    }

  if (talk)
    {
      fprintf (dump_file,
	       "replay-maximal-repeats: bb %d: %u symbols in %u classes,"
	       " %u segments, %u automaton states vs %u grown sequences;"
	       " maximal repeats %u (left-maximal %u); legacy candidates %u,"
	       " automaton candidates %u, automaton-only %u,"
	       " superset %s (%u violations)\n",
	       bb_index, unsigned (v.str.size ()), v.classes, v.segments,
	       nstates, legacy_seqs, maximal, left_maximal, legacy_n, auto_n,
	       missed, violations ? "VIOLATED" : "OK", violations);
      if (truncated)
	fprintf (dump_file,
		 "replay-maximal-repeats-truncated: bb %d: %u maximal"
		 " repeats longer than the %u-word buffer bound"
		 " (longest %u)\n",
		 bb_index, truncated, max_length, truncated_max);
    }

  /* The stage-A proof artifact: a checking build discharges it over
     whatever it compiles, and the corpus checking leg discharges it
     corpus-wide.  */
  if (flag_checking)
    gcc_assert (!violations);
}

/* Greedy selection: return the candidate from ACTIVE with the greatest
   modeled word saving -- (clones - 1) * (length - 1), less the extra
   launch the no-exec-while-record workaround costs -- whose recorded
   length fits LIMIT free slots and whose first clone passes the
   companion-soundness contract (verdict cached in companion_ok; STICKY
   is the function's shadow-coupling possibility).  Ties prefer the
   longer window.  Null when nothing qualifies.  */

replay_sequence *
pick_replay (replay_active &active, unsigned limit, replay_block const &block,
	     bool sticky)
{
  replay_sequence *result = nullptr;
  unsigned best = 0;

  for (auto *seq : active)
    {
      gcc_assert (seq->clones.size () > 1);
      if (seq->length > limit)
	break;
      if (seq->companion_ok < 0)
	seq->companion_ok
	  = span_companion_sound_p (block, seq->clones.front (), sticky);
      if (!seq->companion_ok)
	continue;
      /* Quasar exec-while-load doesn't work, so we need an extra replay */
      unsigned saving = (seq->clones.size () - 1) * (seq->length - 1)
	- !(riscv_tt_fix_qsr_replay > 0);
      /* Measurement-only selection override (reform mode,
	 -mtt-tensix-post-autoincr-window-prefer-longest): length-major
	 key, word-saving as the tie-break.  */
      if (reform_mode && riscv_tt_post_autoincr_window_prefer_longest > 0)
	{
	  unsigned cur_len = result ? result->length : 0;
	  if (seq->length > cur_len
	      || (seq->length == cur_len && best < saving))
	    {
	      best = saving;
	      result = seq;
	    }
	  continue;
	}
      if (best < saving
	  || (best == saving && result && result->length < seq->length))
	{
	  best = saving;
	  result = seq;
	}
    }

  return result;
}

/* Commit the picked in-block candidate SEQ, recording into slots
   [REPLAY_START, +length).  Normally the capture executes while
   recording: it is emitted before the first clone (which stays as the
   recorded payload) and every later clone becomes one playback launch
   with its insns deleted.  Under the no-exec-while-record workaround
   the capture swallows the first clone instead, that clone's insns stay
   as the (un-executed) payload, and ALL clones -- the first included --
   launch.  Returns the recorded length.  */

unsigned
replace_sequence (replay_sequence &seq, replay_block &block,
		  unsigned replay_start)
{
  unsigned length = seq.length;
  bool not_quasar_fix = !(riscv_tt_fix_qsr_replay > 0);
  if (dump_file)
    {
      unsigned saving = (seq.length - 1) * (unsigned (seq.clones.size ()) - 1)
      - not_quasar_fix;
      fprintf (dump_file,
	       "Capturing %ssequence [%u,%u) %u instances to [%u,+%u)"
	       " saving=%u\n",
	       not_quasar_fix ? "and executing " : "",
	       seq.clones.front ().begin, seq.clones.front ().end,
	       unsigned (seq.clones.size ()),
	       replay_start, length, saving);
      dump_sequence (dump_file, block, seq.clones.front (), replay_start);
      fprintf (dump_file, "\n");
    }

  /* Cannot exec while capturing on quasar */
  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode),
     GEN_INT (replay_start), GEN_INT (not_quasar_fix), GEN_INT (1));
  emit_insn_before (capture, block[seq.clones.front ().begin].insn);

  /* Make sure we've not deleted anything in this instance already */
  for (auto pos = block.data () + seq.clones.front ().begin,
	 end = block.data () + seq.clones.front ().end;
       pos != end; pos++)
    gcc_assert (GET_CODE (pos->insn) == INSN);

  for (auto clone = seq.clones.begin () + not_quasar_fix;
       clone != seq.clones.end (); ++clone)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode),
	 GEN_INT (replay_start), const0_rtx, const0_rtx);
      auto *insn = emit_insn_after (replay, block[clone->end - 1].insn);
      if (dump_file)
	{
	  fprintf (dump_file,
		   not_quasar_fix ? "Replaying " : "Replaying original ");
	  dump_insn_slim (dump_file, insn);
	}
      if (not_quasar_fix)
	{
	  unsigned ix = replay_start;
	  for (auto pos = block.data () + clone->begin,
		 end = block.data () + clone->end;
	       pos != end; pos++)
	    {
	      if (dump_file)
		{
		  if (pos->empty)
		    fprintf (dump_file, "Deleting -: ");
		  else
		    fprintf (dump_file, "Deleting %u: ", ix++);
		  dump_insn_slim (dump_file, pos->insn);
		}
	      gcc_assert (GET_CODE (pos->insn) == INSN);
	      SET_INSN_DELETED (pos->insn);
	    }
	}
      else
	not_quasar_fix = true;
      if (dump_file)
	fprintf (dump_file, "\n");
    }

  return length;
}

/* Return true when X is a post-RA, fixed-encoding SFPU pattern.  Hard LREGs,
   constants and compiler scratch outputs are fixed.  A GPR or MEM means that
   the instruction word can still be synthesized at run time and therefore
   cannot be recorded without executing at its original location.  */
bool
fixed_replay_rtx_p (const_rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case SCRATCH:
      return true;

    case REG:
      return SFPU_REG_P (REGNO (x));

    case SET:
      return fixed_replay_rtx_p (SET_DEST (x))
	&& fixed_replay_rtx_p (SET_SRC (x));

    case CLOBBER:
    case USE:
      return fixed_replay_rtx_p (XEXP (x, 0));

    case PARALLEL:
    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (int ix = XVECLEN (x, 0); ix--;)
	if (!fixed_replay_rtx_p (XVECEXP (x, 0, ix)))
	  return false;
      return true;

    default:
      return false;
    }
}

/* Provable constant trip counts.

   The bounded forward
   evaluation this pass founded (see the design comment retired to
   rvtt-trips.cc) moved verbatim to the shared trip-count facade
   rvtt_loop_trips (rvtt-trips.{h,cc}).  The facade is dual-oracle:
   the legacy simulation still DECIDES -- callers below receive
   byte-identical verdicts and outputs -- while the classical loop-iv
   analysis runs as a cross-check whose disagreements dump as
   trip-oracle-divergence.  */

/* Generic replay-hoist profitability model.

   Hoisting converts one in-loop recording pass per trip (re-recording the
   payload, WITH execution) into a single record-only capture pass in the
   preheader plus one playback launch per trip.  The in-loop recording is
   not pure overhead: while being recorded the payload performs the loop's
   real work, so removing it saves only the delivery/overhead premium
   over replay reissue, while the added preheader record-only pass
   executes nothing.  The pricing arithmetic lives in the next comment
   block and the full hardware-anchor derivation with the constants in
   rvtt-cost.md.

   Execution-saturation context term (the LAUNCH_RUN parameter;
   delivery-bound re-record bodies only).  The delivery-bound
   `before = deliver_record' pricing assumes removing the record pass's
   delivered words shortens the trip.  That is false when the body's
   sibling launches of the SAME capture buffer are contiguous in the
   final instruction stream: a contiguous run of R launches occupies the
   issue plane for R * exec centislots while delivering only
   R * RISC_PUSH_X100, and the record pass's delivery streams into that
   execution shadow instead of extending the trip.  When the run's
   execution surplus covers the record pass's delivery,

     launch_run * (exec - RISC_PUSH_X100) >= deliver_record

   the per-trip relief is ~zero: before = after, so benefit degenerates
   to -record (the preheader pass is pure cost) and the hoist refuses.
   A single launch of a delivery-bound payload can never satisfy this
   (exec < deliver_record in this branch), so counted-loop hoists --
   whose per-trip launch is always separated from the next trip's by the
   loop control words -- and all single-instance shapes are unaffected
   and pass LAUNCH_RUN = 1.  An EXECUTION-bound record pass is never
   hidden this way: its cost is its own interlocked execution plus the
   exposed record-engine overhead, which no sibling surplus can absorb
   -- the Reduce-class A/B (trips 4, words 8, exec_ilk 12, 8 siblings)
   measured the in-loop record pass ~2.8 cyc/trip MORE expensive than
   its launch inside a fully execution-backlogged body, and hoisting it
   won 21.5+ cyc/body (855.5 -> 832.75).  Silicon anchor for the
   delivery-bound term: the unary-max/min shape (trips 4, words 4, 8
   contiguous sibling launches per trip) measured +2.06% when hoisted
   under the delivery-only model (which priced it +245).

   Hoist only when benefit >= the minimum-benefit threshold
   (XTT_REPLAY_HOIST_MIN_BENEFIT, overridable in the same centislot units
   through -mtt-tensix-replay-hoist-min-benefit=; the saturation term is
   part of the modeled benefit, not of the threshold, so an override
   cannot force a hoist whose record delivery is hidden).

   TRIPS must be provable (see rvtt_loop_trips, rvtt-trips.cc).  An unknown
   or merely estimated trip count refuses the hoist, which keeps the
   emitted code byte-identical to the unhoisted form.  The decision inputs
   are exactly the provable trip count, the capture length, the longest
   contiguous sibling-launch run, and the cost-table constants.  */

/* Interlock-aware replay-hoist pricing (recalibrated from a
   five-shape diagnosis validated by a 14-shape hardware matrix; the
   re-record branch was re-derived against the Reduce-class and
   Log-class hardware anchors after the first spelling inverted both --
   full derivation and the five-anchor table in rvtt-cost.md).

   The delivery-only model priced the replay reissue stall-free
   (after = max(PUSH, len*SLOT)): on serially-chained short bodies that
   converts a delivery-bound loop into an equally-or-more expensive
   execution-bound one.  Silicon charges the reissue len + the audited
   RAW interlock stalls + a per-launch turnaround.  Per trip, in
   centislots (constants in rvtt-cost.md):

     exec   = exec_interlocked_slots(payload) * SLOT
     after  = max(PUSH, exec + TURNAROUND)               ; launch+reissue

     counted-loop capture (body records nothing per trip):
       before = max(deliver_body, exec)
       record = deliver_record + RECORD_OVERHEAD         ; hoisted pass

     re-record body, execution-bound (exec >= deliver_record):
       before = exec + RECORD_OVERHEAD   ; the record pass executes the
                                         ; payload at its interlocked
                                         ; pace and exposes the record
                                         ; engine's per-pass overhead
       record = RECORD_OVERHEAD          ; the hoisted pass's delivery
                                         ; hides behind the loop's own
                                         ; execution backlog (delivery
                                         ; is concurrent with playback
                                         ; execution, rvtt-cost.md)

     re-record body, delivery-bound (exec < deliver_record):
       before = deliver_record           ; original calibration restored:
                                         ; execution and the record
                                         ; overhead absorb into the
                                         ; per-word delivery slack
       record = deliver_record + RECORD_OVERHEAD
       (execution-saturation `hidden' term may set before = after)

     benefit = trips * (before - after) - record         ; >= MIN_BENEFIT

   A dependence edge whose producer carries no audited result-latency
   fact makes the payload unpriceable: named refusal
   replay-reissue-latency-unproved (the -mtt-tensix-replay-hoist-
   min-benefit= override cannot force an unpriceable payload).  No
   operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates.

   Planner-emitted macro launches: an SFPLOADMACRO pattern is
   attribute-opaque (descriptor-dependent effects), but when the macro
   planner itself emitted the launch it recorded the launch's
   issue-plane effect interface, derived from the descriptor it
   synthesized (rvtt-effects.h, planner emission records; provenance in
   rvtt-cost.md).  The pricing consults the record: issue is never
   operand-gated (no read deps), writes are full-lane (no write-side
   dependence edge from the record-carried insn itself), and each
   written LREG carries the launch's settle distance as its audited
   result latency.  A launch without a matching record -- user-written
   raw words never have one, and derivation fails closed on CC-writing
   calendars -- keeps the refusal above by the same name.  */

/* Interlocked issue-slot count of the payload span of BLOCK:
   dependence-tracked with the audited xtt_result_latency facts
   (intervening slots absorb latency -- the interlock scheduler's own
   accounting), plus the architectural next-slot acceptance stall fact
   (xtt_next_slot_stall: the next instruction issues one slot late).
   Dependence follows the scheduler's definition: a consumer references
   a producer's SFPU destination by reading it or by lane-predicated
   writing (disabled lanes preserve prior contents).  Returns -1 when a
   consumed producer carries no audited latency fact.  */

HOST_WIDE_INT
exec_interlocked_slots (replay_block const &block, replay_span span)
{
  /* QSR carries no audited latency facts (the simulator refuses these
     opcode semantics, rvtt-cost.md): the whole target is unpriceable,
     matching the interlock scheduler's target-level refusal.  */
  if (TARGET_XTT_TENSIX_QSR)
    return -1;
  /* The 16-register ready[] scoreboard is the timing engine's; this
     walker owns only the IR-side effect extraction, dumps and
     refusals (verdict identity proven by the stage-A shadow over a
     full corpus -fchecking leg before the local scoreboard retired).  */
  rvtt_timing::interlock_sim sim;

  for (auto pos = block.data () + span.begin,
	 end = block.data () + span.end; pos != end; ++pos)
    {
      if (pos->empty)
	continue;
      xtt_effect_set e = rvtt_insn_effects (pos->insn);
      bool planner_record = false;
      if (e.opaque && rvtt_planner_launch_effects (pos->insn, &e))
	{
	  /* Planner-emitted macro launch: the planner derived this
	     launch's issue-plane effect interface from the descriptor
	     it synthesized (planner emission record, rvtt-effects.h).
	     Its writes are full-lane by the record contract (non-CC
	     calendar, all-lanes ambient proof), so a record-carried
	     insn contributes no write-side dependence edge -- only
	     reads and lane-predicated writes are dependences under the
	     scheduler's definition.  */
	  planner_record = true;
	  if (dump_file)
	    fprintf (dump_file, "  planner-derived launch effects: insn %d"
		     " writes 0x%x settle %d\n", INSN_UID (pos->insn),
		     e.lreg_write, e.result_latency);
	}
      if (e.opaque)
	{
	  if (dump_file)
	    fprintf (dump_file, "  reissue-unproved: payload insn %d is"
		     " effect-opaque\n", INSN_UID (pos->insn));
	  return -1;
	}
      rvtt_timing::issue_op op;
      op.deps = (e.lreg_read
		 | (planner_record ? 0 : e.lreg_write)) & 0xFFFF;
      op.writes = e.lreg_write;
      op.words = get_attr_length (pos->insn) / 4;
      op.lat = e.result_latency;
      op.next_slot_stall = e.next_slot_stall;
      if (!sim.step (op))
	{
	  if (dump_file)
	    fprintf (dump_file, "  reissue-unproved edge: consumer insn %d"
		     " (deps 0x%x) of an unaudited producer (mask 0x%x)\n",
		     INSN_UID (pos->insn), op.deps, sim.unproved_mask ());
	  return -1;
	}
    }
  return sim.slots ();
}

/* Delivered instruction words of the span (multi-word instructions count
   each word; zero-length ghosts none).  */

HOST_WIDE_INT
delivered_words (replay_block const &block, replay_span span)
{
  HOST_WIDE_INT words = 0;
  for (auto pos = block.data () + span.begin,
	 end = block.data () + span.end; pos != end; ++pos)
    if (!pos->empty)
      words += get_attr_length (pos->insn) / 4;
  return words;
}

/* Longest run of consecutive clones of SEQ (in BLOCK) whose playback
   launches will be contiguous in the FINAL instruction stream: no
   code-emitting instruction remains between one clone's last payload insn
   and the next clone's first.  Non-delivering separators are debug
   insns/notes, USE/CLOBBER markers, and recognized zero-length ghosts.  A
   typed Dst-counter increment (CODE_FOR_rvtt_ttincrwc, the class the
   counted-loop scan above already types) is additionally discounted when
   the Dst auto-increment pass is enabled: that pass runs after replay
   formation and absorbs exactly these per-row separators around replay
   launches into an owned address-modifier configuration (see the pass
   ordering contract in rvtt-passes.def), leaving the launches contiguous.
   When that pass later refuses the absorption this scan over-counts the
   run and the hoist over-refuses -- a byte-identical refusal, never a
   regression.  Every other insn is a delivered word and breaks the run.
   Purely structural: no operation identity, opcode calendar, coefficient
   value, or instruction-word fingerprint participates.  */

unsigned
max_contiguous_launch_run (replay_sequence const &seq,
			   replay_block const &block)
{
  unsigned max_run = 1, run = 1;
  for (size_t ix = 1; ix < seq.clones.size (); ++ix)
    {
      rtx_insn *from = block[seq.clones[ix - 1].end - 1].insn;
      rtx_insn *to = block[seq.clones[ix].begin].insn;
      bool contiguous = true;
      for (rtx_insn *insn = NEXT_INSN (from); insn != to;
	   insn = NEXT_INSN (insn))
	{
	  if (!insn)
	    {
	      /* Clones are within one block; a broken chain refuses the
	         discount conservatively toward "separated" (fires are
	         gated by the benefit model as before).  */
	      contiguous = false;
	      break;
	    }
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx pattern = PATTERN (insn);
	  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
	    continue;
	  if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0)
	    {
	      if (!get_attr_length (insn))
		/* Zero-length ghost: no delivered word.  */
		continue;
	      if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
		  && riscv_tt_opt_dst_autoincr > 0)
		/* Typed per-row Dst increment the later auto-increment
		   pass absorbs around replay launches.  */
		continue;
	    }
	  contiguous = false;
	  break;
	}
      run = contiguous ? run + 1 : 1;
      if (run > max_run)
	max_run = run;
    }
  return max_run;
}

/* Playback launches THIS PASS emitted this function: their
   recorded slot content is the pass's own audited payload, so the
   record-hoist loop replay-preservation walk may admit them where a
   user-authored launch (unknowable recorded content) refuses.  */
std::vector<rtx_insn *> formed_playback_launches;

/* Lane IM: commit the partial playback launch for an admitted widened
   window's trailing prefix run (see the window-sizing block comment
   above window_sizing_clones_exact_p): one REPLAY launch whose Count is
   the trim's word count (below the recorded length -- the ISA
   prefix-launch semantics), emitted at the run's own stream position,
   then the run's insns deleted exactly like a full clone's.  The span
   is appended to SEQ.clones so active_invalidate retires every
   candidate overlapping the consumed positions.  */

void
window_sizing_commit_trim (replay_sequence &seq, replay_block &block,
			   unsigned replay_start, unsigned trim_len,
			   unsigned trim_end)
{
  unsigned begin = seq.clones.back ().end;
  rtx replay = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (trim_len),
     rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
     const0_rtx, const0_rtx);
  rtx_insn *launch = emit_insn_after (replay, block[trim_end - 1].insn);
  formed_playback_launches.push_back (launch);
  if (dump_file)
    {
      fprintf (dump_file,
	       "window-sizing: trailing %u-word run [%u,%u) is a"
	       " recorded-window prefix; launching [%u,+%u) as a partial"
	       " playback\n",
	       trim_len, begin, trim_end, replay_start, trim_len);
      fprintf (dump_file, "Replaying (partial) ");
      dump_insn_slim (dump_file, launch);
    }
  for (auto pos = block.data () + begin, end = block.data () + trim_end;
       pos != end; ++pos)
    SET_INSN_DELETED (pos->insn);
  seq.clones.emplace_back (begin, trim_end);
}

/* Remove or adjust those sequences that are invalidated by having used SEQ.
   (SEQ itself auto-invalidates).  */

bool
active_invalidate (replay_active &active, replay_sequence *seq,
		   unsigned max_length)
{
  auto write = active.begin ();
  auto end = active.end ();
  for (auto pos = write; pos != end; ++pos)
    {
      auto ptr = *pos;

      if (ptr->length > max_length)
	break;

      if (ptr == seq)
	continue;

      auto clone_write = ptr->clones.begin ();
      auto clone_end = ptr->clones.end ();
      auto seq_pos = seq->clones.begin ();
      auto seq_end = seq->clones.end ();

      for (auto clone_pos = clone_write; clone_pos != clone_end; ++clone_pos)
	{
	  while (seq_pos != seq_end
		 && seq_pos->end <= clone_pos->begin)
	    ++seq_pos;

	  if (seq_pos != seq_end && seq_pos->begin < clone_pos->end)
	    continue;

	  *clone_write = *clone_pos;
	  ++clone_write;
	}
      ptr->clones.erase (clone_write, clone_end);
      if (ptr->clones.size () < 2)
	continue;

      /* Keep this one */
      *write = *pos;
      ++write;

      if (dump_file)
	{
	  fprintf (dump_file, "Sequence [%u,%u) length %u, %u instances\n",
		   ptr->clones.front ().begin,
		   ptr->clones.front ().end,
		   ptr->length, unsigned (ptr->clones.size ()));
	}
    }

  active.erase (write, end);

  if (dump_file)
    fprintf (dump_file, "%u candidates\n\n", unsigned (active.size ()));

  return !active.empty ();
}

/* Subtract the PERSISTENT slots (already consumed by hoisted or
   canonicalized records) from the free spans in BASE and return the
   remaining sub-spans of at least MIN_SEQUENCE slots, as [start,
   +length) pairs sorted by decreasing length.  BASE spans are [start,
   +length) too (transform's post-census form).  */

std::vector<replay_span>
available_replay_spans (std::vector<replay_span> const &base,
			std::vector<bool> const &persistent)
{
  std::vector<replay_span> result;
  for (auto const &slot : base)
    {
      unsigned end = slot.begin + slot.end;
      for (unsigned pos = slot.begin; pos != end;)
	{
	  while (pos != end && persistent[pos])
	    ++pos;
	  unsigned begin = pos;
	  while (pos != end && !persistent[pos])
	    ++pos;
	  if (pos - begin >= MIN_SEQUENCE)
	    result.emplace_back (begin, pos - begin);
	}
    }
  std::sort (result.begin (), result.end (),
	     [] (replay_span const a, replay_span const b)
	     {
	       return a.end > b.end
		 || (a.end == b.end && a.begin < b.begin);
	     });
  return result;
}
