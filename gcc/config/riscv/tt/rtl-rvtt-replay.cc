/* Pass to generate the tensix replay insn
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

/* Each Tensix thread has a REPLAY buffer of riscv_tt_replay_size
   instruction slots (REPLAY.md).  A TTREPLAY word with load=1 records the
   next LEN delivered words into slots [IDX, IDX+LEN), executing them as
   it records (exec=1) or not (exec=0); a TTREPLAY word with load=0
   replays the stored words at its own position in the stream.  Replacing
   an N-word repeat with one launch saves N-1 words pushed by the RISC-V
   core, at the cost of one recording and N buffer slots.  Profitability
   is decided by the delivery-cost model (rvtt-delivery-cost.h,
   rvtt-cost.md) and, where reissue stalls matter, by rvtt-timing.h.

   The pass runs after reload, before the Dst auto-increment and MOP
   formation passes.  With -mtt-tensix-optimize-post-autoincr-window it
   instead runs once after the auto-increment pass (see reform_mode).

   transform () works in these steps:

   1. Find the slots used by explicit user TTREPLAY words; formation
      uses only the remaining slots.  A raw asm REPLAY word disables
      formation in the function.
   2. Hoist the body of a counted single-block loop into a no-exec
      recording in the preheader, with one launch per iteration.
   3. Rewrite families of rows that differ only in immediate loads and
      register names into identical words (the crf_* code).
   4. In each basic block, find repeated instruction sequences.  This is
      O(N^2): candidates grow one insn at a time from length-1 seeds.
   5. Greedily allocate the most profitable candidate to the best-fitting
      free span until nothing fits.  The first instance records with
      exec=1 and the others become launches, or the recording is hoisted
      to a preheader.
   6. Fully unroll loops that contain only launches, and turn runs that
      match a recording up to a register renaming into launches.
   7. Undo hoisted recordings that ended up in an unsafe place.

   Instances must be identical words (rvtt_dcost_replay_word_equal_p) of
   the same generation, so that two equal insns whose scalar inputs may
   differ are never merged, and a sequence never crosses a MUST_END insn.
   Every step that cannot prove its conditions leaves the code unchanged
   and reports a named refusal (rvtt-refuse.h).

   Repeated-sequence discovery is a form of procedural abstraction
   (Fraser, Myers and Wendt, SIGPLAN Symposium on Compiler Construction
   1984).  */

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

// Look for repeated sequences of Tensix insns, and use REPLAy/ instruction for
// them.  Finding the sequences is O(N^2), and allocating them to the replay
// buffer is the knapsack problem.  We aim for 'good enough'

// 1) Only consider single BBs.  This works well for unrolled loops anyway.
//    Looking accross BBs would require considering the dominator graph, and
//    better live value computation for synthesized insns
// 2) If sequence A's occurrences are all before sequence B's, B could reuse
//    the replay buffer locations.  We do not consider this.
// 3) If the user has explicitly used replay, we use the parts of the replay
//    buffer that have not used (anywhere in the function).
// 4) We use all of a discovered sequence (or none of it).  We could of course
//    use the first N insns, if that is profitable and no room for the whole sequence.

// FIXME: PR 36496 We terminate sequences if they meet a non TENSIX insn. This isn't
// always necessary.  The non-Tensix insn could be hoisted upwards, provided it
// doesn't affect the generation of any insn hoisted past. This may improve
// synthesized insns where opcode or address computation is in the middle of a sequence.

// Minimum acceptable sequence length, matching
// XTT_REPLAY_LOOP_UNROLL_MIN_WORDS (rvtt-cost.md): shorter rows cannot
// pay for a record and launch.
constexpr unsigned MIN_SEQUENCE = 4;

/* True when the pass runs after the Dst auto-increment pass
   (-mtt-tensix-optimize-post-autoincr-window).  The early instance then
   does nothing and pass_rvtt_replay_reform runs transform () once over
   the folded stream.  The auto-increment pass removes the TTINCRWC
   separators between rows, so repeated row bodies become identical only
   after it, and a single allocation after it can weigh all candidates
   against the one buffer.  Nothing is lost by waiting: the fold only
   deletes separators and retargets address modifiers of the rows they
   separated, so any run capturable before it is capturable after.

   Replacement preserves the delivered stream: an in-place recording
   executes its words as it records, a hoisted no-exec recording executes
   nothing, and each other instance becomes a launch that replays exactly
   its words at the same position.  So the RWC and ADDR_MOD walk and all
   delay-shadow gaps are preserved (gaps can only grow).

   A Dst access retargeted by the auto-increment pass
   (rvtt_dst_autoincr_carried_access_p) depends on the number of times
   it executes, which must equal the number of replaced rows;
   reform_carried_launch_arithmetic_ok checks this.  The two
   transformations that do not replay a site's own words, the
   register-renamed launch conversion and the first-trip peel, refuse
   payloads containing such accesses.  */
static bool reform_mode = false;

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

/* Set of sequences, by construction in increasing length and, within
   each length, by starting insn position.  */
using replay_list = std::vector<replay_sequence>;

/* Map from hash to set of sequences, used to find matches during
   construction */
using replay_map = std::map<unsigned, std::vector<unsigned>>;

/* It is cheaper to remove/copy pointers than sequence info itself.  */
using replay_active = std::vector<replay_sequence *>;

enum REPLAY_TYPE {REPLAY_none, REPLAY_playback, REPLAY_fixed_capture,
		  REPLAY_variable_capture};

/* Classify INSN as a typed TTREPLAY word.  Returns REPLAY_none unless
   INSN is the typed replay pattern; otherwise fills SPAN with the raw
   operands -- begin = the start slot, end = the LENGTH (not a half-open
   bound; 0 when the length is not a compile-time constant) -- and
   returns REPLAY_playback (load operand 0), or REPLAY_fixed_capture vs
   REPLAY_variable_capture for a recording with a constant vs unknown
   length.  */

static REPLAY_TYPE
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

static bool
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
   compares structurally.  */

static bool
replay_ignore_rtx_p (const_rtx *a, const_rtx *b, rtx *na, rtx *nb)
{
  if (GET_CODE (*a) != GET_CODE (*b))
    return false;

  if (GET_CODE (*a) == MEM)
    {
      if (GET_MODE (*a) != SImode)
	// This is (probably) broken code attempting to spill/fill an
	// LReg
	return false;
    }
  else if (GET_CODE (*a) != CLOBBER
	   && GET_CODE (*a) != SCRATCH)
    return false;

  gcc_checking_assert (GET_MODE (*a) == GET_MODE (*b));

  *na = *nb = nullptr;
  return true;
}

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
      /* Pattern equality, ignoring MEM and scratch operands.  */
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

static unsigned
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

static void
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

/* Return true if capturing SPAN of BLOCK keeps each value-bank
   instruction together with its companion-bank results.  STICKY is
   rvtt_shadow_coupling_possible for the function: when index tracking
   may be enabled, every value-bank move also moves the companion bank,
   so the span may contain only instructions whose companion effects are
   known.  Refuse with shadow-state-unproved for an instruction with
   unknown effects, and with multiresult-companion-split for an
   instruction that writes the value bank without companion results or
   when the span boundary separates a multi-result instruction from its
   zero-length companion markers, which carry its fixed-LREG
   dataflow.  */

static bool
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
	  /* Companion markers just outside either end of the span must
	     not be separated from their instruction.  */
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

/* Return true if SPAN of BLOCK contains a Dst access that the
   auto-increment pass retargeted to the compiler-owned scratch modifier.
   Such accesses exist only in reform_mode.  */

static bool
payload_contains_carried_p (replay_block const &block, replay_span span)
{
  for (unsigned ix = span.begin; ix != span.end; ++ix)
    if (!block[ix].empty
	&& rvtt_dst_autoincr_carried_access_p (block[ix].insn))
      return true;
  return false;
}

/* Check that the clones of SEQ in BLOCK are non-overlapping, ascending
   and word-for-word copies of the recorded window, pairing their
   non-empty insns in order.  Only then does each launch deliver the same
   words as the site it replaces.  REQUIRE_INSN_CODE additionally
   requires each member to be a plain INSN.  Callers turn the result into
   their own refusal.  */

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

  /* The k-th non-empty word of every clone equals the k-th recorded
     word.  */
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

/* In reform_mode, check that SEQ, whose payload contains a carried Dst
   access, delivers exactly one payload execution per replaced site, so
   the access executes as often as the auto-increment pass assumed.
   Every replacement path delivers one execution per clone, provided the
   clones do not overlap and are exact copies; verify that and refuse
   with post-autoincr-window-launch-arithmetic-skew otherwise.  */

static bool
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

/* Window sizing for hoisted records
   (-mtt-tensix-optimize-replay-window-sizing).

   pick_replay maximizes the in-block saving, (clones - 1) * (length - 1),
   which favours short windows with many instances.  Once a recording is
   hoisted out of the loop, though, the per-iteration cost is just the
   launch words, so the widest exact window that fits the free slots is
   best.  A REPLAY launch of COUNT words plays
   ReplayBuffer[(Index + i) % 32] for i in [0, COUNT) (REPLAY.md), so a
   leftover tail that matches a prefix of the window can be delivered by
   one partial launch, e.g. three launches of 28 words and one of 13.

   After the hoist of the original pick has been accepted, the helpers
   below look for a wider candidate with the same first instance:
   window_sizing_clones_exact_p checks its clones, window_sizing_prefix_trim
   measures the trailing prefix run, and window_sizing_widen picks the
   candidate with the fewest delivered words per iteration.  The caller
   then reruns the full hoist checks on the result and falls back to the
   original pick if they fail.  The partial launch replays the same words
   at the same position as the code it replaces.  Widening is not done
   in reform_mode (window-sizing-reform-composition-unaudited) or for
   in-block picks.  */

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

  /* Same check as the reform audit, but requiring plain INSNs.  */
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

/* Return the length of the longest run after the last clone of CAND in
   BLOCK that is a prefix of the recorded window, stopping one word short
   of a full extra clone and never crossing a must_end word or the block
   end.  Store the block position just past the run in *TRIM_END_OUT.
   Return 0 if there is no such run.  */

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
      /* Like sequence growth, stop at a must_end word.  */
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

/* Among the ACTIVE candidates longer than SEQ that start at the same
   position, fit in FREE_SPAN slots and pass the companion (under
   STICKY) and clone-exactness checks, return the one whose covered span
   needs strictly the fewest delivered words per iteration (launches,
   one partial launch for the trailing trim, and words left inline).
   Return its trim through *TRIM_LEN_OUT and *TRIM_END_OUT.  Return
   null, after a named refusal, to keep SEQ.  */

static replay_sequence *
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

      /* Delivered words per iteration over the covered span for both
	 candidates: launches, a partial launch, and the words left
	 inline.  The current pick gets no partial launch.  */
      unsigned ne = 0;
      for (unsigned ix = begin; ix != cov_end; ++ix)
	if (!block[ix].empty)
	  ++ne;
      unsigned covered = cand->length * unsigned (cand->clones.size ()) + trim;
      gcc_checking_assert (ne >= covered);
      /* See window_trip_issue_words in rvtt-delivery-cost-core.h.  */
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

/* Return the candidate from ACTIVE with the greatest word saving,
   (clones - 1) * (length - 1) less one when the no-exec-while-record
   workaround needs an extra launch, whose length fits LIMIT slots and
   whose first clone passes span_companion_sound_p (cached in
   companion_ok; STICKY as there).  Ties prefer the longer window.
   Return null if none qualifies.  */

static replay_sequence *
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

static unsigned
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
rvtt_fixed_replay_rtx_p (const_rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case SCRATCH:
      return true;

    case REG:
      return SFPU_REG_P (REGNO (x));

    case SET:
      return rvtt_fixed_replay_rtx_p (SET_DEST (x))
	&& rvtt_fixed_replay_rtx_p (SET_SRC (x));

    case CLOBBER:
    case USE:
      return rvtt_fixed_replay_rtx_p (XEXP (x, 0));

    case PARALLEL:
    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (int ix = XVECLEN (x, 0); ix--;)
	if (!rvtt_fixed_replay_rtx_p (XVECEXP (x, 0, ix)))
	  return false;
      return true;

    default:
      return false;
    }
}

/* Replay-hoist profitability.  Hoisting replaces a recording in the loop
   body, which also executes the payload, with one no-exec recording in
   the preheader plus one launch per iteration.  The saving per iteration
   is only the difference between delivering the recording and replaying
   it, so the reissue must be priced with its real cost: LEN slots plus
   the RAW interlock stalls plus a per-launch turnaround.  Per iteration,
   in centislots (constants and their derivation in rvtt-cost.md):

     exec   = exec_interlocked_slots (payload) * SLOT
     after  = max (PUSH, exec + TURNAROUND)

     counted-loop capture (nothing recorded in the body):
       before = max (deliver_body, exec)
       record = deliver_record + RECORD_OVERHEAD

     re-recording body, execution-bound (exec >= deliver_record):
       before = exec + RECORD_OVERHEAD
       record = RECORD_OVERHEAD    (delivery overlaps loop execution)

     re-recording body, delivery-bound (exec < deliver_record):
       before = deliver_record
       record = deliver_record + RECORD_OVERHEAD

     benefit = trips * (before - after) - record

   and the hoist is done only if benefit >= XTT_REPLAY_HOIST_MIN_BENEFIT
   (-mtt-tensix-replay-hoist-min-benefit=).  In the delivery-bound case,
   if a run of LAUNCH_RUN contiguous launches of the same buffer has

     launch_run * (exec - PUSH) >= deliver_record

   the recording is hidden behind their execution, so before = after and
   the hoist is refused.  TRIPS must be provable (rvtt_loop_trips).

   A payload instruction with no known result latency makes the payload
   unpriceable (replay-reissue-latency-unproved).  An SFPLOADMACRO launch
   emitted by the macro planner is priced from the effect record the
   planner attached to it (rvtt-effects.h): it reads no operands, writes
   all lanes, and each written LREG has the launch's settle distance as
   its latency.  Other SFPLOADMACRO words are refused.  */

/* Return the number of issue slots needed for PAYLOAD of BLOCK including
   interlock stalls, using the xtt_result_latency facts (intervening
   slots absorb latency) and xtt_next_slot_stall.  As in the interlock
   scheduler, a consumer depends on a producer if it reads the producer's
   destination or writes it under lane predication (disabled lanes keep
   the old value).  Return -1 if a consumed producer has no known
   latency.  */

static HOST_WIDE_INT
exec_interlocked_slots (replay_block const &block, replay_span span)
{
  /* No latency facts are known for QSR.  */
  if (TARGET_XTT_TENSIX_QSR)
    return -1;
  /* The scoreboard is in rvtt-timing; this function only extracts
     effects from the insns.  */
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
	  /* Macro launch emitted by the planner, which records its
	     effects (rvtt-effects.h).  It writes all lanes, so it adds no
	     write-side dependence.  */
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

/* Return the number of instruction words delivered for SPAN of BLOCK
   (multi-word instructions count each word, zero-length insns none).  */

static HOST_WIDE_INT
delivered_words (replay_block const &block, replay_span span)
{
  HOST_WIDE_INT words = 0;
  for (auto pos = block.data () + span.begin,
	 end = block.data () + span.end; pos != end; ++pos)
    if (!pos->empty)
      words += get_attr_length (pos->insn) / 4;
  return words;
}

/* Return true if recording PAYLOAD of BLOCK once in PREHEADER instead
   of in the body of LOOP is profitable under the model above.
   BODY_RERECORDS is true if the body records the payload every
   iteration (in-block formation) and false for a counted-loop capture;
   LAUNCH_RUN is the longest run of contiguous launches of the same
   buffer.  Refuse by name if unprofitable or unpriceable.  */

static bool
hoist_profitable_p (class loop *loop, basic_block preheader,
		    replay_block const &block, replay_span payload,
		    bool body_rerecords, unsigned launch_run)
{
  bool record_hoist_mode
    = body_rerecords && riscv_tt_opt_replay_record_hoist > 0;
  /* The record-hoist model ignores payload execution, which is the same
     with and without the hoist, and prices only delivered words.  Under
     the completion guard that is not valid: an in-loop recording
     executes the first payload while a hoisted no-exec recording does
     not, and the first launch waits for the recording.  So the guard
     uses the general model.  */
  bool record_completion_model
    = record_hoist_mode
      && riscv_tt_replay_hoist_completion_guard > 0;
  uint64_t niter;
  bool trips_proven = rvtt_loop_trips (loop, preheader, &niter);
  /* An unknown trip count is accepted for the record-hoist model: the
     recording is placed in the dedicated preheader of a single-block
     loop, so it executes only if the body executes at least once.  The
     pricing below requires a benefit at 2 iterations.  */
  bool runtime_trips = record_hoist_mode && !trips_proven;
  if (!trips_proven && !runtime_trips)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: loop %d trip count is not provably"
		 " constant\n", loop->num);
      return false;
    }

  HOST_WIDE_INT trips = trips_proven ? (HOST_WIDE_INT) niter : 0;
  if (trips_proven && trips < 2)
    {
      if (dump_file)
	{
	  fprintf (dump_file, "Not hoisting: loop %d runs %ld time(s)\n",
		   loop->num, (long) trips);
	  if (record_hoist_mode)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_TRIP_COUNT_UNPROVEN, dump_file,
			 "record-hoist refused:"
			 " record-hoist-trip-count-unproven\n");
	}
      return false;
    }

  HOST_WIDE_INT words = delivered_words (block, payload);
  /* The record-hoist model does not need the reissue latencies: the
     window has at least two clones, so the unhoisted code already
     replays the same words through launches, and turning the first
     clone into one more launch adds no new reissue hazard.  The check
     remains for the default model, which uses the estimate, and for
     QSR.  */
  bool reissue_gate_discharged
    = record_hoist_mode && !record_completion_model
      && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH);
  HOST_WIDE_INT eslots = 0;
  if (!reissue_gate_discharged)
    {
      eslots = exec_interlocked_slots (block, payload);
      if (eslots < 0)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED, dump_file,
			   "Not hoisting: replay-reissue-latency-unproved: a"
			   " consumed payload producer carries no"
			   " audited result"
			   " latency (loop %d, %ld words)\n",
			   loop->num, (long) words);
	      if (record_hoist_mode)
		rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED,
			     dump_file,
			     "record-hoist refused:"
			     " replay-reissue-latency-unproved\n");
	    }
	  return false;
	}
    }

  HOST_WIDE_INT min_benefit = rvtt_dcost_replay_hoist_min_benefit ();

  /* See replay_pricing in rvtt-delivery-cost-core.h.  */
  rvtt_delivery_cost::replay_shape shape
    = !body_rerecords
      ? rvtt_delivery_cost::SHAPE_COUNTED
      : rvtt_delivery_cost::rerecord_shape
	  (riscv_tt_opt_replay_record_hoist > 0,
	   riscv_tt_replay_hoist_completion_guard > 0,
	   !runtime_trips);
  rvtt_delivery_cost::replay_price price = rvtt_dcost_replay_pricing
    (shape, trips, words, eslots, launch_run,
     riscv_tt_replay_hoist_completion_guard > 0, min_benefit);

  /* Record-hoist pricing (-mtt-tensix-optimize-replay-record-hoist,
     re-recording bodies only).  hoist_preheader has checked that every
     payload word has a fixed encoding, so both versions execute the same
     words at the same positions and the no-exec recording executes
     nothing.  The difference is therefore only delivery: the loop body
     delivers the capture word and the payload every iteration where the
     hoisted version delivers one launch word, at the cost of delivering
     the recording once in the preheader.  */
  if (body_rerecords && riscv_tt_opt_replay_record_hoist > 0
      && !record_completion_model)
    {
      /* The hoist turns the first clone into one more launch per
	 iteration; charge it at the turnaround cost.  On serially
	 dependent windows this undercharges, which MIN_BENEFIT
	 covers.  */
      HOST_WIDE_INT record_once = price.record_once;
      HOST_WIDE_INT per_trip = price.per_trip;
      if (runtime_trips)
	{
	  /* Unknown trip count (at least 1, see above).  Each iteration
	     saves per_trip, so the worst case is a single iteration, which
	     loses record_once - per_trip.  Accept if the benefit at 2
	     iterations reaches MIN_BENEFIT.  */
	  HOST_WIDE_INT benefit2 = price.benefit;
	  HOST_WIDE_INT exposure = price.exposure;
	  if (dump_file)
	    fprintf (dump_file,
		     "Record-hoist runtime-trip pricing (loop %d): words"
		     " %ld, per_trip %ld, record_once %ld, 2-trip benefit"
		     " %ld (min %ld), single-trip exposure %ld\n",
		     loop->num, (long) words, (long) per_trip,
		     (long) record_once, (long) benefit2,
		     (long) min_benefit, (long) exposure);
	  if (!price.profitable)
	    {
	      rvtt_refuse (RVTT_REF_RECORD_HOIST_RUNTIME_TRIPS_BREAK_EVEN,
			   dump_file,
			   "record-hoist refused:"
			   " record-hoist-runtime-trips-break-even: 2-trip"
			   " benefit %ld < %ld\n",
			   (long) benefit2, (long) min_benefit);
	      return false;
	    }
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist: runtime-trip re-record window admitted"
		     " (structural trips>=1, words %ld, 2-trip benefit %ld,"
		     " single-trip exposure %ld)\n",
		     (long) words, (long) benefit2, (long) exposure);
	  return true;
	}
      HOST_WIDE_INT benefit = price.benefit;
      if (dump_file)
	fprintf (dump_file,
		 "Record-hoist pricing (loop %d): trips %ld, words %ld,"
		 " deliver_body %ld/trip, boundary %d/trip, record_once %ld,"
		 " benefit %ld (min %ld)\n",
		 loop->num, (long) trips, (long) words,
		 (long) price.deliver_body, XTT_REPLAY_COST_TURNAROUND_X100,
		 (long) record_once, (long) benefit,
		 (long) min_benefit);
      if (!price.profitable)
	{
	  rvtt_refuse (RVTT_REF_RECORD_HOIST_BENEFIT, dump_file,
		       "Not hoisting: record-hoist-benefit: modeled issue-side"
		       " benefit %ld < %ld\n",
		       (long) benefit, (long) min_benefit);
	  return false;
	}
      if (dump_file)
	fprintf (dump_file,
		 "record-hoist: invariant re-record window admitted"
		 " (trips %ld, words %ld, benefit %ld)\n",
		 (long) trips, (long) words, (long) benefit);
      return true;
    }
  // Re-recording bodies are priced according to which resource limits
  // the in-loop recording (see the model above).  The completion guard
  // charges the hoisted recording's full delivery even when execution
  // bound: a caller that measures completion after a final drain
  // cannot assume the recording overlaps execution, since it must finish
  // before the first launch.
  rvtt_timing::hoist_costs costs;
  costs.push = XTT_REPLAY_COST_RISC_PUSH_X100;
  costs.slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
  costs.turnaround = XTT_REPLAY_COST_TURNAROUND_X100;
  costs.record_overhead = XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
  rvtt_timing::hoist_pricing pricing
    = !body_rerecords
      ? rvtt_timing::counted_hoist_price (costs, trips, words, eslots)
      : rvtt_timing::rerecord_hoist_price
	  (costs, trips, words, eslots, launch_run,
	   riscv_tt_replay_hoist_completion_guard > 0);
  bool exec_bound_rerecord = body_rerecords && pricing.exec_bound;
  HOST_WIDE_INT after = pricing.after;
  HOST_WIDE_INT before = pricing.before;
  HOST_WIDE_INT record = pricing.record;
  HOST_WIDE_INT benefit = pricing.benefit;
  /* PRICING and PRICE compute the same terms independently.  */
  gcc_checking_assert (pricing.exec == price.exec
		       && pricing.deliver_body == price.deliver_body
		       && pricing.deliver_record == price.deliver_record);
  if (dump_file && exec_bound_rerecord
      && riscv_tt_replay_hoist_completion_guard > 0)
    fprintf (dump_file,
	     "Replay completion guard: execution-bound re-record"
	     " charges hoisted delivery %ld (record cost %ld)\n",
	     (long) price.deliver_record, (long) record);
  if (dump_file && body_rerecords && !exec_bound_rerecord
      && pricing.hidden)
    fprintf (dump_file,
	     "Record delivery hidden: contiguous launch run %u exec"
	     " surplus %ld >= record delivery %ld\n",
	     launch_run, (long) pricing.surplus, (long) price.deliver_record);

  if (dump_file)
    fprintf (dump_file,
	     "Hoist pricing (loop %d): trips %ld, words %ld,"
	     " exec_ilk %ld slots%s, deliver_body %ld,"
	     " deliver_record %ld, record %ld, before %ld, after %ld,"
	     " benefit %ld (min %ld)\n",
	     loop->num, (long) trips, (long) words, (long) eslots,
	     !body_rerecords ? ""
	     : exec_bound_rerecord ? " [re-record body, execution-bound]"
	     : " [re-record body, delivery-bound]",
	     (long) price.deliver_body, (long) price.deliver_record,
	     (long) record, (long) before, (long) after, (long) benefit,
	     (long) min_benefit);

  /* With the completion guard, an unknown trip count is priced as zero
     trips above, which proves nothing.  Refuse, naming the real
     reason.  */
  if (runtime_trips && record_completion_model)
    {
      rvtt_refuse (RVTT_REF_RECORD_HOIST_COMPLETION_RUNTIME_TRIPS_UNPROVEN,
		   dump_file,
		   "Not hoisting:"
		   " record-hoist-completion-runtime-trips-unproven:"
		   " completion-accurate shared model requires a proven"
		   " trip count"
		   " (loop %d)\n",
		   loop->num);
      return false;
    }

  if (benefit < min_benefit)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: modeled benefit %ld < %ld\n",
		 (long) benefit, (long) min_benefit);
      return false;
    }

  if (dump_file)
    fprintf (dump_file, "Hoist profitable: modeled benefit %ld >= %ld\n",
	     (long) benefit, (long) min_benefit);
  return true;
}

/* Return the longest run of consecutive clones of SEQ in BLOCK whose
   launches will be contiguous in the final instruction stream, i.e. with
   no word-emitting insn between one clone and the next.  Debug insns,
   notes, USE/CLOBBER and zero-length insns emit nothing.  When the Dst
   auto-increment pass is enabled, TTINCRWC insns are skipped too, since
   that pass (which runs later) absorbs them; if it does not, the run is
   overestimated and the hoist is wrongly refused, which is safe.  */

static unsigned
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
	      /* Clones are in one block; treat a broken chain as
		 separated.  */
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
		/* Zero-length insn: no delivered word.  */
		continue;
	      if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
		  && riscv_tt_opt_dst_autoincr > 0)
		/* Absorbed by the later auto-increment pass.  */
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

/* Launches emitted by this pass in the current function.  Their recorded
   content is known, so the loop replay-preservation walk can accept them,
   unlike user launches.  */
static std::vector<rtx_insn *> formed_playback_launches;

/* Replace the trailing prefix run of a widened window (see the comment
   before window_sizing_clones_exact_p) with one launch of the run's
   length, at the run's position, and delete the run's insns as for a
   full clone.  Append the span to SEQ.clones so that active_invalidate
   drops overlapping candidates.  */

static void
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

/* Return LOOP's dedicated preheader: the unique block outside the loop
   with an edge into the header, provided that entry edge is normal and
   the block's only successor is the header (so an insn placed at its
   end executes exactly once per loop entry).  Null when the loop has
   several entry blocks or the candidate is shared with other code.  */

static basic_block
dedicated_loop_preheader (class loop *loop)
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

// Return true if the volatile store PAT might write to the instruction
// FIFO, and so deliver any word, including a REPLAY that overwrites
// hoisted slots.  Stores to named objects other than __instrn_buffer, to
// the stack, or to constant addresses outside the FIFO range cannot.
static bool
volatile_store_maybe_fifo_p (rtx pat)
{
  if (GET_CODE (pat) == PARALLEL)
    {
      for (int i = 0; i != XVECLEN (pat, 0); ++i)
	if (volatile_store_maybe_fifo_p (XVECEXP (pat, 0, i)))
	  return true;
      return false;
    }
  if (GET_CODE (pat) != SET)
    return false;
  rtx dest = SET_DEST (pat);
  if (!MEM_P (dest) || !MEM_VOLATILE_P (dest))
    return false;
  rtx addr = XEXP (dest, 0);
  if (CONST_INT_P (addr))
    {
      unsigned HOST_WIDE_INT a = UINTVAL (addr) & 0xffffffff;
      return a >= XTT_INSTRN_BUF_MMIO_BASE && a <= XTT_INSTRN_BUF_MMIO_LIMIT;
    }
  rtx base = addr;
  if (GET_CODE (base) == CONST)
    base = XEXP (base, 0);
  if (GET_CODE (base) == PLUS && CONST_INT_P (XEXP (base, 1)))
    base = XEXP (base, 0);
  if (GET_CODE (base) == LO_SUM)
    base = XEXP (base, 1);
  if (GET_CODE (base) == CONST)
    base = XEXP (base, 0);
  if (GET_CODE (base) == PLUS && CONST_INT_P (XEXP (base, 1)))
    base = XEXP (base, 0);
  if (GET_CODE (base) == SYMBOL_REF)
    return strcmp (XSTR (base, 0), "__instrn_buffer") == 0;
  if (REG_P (base) && REGNO (base) == STACK_POINTER_REGNUM)
    return false;
  return true;			/* unresolvable: fail closed */
}

/* Return true if nothing in LOOP can change the replay buffer.  A raw asm
   or an unknown callee might, without showing it in the RTL, as might a
   volatile store to the instruction FIFO (volatile_store_maybe_fifo_p).
   A typed replay owner is also treated as a boundary, even though its
   declared slots are already excluded.  Typed barriers are harmless.  */
static bool
loop_preserves_replay_p (class loop *loop)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfun)
    if (flow_bb_inside_loop_p (loop, bb))
      {
	rtx_insn *insn;
	FOR_BB_INSNS (bb, insn)
	  if (NONDEBUG_INSN_P (insn)
	      && (CALL_P (insn)
		  || asm_noperands (PATTERN (insn)) >= 0
		  || (GET_CODE (insn) == INSN
		      && recog_memoized (insn) >= 0
		      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
		  || (volatile_refs_p (PATTERN (insn))
		      && volatile_store_maybe_fifo_p (PATTERN (insn)))))
	    return false;
      }
  return true;
}

/* First-iteration peel (-mtt-tensix-optimize-record-hoist-peel, with
   -mtt-tensix-optimize-replay-record-hoist).

   A no-exec recording of a payload that stores to Dst, re-recorded on
   each iteration of an outer loop with launches in between, hangs the
   device, so such hoists are refused when the preheader is inside
   another loop.  An exec-while-record recording in the same place is
   safe; it is what the in-block formation already does every iteration.

   So instead of a bare recording, the whole first iteration is moved to
   the preheader: the recording executes while recording, followed by the
   other launches, each in-body recording becomes a launch, and the loop
   counter is initialized one step further so the loop runs trips - 1
   times.  The executed words are unchanged apart from the removed
   per-iteration recordings.

   Requirements, each with a named refusal:
     record-hoist-peel-qsr-exec-record-unavailable  Quasar cannot execute
					while recording
     record-hoist-peel-multibb-loop     the loop is a single block
     record-hoist-peel-trips-unproven   constant trips >= 2 (the loop is
					a do-while, so one trip cannot be
					peeled)
     record-hoist-peel-body-foreign-insn the body contains only clones,
					the counter step and the jump
     record-hoist-peel-counter-rewrite-unproven  the new counter
					initialization is a single-insn
					constant

   A peeled recording skips the downstream-fallback check, which applies
   only to no-exec recordings, and the end-of-pass sweep skips it as an
   exec-while-record recording.  */

struct peel_plan
{
  bool valid = false;
  rtx counter = nullptr;
  uint64_t new_init = 0;
  machine_mode mode = VOIDmode;
  uint64_t trips = 0;
};

/* Return true, filling *PLAN, if the first iteration of LOOP can be
   peeled into PREHEADER as described above struct peel_plan: LOOP is a
   single block with constant trips >= 2, its new counter initialization
   is a single-insn constant, and every non-debug body insn is in a clone
   of SEQ, a fixed-encoding TTINCRWC, the counter step or the final jump.
   Otherwise fire the named refusal and return false.  */

static bool
peel_admissible_p (class loop *loop, basic_block preheader,
		   replay_block const &block, replay_sequence const &seq,
		   peel_plan *plan)
{
  auto refuse = [] (const char *why, int uid) -> bool
    {
      rvtt_refusal_fire_composed ("record-hoist-peel", why);
      if (dump_file)
	{
	  fprintf (dump_file, "record-hoist refused: record-hoist-peel-%s",
		   why);
	  if (uid >= 0)
	    fprintf (dump_file, ": body insn %d", uid);
	  fprintf (dump_file, "\n");
	}
      return false;
    };

  if (riscv_tt_fix_qsr_replay > 0)
    return refuse ("qsr-exec-record-unavailable", -1);
  if (loop->num_nodes != 1)
    return refuse ("multibb-loop", -1);

  uint64_t trips;
  rtx_insn *step_insn = nullptr;
  if (!rvtt_loop_trips (loop, preheader, &trips, &step_insn)
      || trips < 2)
    return refuse ("trips-unproven", -1);

  rtx step_set = single_set (step_insn);
  rtx counter = SET_DEST (step_set);
  uint64_t step = UINTVAL (XEXP (SET_SRC (step_set), 1));
  scalar_int_mode mode = as_a<scalar_int_mode> (GET_MODE (counter));
  unsigned prec = GET_MODE_PRECISION (mode);
  uint64_t mask = prec == 64 ? ~uint64_t (0) : (uint64_t (1) << prec) - 1;
  uint64_t init;
  if (!rvtt_constant_reaching_value (preheader, counter, &init))
    return refuse ("counter-rewrite-unproven", -1);
  uint64_t new_init = (init + step) & mask;
  rtx new_init_rtx = gen_int_mode (new_init, mode);
  /* Post-reload only single-insn constants may be materialized.  */
  if (!SMALL_OPERAND (INTVAL (new_init_rtx))
      && !LUI_OPERAND (INTVAL (new_init_rtx)))
    return refuse ("counter-rewrite-unproven", -1);

  /* Every body insn must be one of the kinds the peel copies (clone
     members, TTINCRWC steps copied in order, the counter step, the
     final jump); anything else would be lost from the peeled
     iteration.  */
  hash_set<rtx_insn *> covered;
  for (auto const &clone : seq.clones)
    for (auto pos = block.data () + clone.begin,
	  end = block.data () + clone.end; pos != end; ++pos)
      covered.add (pos->insn);
  rtx_insn *jump = BB_END (loop->header);
  rtx_insn *insn;
  FOR_BB_INSNS (loop->header, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (insn == step_insn || insn == jump || covered.contains (insn))
	continue;
      if (GET_CODE (insn) == INSN
	  && recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
	  && rvtt_fixed_replay_rtx_p (PATTERN (insn)))
	continue;
      return refuse ("body-foreign-insn", INSN_UID (insn));
    }

  plan->valid = true;
  plan->counter = counter;
  plan->new_init = new_init;
  plan->mode = mode;
  plan->trips = trips;
  return true;
}

/* Placement lift (-mtt-tensix-optimize-record-hoist-lift, with
   -mtt-tensix-optimize-replay-record-hoist).

   The downstream-fallback check refuses a no-exec recording that would
   be ingested too soon after a Dst auto-increment mod-write row.  That
   applies only to the innermost preheader; a preheader further out, up
   to the function entry, may be far enough away.  The lift walks outward
   across enclosing loops and places the unchanged no-exec recording in
   the outermost preheader that passes the check.  Each crossed loop must
   preserve the replay buffer, and each candidate placement must be a
   dedicated preheader with no user recording open and must pass the
   same check.  If no level qualifies, the hoist is refused
   (record-hoist-lift-no-admissible-level).

   The recording still dominates every launch.  Payloads that store to
   Dst have already been refused, and re-recording the same fixed words
   once per outer iteration is harmless.  The end-of-pass sweep rechecks
   the final placement.  The pricing for the innermost loop still holds,
   since the lifted recording is delivered no more often.  */

struct hoist_lift_plan
{
  bool valid = false;
  basic_block placement = nullptr;
  unsigned levels = 0;
};

/* Walk outward from PREHEADER as described above hoist_lift_plan,
   crossing enclosing loops while each preserves the replay buffer, has
   a dedicated preheader and has no open recording (DIRTY_BBS).  Fill
   *LIFT with the outermost crossed preheader that passes the
   downstream-fallback check and return true, or fire
   record-hoist-lift-no-admissible-level and return false.  */

static bool
hoist_lift_admit (basic_block preheader, bitmap dirty_bbs,
		  hoist_lift_plan *lift)
{
  hash_set<rtx_insn *> pass_launches;
  for (rtx_insn *launch : formed_playback_launches)
    pass_launches.add (launch);

  basic_block best = nullptr;
  unsigned best_levels = 0;
  unsigned levels = 0;
  basic_block ph = preheader;
  for (class loop *l = ph->loop_father; l && l->num != 0;)
    {
      basic_block *body = get_loop_body (l);
      rtx_insn *refusal_insn = nullptr;
      const char *refusal = rvtt_macro_epoch_loop_replay_preserved_p
	(cfun, body, l->num_nodes, l->header, pass_launches, &refusal_insn);
      free (body);
      if (refusal)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d"
		     " (%s, insn %d)\n",
		     l->num, refusal,
		     refusal_insn ? INSN_UID (refusal_insn) : -1);
	  break;
	}
      basic_block up = dedicated_loop_preheader (l);
      if (!up)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d"
		     " (no dedicated preheader)\n", l->num);
	  break;
	}
      if (bitmap_bit_p (dirty_bbs, up->index))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d (preheader"
		     " bb %d may hold open recording state)\n",
		     l->num, up->index);
	  break;
	}
      ++levels;
      unsigned dist = 0;
      if (!rvtt_dst_autoincr_hoist_capture_composition_p (up, &dist))
	{
	  best = up;
	  best_levels = levels;
	}
      else if (dump_file)
	fprintf (dump_file,
		 "record-hoist-lift: level %u placement bb %d still within"
		 " a mod-write drained-frontend window (distance %u);"
		 " walking on\n", levels, up->index, dist);
      ph = up;
      l = ph->loop_father;
    }

  if (!best)
    {
      rvtt_refuse (RVTT_REF_RECORD_HOIST_LIFT_NO_ADMISSIBLE_LEVEL, dump_file,
		   "record-hoist-lift refused:"
		   " record-hoist-lift-no-admissible-level:"
		   " no oracle-clean admissible placement (walked %u"
		   " level(s))\n", levels);
      return false;
    }

  lift->valid = true;
  lift->placement = best;
  lift->levels = best_levels;
  if (dump_file)
    fprintf (dump_file,
	     "record-hoist-lift: lifted placement to bb %d (%u level(s)"
	     " out; oracle-clean, crossed loops replay-preserving)\n",
	     best->index, best_levels);
  return true;
}

/* Return the dedicated preheader of the loop around BLOCK if the
   recording of SEQ can be hoisted into it, otherwise null, in which case
   SEQ is formed in-block.  The checks are the loop shape, replay
   preservation, no open recording (DIRTY_BBS), a fixed-encoding
   payload, the Dst-store check (which a peel, returned in *PEEL, can
   satisfy), the downstream-fallback check (which a lift, returned in
   *LIFT, can satisfy) and hoist_profitable_p.  */

static basic_block
hoist_preheader (replay_sequence const &seq, replay_block const &block,
		 bitmap dirty_bbs, peel_plan *peel, hoist_lift_plan *lift)
{
  basic_block bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return nullptr;
  bool record_hoist = riscv_tt_opt_replay_record_hoist > 0;
  if (loop->num_nodes != 1 || loop->header != bb)
    {
      /* With the record-hoist flag, a multi-block loop is accepted if
	 the capture block dominates the latch, so it executes on every
	 complete iteration as the pricing assumes; the
	 replay-preservation check below covers every block.  Tile loops
	 often have this shape.  */
      bool multi_bb_ok = false;
      if (record_hoist && loop->latch)
	{
	  bool free_dom = !dom_info_available_p (CDI_DOMINATORS);
	  calculate_dominance_info (CDI_DOMINATORS);
	  multi_bb_ok = dominated_by_p (CDI_DOMINATORS, loop->latch, bb);
	  if (free_dom)
	    free_dominance_info (CDI_DOMINATORS);
	}
      if (!multi_bb_ok)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Not hoisting: candidate bb %d is not a single-bb loop"
		       " header\n",
		       bb->index);
	      if (record_hoist)
		rvtt_refuse (RVTT_REF_RECORD_HOIST_LOOP_SHAPE, dump_file,
			     "record-hoist refused: record-hoist-loop-shape:"
			     " capture bb %d does not dominate the latch"
			     " (conditional per-trip execution unpriced)\n",
			     bb->index);
	    }
	  return nullptr;
	}
      if (dump_file && record_hoist)
	fprintf (dump_file,
		 "record-hoist: multi-block loop %d admitted (capture bb %d"
		 " dominates latch bb %d)\n",
		 loop->num, bb->index, loop->latch->index);
    }
  if (!loop_preserves_replay_p (loop))
    {
      /* With the record-hoist flag, recheck a refused loop with the
	 more precise walk in rvtt-macro-epoch.cc, which can show that
	 raw sync words and computed FIFO pushes leave the replay buffer
	 alone, and accepts this pass's own launches.  */
      const char *audit_refusal = nullptr;
      rtx_insn *audit_insn = nullptr;
      if (record_hoist)
	{
	  hash_set<rtx_insn *> pass_launches;
	  for (rtx_insn *launch : formed_playback_launches)
	    pass_launches.add (launch);
	  basic_block *body = get_loop_body (loop);
	  audit_refusal = rvtt_macro_epoch_loop_replay_preserved_p
	    (cfun, body, loop->num_nodes, loop->header, pass_launches,
	     &audit_insn);
	  free (body);
	}
      if (!record_hoist || audit_refusal)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Not hoisting: loop contains call, opaque asm, or replay"
		       " owner\n");
	      /* This also ensures that nothing in the loop re-records the
		 hoisted slots between the preheader recording and a later
		 launch: other windows lie within one block and hoisted slots
		 are excluded from later formation.  */
	      if (record_hoist)
		rvtt_refuse (RVTT_REF_RECORD_HOIST_LOOP_OPAQUE, dump_file,
			     "record-hoist refused: record-hoist-loop-opaque:"
			     " %s (insn %d)\n", audit_refusal,
			     audit_insn ? INSN_UID (audit_insn) : -1);
	    }
	  return nullptr;
	}
      if (dump_file)
	fprintf (dump_file,
		 "record-hoist: loop %d replay-state audit admitted"
		 " (every body word proven replay-preserving)\n",
		 loop->num);
    }

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    {
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Not hoisting: loop has no dedicated preheader\n");
	  if (record_hoist)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_NO_DEDICATED_PREHEADER,
			 dump_file,
			 "record-hoist refused:"
			 " record-hoist-no-dedicated-preheader\n");
	}
      return nullptr;
    }
  if (bitmap_bit_p (dirty_bbs, preheader->index))
    {
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Not hoisting: preheader bb %d may hold open recording"
		   " state\n", preheader->index);
	  if (record_hoist)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_PREHEADER_RECORDING_OPEN,
			 dump_file,
			 "record-hoist refused:"
			 " record-hoist-preheader-recording-open\n");
	}
      return nullptr;
    }

  /* Every payload word must have a fixed encoding (hard LREGs,
     constants, compiler scratch).  A GPR or MEM operand means the word
     is built at run time, and its value in the preheader might differ
     from its value in the loop.  Without the record-hoist flag this is
     checked after pricing, below.  */
  if (record_hoist)
    for (auto pos = block.data () + seq.clones.front ().begin,
	  end = block.data () + seq.clones.front ().end;
	 pos != end; ++pos)
      if (!pos->empty && !rvtt_fixed_replay_rtx_p (PATTERN (pos->insn)))
	{
	  rvtt_refuse (RVTT_REF_RECORD_HOIST_VARIANT_ENCODING, dump_file,
		       "record-hoist refused: record-hoist-variant-encoding:"
		       " payload insn %d is a run-time-composed word\n",
		       INSN_UID (pos->insn));
	  return nullptr;
	}

  /* A payload that stores to Dst, recorded no-exec in a preheader that
     is itself inside a loop, would be undone by unhoist_hazard_rerecords
     (rule 1), leaving code worse than not hoisting.  Refuse it now under
     the same name.  A loop-free preheader is fine (rule 3).  */
  if (record_hoist)
    {
      class loop *ph_loop = preheader->loop_father;
      if (ph_loop && ph_loop->num != 0)
	for (auto pos = block.data () + seq.clones.front ().begin,
	      end = block.data () + seq.clones.front ().end;
	     pos != end; ++pos)
	  if (!pos->empty
	      && (recog_memoized (pos->insn) == CODE_FOR_rvtt_sfpstore_int
		  || recog_memoized (pos->insn)
		     == CODE_FOR_rvtt_sfpstoresrcs_int))
	    {
	      /* A first-iteration peel avoids this case, as it leaves no
		 no-exec Dst-store recording.  */
	      /* In reform_mode the peel would move one iteration's carried
		 Dst accesses into the preheader, across the placement of
		 the address-modifier configuration; that ordering is not
		 verified, so refuse the peel.  */
	      if (reform_mode
		  && payload_contains_carried_p (block, seq.clones.front ()))
		{
		  rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_PEEL_LAUNCH_ARITHMETIC_UNPROVEN, dump_file,
			       "record-hoist refused:"
			       " post-autoincr-window-carried-peel-"
			       "launch-arithmetic-unproven:"
			       " carried payload, first-trip peel would"
			       " relocate carried executions to preheader"
			       " bb %d (walk-order proof not in this"
			       " increment)\n",
			       preheader->index);
		}
	      else if (riscv_tt_opt_record_hoist_peel > 0
		       && peel_admissible_p (loop, preheader, block, seq,
					     peel))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "record-hoist-peel: dststore composition"
			     " rescued by exec-while-record first-trip peel"
			     " (preheader bb %d inside loop %d; trips %lu"
			     " -> %lu)\n",
			     preheader->index, ph_loop->num,
			     (unsigned long) peel->trips,
			     (unsigned long) (peel->trips - 1));
		  break;
		}
	      rvtt_refuse
		(RVTT_REF_NOEXEC_RERECORD_DSTSTORE_COMPOSITION_UNAUDITED,
		 dump_file,
		 "record-hoist refused:"
		 " noexec-rerecord-dststore-composition-unaudited:"
		 " Dst-store payload, preheader bb %d inside loop %d"
		 " (the re-record sweep would un-hoist)\n",
		 preheader->index, ph_loop->num);
	      return nullptr;
	    }
    }

  /* The record-hoist pricing assumes both versions execute the same
     words.  That fails if the no-exec recording lands too soon after a
     row that the Dst auto-increment pass would turn into a mod-write:
     that pass then refuses the group (that combination fails on
     hardware) and keeps the explicit increments, which is slower overall
     (rvtt-cost.md).  So refuse such hoists, using the same distance
     check as rtl-rvtt-dst-autoincr.cc.  This only matters when that pass
     is enabled.  */
  /* A peel records with exec=1, which that check does not cover.  */
  if (record_hoist && !peel->valid
      && TARGET_XTT_TENSIX && riscv_tt_opt_dst_autoincr > 0)
    {
      unsigned dist = 0;
      if (rvtt_dst_autoincr_hoist_capture_composition_p (preheader, &dist))
	{
	  /* Try a placement further out (see hoist_lift_plan).  */
	  if (!(riscv_tt_opt_record_hoist_lift > 0
		&& hoist_lift_admit (preheader, dirty_bbs, lift)))
	    {
	      rvtt_refuse
		(RVTT_REF_RECORD_HOIST_DOWNSTREAM_FALLBACK_UNPROFITABLE,
		 dump_file,
		 "record-hoist refused:"
		 " record-hoist-downstream-fallback-unprofitable:"
		 " hoisted no-exec record within the drained-frontend"
		 " window of a would-be dst-autoincr mod-write row"
		 " (distance %u < %u, preheader bb %d; the group guard"
		 " would refuse and the mod-write falls back)\n",
		 dist, rvtt_modwrite_drained_frontend_window (),
		 preheader->index);
	      return nullptr;
	    }
	}
    }

  if (!hoist_profitable_p (loop, preheader, block, seq.clones.front (),
			   /*body_rerecords=*/true,
			   max_contiguous_launch_run (seq, block)))
    return nullptr;

  for (auto pos = block.data () + seq.clones.front ().begin,
	end = block.data () + seq.clones.front ().end;
       pos != end; ++pos)
    if (!pos->empty && !rvtt_fixed_replay_rtx_p (PATTERN (pos->insn)))
      {
	if (dump_file)
	  fprintf (dump_file,
		   "Not hoisting: payload insn %d is not fixed encoding\n",
		   INSN_UID (pos->insn));
	return nullptr;
      }

  return preheader;
}

/* No-exec captures hoisted by this pass in the current function, for
   unhoist_hazard_rerecords.  User recordings are never added.  */
static std::vector<rtx_insn *> formed_noexec_captures;


/* Hoist SEQ: emit a no-exec capture into slots [REPLAY_START, +length)
   followed by a copy of the payload's non-empty insns at the end of
   PREHEADER, then replace every clone, the first included, with a
   launch.  Register the capture and launches for the end-of-pass
   sweeps.  Return the recorded length.  */

static unsigned
replace_hoisted_sequence (replay_sequence &seq, replay_block &block,
			  unsigned replay_start, basic_block preheader)
{
  unsigned length = seq.length;
  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
     const0_rtx, GEN_INT (1));

  start_sequence ();
  emit_insn (capture);
  for (auto pos = block.data () + seq.clones.front ().begin,
	end = block.data () + seq.clones.front ().end;
       pos != end; ++pos)
    if (!pos->empty)
      emit_insn (copy_insn (PATTERN (pos->insn)));
  rtx_insn *recording = get_insns ();
  end_sequence ();

  rtx_insn *anchor = BB_END (preheader);
  if (JUMP_P (anchor))
    emit_insn_before (recording, anchor);
  else
    emit_insn_after (recording, anchor);

  /* The first emitted insn is the capture.  */
  formed_noexec_captures.push_back (recording);

  for (auto const &clone : seq.clones)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
      rtx_insn *launch
	= emit_insn_after (replay, block[clone.end - 1].insn);
      formed_playback_launches.push_back (launch);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	SET_INSN_DELETED (pos->insn);
    }

  if (dump_file)
    fprintf (dump_file,
	     "Hoisted no-exec capture [%u,+%u) to preheader bb %d;"
	     " %u playbacks\n\n",
	     replay_start, length, preheader->index,
	     unsigned (seq.clones.size ()));
  return length;
}

/* Peel the first iteration (see peel_plan).  The preheader receives, in
   order: the counter initialization (scalar, so independent of the
   Tensix words), the exec-while-record capture, the payload, and the
   first iteration's other launches.  Every clone in the body, including
   the former recording, becomes a launch.  */

static unsigned
replace_hoisted_sequence_peel (replay_sequence &seq, replay_block &block,
			       unsigned replay_start, basic_block preheader,
			       peel_plan const &plan)
{
  unsigned length = seq.length;
  basic_block body_bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  rtx_insn *body_jump = BB_END (body_bb);

  /* Map each body insn to its clone, and note each clone's first
     insn.  */
  hash_map<rtx_insn *, unsigned> member_clone;
  hash_set<rtx_insn *> clone_first;
  for (unsigned ix = 0; ix != seq.clones.size (); ++ix)
    {
      auto const &clone = seq.clones[ix];
      clone_first.add (block[clone.begin].insn);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	member_clone.put (pos->insn, ix);
    }

  auto gen_launch = [&] () -> rtx
    {
      return gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
    };

  start_sequence ();
  emit_insn (gen_rtx_SET (plan.counter,
			  gen_int_mode (plan.new_init, plan.mode)));
  unsigned peeled_launches = 0, peeled_steps = 0;
  {
    rtx_insn *insn;
    FOR_BB_INSNS (body_bb, insn)
      {
	if (!NONDEBUG_INSN_P (insn) || insn == body_jump)
	  continue;
	if (unsigned *cl = member_clone.get (insn))
	  {
	    if (*cl == 0)
	      {
		if (clone_first.contains (insn))
		  emit_insn (gen_rvtt_ttreplay_int
			     (const0_rtx, const0_rtx, const0_rtx,
			      GEN_INT (length),
			      rvtt_gen_rtx_noval (XTT32SImode),
			      GEN_INT (replay_start),
			      GEN_INT (1), GEN_INT (1)));
		/* Copy the non-empty insns, as in the hoist path.  */
		for (auto pos = block.data () + seq.clones.front ().begin,
		      end = block.data () + seq.clones.front ().end;
		     pos != end; ++pos)
		  if (pos->insn == insn)
		    {
		      if (!pos->empty)
			emit_insn (copy_insn (PATTERN (insn)));
		      break;
		    }
	      }
	    else if (clone_first.contains (insn))
	      {
		emit_insn (gen_launch ());
		++peeled_launches;
	      }
	    continue;
	  }
	if (GET_CODE (insn) == INSN
	    && recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc)
	  {
	    emit_insn (copy_insn (PATTERN (insn)));
	    ++peeled_steps;
	    continue;
	  }
	/* The counter step: handled by the re-init.  */
      }
  }
  rtx_insn *recording = get_insns ();
  end_sequence ();

  rtx_insn *anchor = BB_END (preheader);
  if (JUMP_P (anchor))
    emit_insn_before (recording, anchor);
  else
    emit_insn_after (recording, anchor);

  /* Register the capture for the end-of-pass sweep (which skips
     exec-while-record captures) and the launches for the loop
     replay-preservation check.  */
  bool saw_capture = false;
  for (rtx_insn *insn = recording; insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn) && GET_CODE (insn) == INSN
	  && recog_memoized (insn) == CODE_FOR_rvtt_ttreplay_int)
	{
	  if (!saw_capture)
	    {
	      formed_noexec_captures.push_back (insn);
	      saw_capture = true;
	    }
	  else
	    formed_playback_launches.push_back (insn);
	}
      if (insn == BB_END (preheader))
	break;
    }

  for (auto const &clone : seq.clones)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
      rtx_insn *launch
	= emit_insn_after (replay, block[clone.end - 1].insn);
      formed_playback_launches.push_back (launch);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	SET_INSN_DELETED (pos->insn);
    }

  if (dump_file)
    fprintf (dump_file,
	     "record-hoist-peel: exec-recorded [%u,+%u) in preheader bb %d"
	     " with %u peeled sibling launches + %u peeled Dst steps;"
	     " %u body clones -> launches; trips %lu -> %lu;"
	     " counter reinit " HOST_WIDE_INT_PRINT_DEC "\n\n",
	     replay_start, length, preheader->index,
	     peeled_launches, peeled_steps,
	     unsigned (seq.clones.size ()),
	     (unsigned long) plan.trips, (unsigned long) (plan.trips - 1),
	     (HOST_WIDE_INT) plan.new_init);
  return length;
}

/* Remove or adjust those sequences that are invalidated by having used SEQ.
   (SEQ itself auto-invalidates).  */

static bool
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

/* Subtract the persistent slots (used by hoisted or canonicalized
   recordings) from the free spans in BASE and return the remaining spans
   of at least MIN_SEQUENCE slots, sorted by decreasing length.  Spans
   are (start, length) pairs.  */

static std::vector<replay_span>
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

/* A counted-loop capture must be one fixed, uninterrupted SFPU run.  Scalar
   loop-control instructions may surround it.  A single typed TTINCRWC may
   follow the run: it remains explicit after the playback and therefore
   preserves the per-iteration Dst boundary used by semantic SFPI loops.
   Counter operations before or inside the run, ordinary memory, calls, opaque
   asm, configuration operations, dynamic instruction words, and explicit
   replay ownership make the whole loop ineligible.  */
static bool
counted_loop_payload (class loop *loop, replay_block &info,
		      replay_sequence &seq)
{
  if (loop->num_nodes != 1 || loop->header != loop->latch
      || !loop_preserves_replay_p (loop))
    return false;

  bool saw_safe = false;
  bool saw_trailing_increment = false;
  rtx_insn *insn;
  FOR_BB_INSNS (loop->header, insn)
    if (NONDEBUG_INSN_P (insn))
      {
	if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0
	    || contains_mem_rtx_p (PATTERN (insn)))
	  return false;
	if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	    && get_attr_type (insn) == TYPE_TENSIX)
	  {
	    if (get_attr_xtt_replay (insn) == XTT_REPLAY_SAFE
		&& rvtt_fixed_replay_rtx_p (PATTERN (insn)))
	      {
		if (saw_trailing_increment)
		  return false;
		saw_safe = true;
	      }
	    else if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
		     && saw_safe && !saw_trailing_increment)
	      saw_trailing_increment = true;
	    else
	      return false;
	  }
      }

  if (!scan_insns (info, loop->header))
    return false;

  unsigned length = 0;
  for (unsigned ix = 0; ix != info.size (); ++ix)
    {
      if (ix + 1 != info.size () && info[ix].must_end)
	return false;
      if (!info[ix].empty)
	++length;
    }
  if (length < MIN_SEQUENCE)
    return false;

  seq = replay_sequence (0, 0, length);
  seq.clones.emplace_back (0, info.size ());
  return true;
}

/* Pricing for peeling the first iteration of a counted-loop capture
   (-mtt-tensix-optimize-counted-capture-peel; rvtt-cost.md).  The plain
   counted-loop hoist pays for delivering the recording in the preheader.
   The peel instead moves the first iteration to the preheader and
   executes it while recording, so the recording costs only the capture
   word plus the record overhead, and trips - 1 launches remain:

     benefit = (trips - 1) * (before - after)
	       - (RISC_PUSH + RECORD_OVERHEAD)	 ; >= MIN_BENEFIT

   with before and after as for the counted-loop capture.  The executed
   words are those of the original loop in the same order.  TRIPS must be
   proven >= 2.  */

static bool
counted_peel_profitable_p (class loop *loop, basic_block preheader,
			   replay_block const &block, replay_span payload)
{
  uint64_t niter;
  if (!rvtt_loop_trips (loop, preheader, &niter) || niter < 2)
    {
      rvtt_refuse (RVTT_REF_COUNTED_CAPTURE_PEEL_TRIPS_UNPROVEN, dump_file,
		   "counted-capture-peel refused:"
		   " counted-capture-peel-trips-unproven (loop %d)\n",
		   loop->num);
      return false;
    }
  HOST_WIDE_INT trips = (HOST_WIDE_INT) niter;
  HOST_WIDE_INT eslots = exec_interlocked_slots (block, payload);
  if (eslots < 0)
    {
      rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED, dump_file,
		   "counted-capture-peel refused:"
		   " replay-reissue-latency-unproved (loop %d)\n",
		   loop->num);
      return false;
    }
  HOST_WIDE_INT words = delivered_words (block, payload);
  HOST_WIDE_INT min_benefit = rvtt_dcost_replay_hoist_min_benefit ();
  /* See replay_pricing in rvtt-delivery-cost-core.h.  */
  rvtt_delivery_cost::replay_price price = rvtt_dcost_replay_pricing
    (rvtt_delivery_cost::SHAPE_COUNTED_PEEL, trips, words, eslots,
     /*launch_run=*/1, false, min_benefit);
  HOST_WIDE_INT before = price.before;
  HOST_WIDE_INT after = price.after;
  HOST_WIDE_INT peel_cost = price.record;
  HOST_WIDE_INT benefit = price.benefit;
  if (dump_file)
    fprintf (dump_file,
	     "Counted-peel pricing (loop %d): trips %ld, words %ld,"
	     " exec_ilk %ld slots, before %ld, after %ld, peel_cost %ld,"
	     " benefit %ld (min %ld)\n",
	     loop->num, (long) trips, (long) words, (long) eslots,
	     (long) before, (long) after, (long) peel_cost,
	     (long) benefit, (long) min_benefit);
  if (!price.profitable)
    {
      rvtt_refuse (RVTT_REF_COUNTED_CAPTURE_PEEL_BENEFIT, dump_file,
		   "counted-capture-peel refused:"
		   " counted-capture-peel-benefit: modeled benefit %ld < %ld\n",
		   (long) benefit, (long) min_benefit);
      return false;
    }
  return true;
}

/* Hoist each eligible counted loop of CFN: the body of a single-block
   loop that is one fixed-encoding SFPU run (counted_loop_payload) becomes
   a no-exec capture in the dedicated preheader plus one launch per
   iteration, or, if that is unprofitable and the peel is enabled, a
   first-iteration peel.  Slots come from REPLAY_SPANS minus
   PERSISTENT_SLOTS, and used slots are added to PERSISTENT_SLOTS.
   DIRTY_BBS are blocks where a user recording may be open; STICKY is as
   for span_companion_sound_p.  */

static void
hoist_counted_loops (function *cfn,
		     std::vector<replay_span> const &replay_spans,
		     std::vector<bool> &persistent_slots,
		     bitmap dirty_bbs, bool sticky)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;
      if (bitmap_bit_p (dirty_bbs, bb->index))
	/* A user recording may be open here.  */
	continue;

      replay_block info;
      replay_sequence seq;
      if (!counted_loop_payload (loop, info, seq))
	continue;
      if (!span_companion_sound_p (info, seq.clones.front (), sticky))
	continue;
      /* See reform_carried_launch_arithmetic_ok.  */
      if (reform_mode
	  && payload_contains_carried_p (info, seq.clones.front ())
	  && !reform_carried_launch_arithmetic_ok (info, seq))
	continue;

      basic_block preheader = dedicated_loop_preheader (loop);
      if (!preheader)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Not hoisting: loop has no dedicated preheader\n");
	  continue;
	}
      if (bitmap_bit_p (dirty_bbs, preheader->index))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Not hoisting: preheader bb %d may hold open recording"
		     " state\n", preheader->index);
	  continue;
	}
      /* Launches in successive iterations are separated by the loop
	 control, so the launch run is 1.  */
      peel_plan peel;
      if (!hoist_profitable_p (loop, preheader, info, seq.clones.front (),
			       /*body_rerecords=*/false,
			       /*launch_run=*/1))
	{
	  /* Try the first-iteration peel instead.  In reform_mode it is
	     refused for carried payloads, as for the record-hoist
	     peel.  */
	  if (!(riscv_tt_opt_counted_capture_peel > 0))
	    continue;
	  if (reform_mode
	      && payload_contains_carried_p (info, seq.clones.front ()))
	    {
	      rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_PEEL_LAUNCH_ARITHMETIC_UNPROVEN,
			   dump_file,
			   "counted-capture-peel refused:"
			   " post-autoincr-window-carried-peel-"
			   "launch-arithmetic-unproven:"
			   " carried payload, first-trip peel would relocate"
			   " carried executions to the preheader (loop %d)\n",
			   loop->num);
	      continue;
	    }
	  if (!counted_peel_profitable_p (loop, preheader, info,
					  seq.clones.front ())
	      || !peel_admissible_p (loop, preheader, info, seq, &peel))
	    continue;
	}

      auto spans = available_replay_spans (replay_spans, persistent_slots);
      auto slot = std::find_if (spans.begin (), spans.end (),
				[&seq] (replay_span span)
				{ return span.end >= seq.length; });
      if (slot == spans.end ())
	continue;

      unsigned length
	= peel.valid
	  ? replace_hoisted_sequence_peel (seq, info, slot->begin, preheader,
					   peel)
	  : replace_hoisted_sequence (seq, info, slot->begin, preheader);
      std::fill (persistent_slots.begin () + slot->begin,
		 persistent_slots.begin () + slot->begin + length, true);
      if (dump_file && peel.valid)
	fprintf (dump_file,
		 "counted-capture-peel admitted: counted-loop bb %d peeled"
		 " exec-while-record (trips %lu -> %lu)\n",
		 bb->index, (unsigned long) peel.trips,
		 (unsigned long) (peel.trips - 1));
      if (dump_file)
	fprintf (dump_file,
		 "Counted-loop replay payload bb %d length %u captured at %u\n",
		 bb->index, length, slot->begin);
    }
}

/* Complete unrolling of launch loops.  After hoisting, a counted loop
   body may consist only of launches and Dst steps, plus the counter
   update and branch, which cost two delivered words per iteration and
   separate consecutive launches.  When the trip count is provable
   (rvtt_loop_trips), emit TRIPS copies of the body, set the counter to
   its final value (later passes delete it if dead) and remove the loop.
   Unrolling in GIMPLE cannot do this, since the loop only takes this form
   after the recording has been hoisted.

   Every non-debug insn in the single-block body must be:
     (a) a fixed-encoding TTREPLAY launch,
     (b) a TTINCRWC with constant operands,
     (c) the loop's single counter step, or
     (d) the final conditional jump.
   The total number of delivered words is limited by
   XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS.  The contiguous launch run created
   here is a result of the hoist, so hoist_profitable_p does not count it
   as LAUNCH_RUN.  */

static bool
unroll_launch_loop (class loop *loop, bitmap dirty_bbs)
{
  basic_block header = loop->header;
  if (loop->num_nodes != 1 || loop->header != loop->latch)
    return false;
  if (bitmap_bit_p (dirty_bbs, header->index))
    /* A user recording may be open here.  */
    return false;

  /* "#pragma GCC unroll" controls duplication of the payload, which this
     does not do: the recording stays in the preheader and only launch
     words are copied.  Loop structures are rebuilt after reload, so
     loop->unroll is normally clear here, but honour it if it is set.  */
  if (loop->unroll)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d carries an explicit user"
		 " unroll request\n", header->index);
      return false;
    }

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    return false;

  rtx_insn *jump = BB_END (header);
  if (!JUMP_P (jump) || !any_condjump_p (jump) || !onlyjump_p (jump)
      || EDGE_COUNT (header->succs) != 2)
    return false;

  edge e_branch = BRANCH_EDGE (header);
  edge e_fall = FALLTHRU_EDGE (header);
  /* A fallthrough cannot re-enter its own block, so the backedge must be
     the taken branch and the fallthrough the unique exit.  */
  if (e_branch->dest != header || e_fall->dest == header
      || (e_branch->flags & EDGE_ABNORMAL) || (e_fall->flags & EDGE_ABNORMAL))
    return false;

  /* Loops without a launch are skipped silently.  */
  bool has_playback = false;
  rtx_insn *insn;
  FOR_BB_INSNS (header, insn)
    if (NONDEBUG_INSN_P (insn) && GET_CODE (insn) == INSN
	&& recog_memoized (insn) >= 0
	&& get_attr_type (insn) == TYPE_TENSIX)
      {
	replay_span span;
	if (is_replay_insn (span, insn) == REPLAY_playback)
	  {
	    has_playback = true;
	    break;
	  }
      }
  if (!has_playback)
    return false;

  /* Classify the body.  DELIVERY collects the per-trip delivered words in
     program order; STEP is the single scalar counter update.  */
  std::vector<rtx_insn *> delivery;
  rtx_insn *step = nullptr;
  unsigned launches = 0;
  FOR_BB_INSNS (header, insn)
    {
      if (!NONDEBUG_INSN_P (insn) || insn == jump)
	continue;
      if (CALL_P (insn) || GET_CODE (insn) != INSN)
	return false;
      rtx pattern = PATTERN (insn);
      if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER
	  || asm_noperands (pattern) >= 0)
	return false;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  replay_span span;
	  if (is_replay_insn (span, insn) == REPLAY_playback
	      && rvtt_fixed_replay_rtx_p (pattern))
	    {
	      ++launches;
	      delivery.push_back (insn);
	      continue;
	    }
	  if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
	      && rvtt_fixed_replay_rtx_p (pattern))
	    {
	      delivery.push_back (insn);
	      continue;
	    }
	  if (dump_file)
	    fprintf (dump_file,
		     "Launch-loop unroll refused: bb %d body insn %d is not"
		     " a playback launch or typed Dst step\n",
		     header->index, INSN_UID (insn));
	  return false;
	}
      if (step)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Launch-loop unroll refused: bb %d has a second scalar"
		     " insn %d beyond the counter step\n",
		     header->index, INSN_UID (insn));
	  return false;
	}
      step = insn;
    }
  if (!launches || !step)
    return false;

  uint64_t trips, final_value;
  rtx_insn *counter_step;
  if (!rvtt_loop_trips (loop, preheader, &trips, &counter_step,
				&final_value))
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d trip count is not"
		 " provably constant\n", header->index);
      return false;
    }
  /* The only scalar insn must be the counter step; other scalar state
     would be lost by removing the loop.  */
  if (counter_step != step || trips < 2)
    return false;

  rtx step_set = single_set (step);
  rtx counter = SET_DEST (step_set);
  machine_mode counter_mode = GET_MODE (counter);
  rtx final_rtx = gen_int_mode (final_value, counter_mode);
  /* Set the counter to its exit value; after reload only a single-insn
     constant can be loaded.  */
  if (!SMALL_OPERAND (INTVAL (final_rtx)) && !LUI_OPERAND (INTVAL (final_rtx)))
    return false;

  unsigned trip_words = 0;
  for (rtx_insn *d : delivery)
    trip_words += get_attr_length (d) / 4;
  if ((uint64_t) trip_words * trips > XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d unrolled size %lu words"
		 " exceeds %d\n", header->index,
		 (unsigned long) ((uint64_t) trip_words * trips),
		 XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS);
      return false;
    }

  /* If possible, make the hoisted no-exec capture execute while
     recording and drop the first iteration's launch, so the first
     execution happens at the recording as in replace_sequence.  This
     requires that:
       - the target is not Quasar, which cannot execute while recording;
       - the first word of the iteration launches exactly the capture's
	 span;
       - the preheader contains no Tensix word after the capture's
	 payload, so moving the execution crosses only scalar insns.
     The Dst auto-increment pass treats an executing capture as a row
     (ROW_CAPTURE_EXEC).  */
  rtx_insn *exec_capture = nullptr;
  rtx_insn *exec_payload_end = nullptr;
  bool drop_first_launch = false;
  if (!(riscv_tt_fix_qsr_replay > 0) && riscv_tt_opt_replay_exec_record > 0)
    {
      replay_span lead_span;
      if (is_replay_insn (lead_span, delivery.front ()) == REPLAY_playback)
	{
	  /* Find the capture of this span in the preheader and check that
	     nothing Tensix follows its payload.  */
	  rtx_insn *pinsn;
	  rtx_insn *cap = nullptr;
	  unsigned payload_left = 0;
	  bool clean = true;
	  FOR_BB_INSNS (preheader, pinsn)
	    {
	      if (!NONDEBUG_INSN_P (pinsn) || GET_CODE (pinsn) != INSN)
		continue;
	      rtx pat = PATTERN (pinsn);
	      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
		continue;
	      if (asm_noperands (pat) >= 0 || recog_memoized (pinsn) < 0
		  || get_attr_type (pinsn) != TYPE_TENSIX)
		{
		  if (asm_noperands (pat) >= 0)
		    {
		      // An empty asm (a compiler barrier) emits nothing.
		      // Any other asm is an unknown word, which is
		      // acceptable only before the recording (e.g. the
		      // LLK's TTI_STALLWAIT before each tile), since the
		      // execution moves only across words between the
		      // recording and the first launch.
		      const char *tmpl
			= GET_CODE (pat) == ASM_OPERANDS
			  ? ASM_OPERANDS_TEMPLATE (pat)
			  : GET_CODE (pat) == PARALLEL
			      && GET_CODE (XVECEXP (pat, 0, 0)) == ASM_OPERANDS
			    ? ASM_OPERANDS_TEMPLATE (XVECEXP (pat, 0, 0))
			    : nullptr;
		      while (tmpl && (*tmpl == ' ' || *tmpl == '\t'))
			++tmpl;
		      if ((!tmpl || *tmpl) && (cap || payload_left))
			{
			  clean = false;
			  if (dump_file)
			    fprintf (dump_file,
				     "Exec-while-record refused: preheader"
				     " insn %d is a non-empty asm after"
				     " the record\n", INSN_UID (pinsn));
			  break;
			}
		    }
		  /* Scalar insn, empty asm or word before the recording.  */
		  continue;
		}
	      if (payload_left)
		{
		  unsigned words = get_attr_length (pinsn) / 4;
		  if (words > payload_left)
		    {
		      clean = false;
		      break;
		    }
		  payload_left -= words;
		  if (!payload_left)
		    exec_payload_end = pinsn;
		  continue;
		}
	      replay_span span;
	      auto type = is_replay_insn (span, pinsn);
	      /* is_replay_insn's raw span carries {begin = start slot,
	         end = length}.  */
	      if (type == REPLAY_fixed_capture && !cap
		  && span.begin == lead_span.begin
		  && span.end == lead_span.end
		  && XVECEXP (pat, 0, 6) == const0_rtx)
		{
		  cap = pinsn;
		  payload_left = span.end;
		  continue;
		}
	      if (!cap)
		/* Tensix work before the recording is unaffected.  */
		continue;
	      if (!get_attr_length (pinsn))
		/* Zero-length architectural markers deliver no word.  */
		continue;
	      /* A Tensix word between the payload and the first launch
		 would be crossed by the moved execution.  */
	      clean = false;
	      if (dump_file)
		fprintf (dump_file,
			 "Exec-while-record refused: preheader insn %d is a"
			 " Tensix word between record and first launch\n",
			 INSN_UID (pinsn));
	      break;
	    }
	  if (clean && cap && !payload_left)
	    {
	      exec_capture = cap;
	      drop_first_launch = true;
	    }
	  else if (dump_file && clean)
	    fprintf (dump_file,
		     "Exec-while-record refused: no matching record-only"
		     " capture terminates the dedicated preheader\n");
	}
      else if (dump_file)
	fprintf (dump_file,
		 "Exec-while-record refused: the trip's first delivered"
		 " word is not the playback launch\n");
    }

  /* Copy the body TRIPS - 1 more times (the counter step touches only
     the counter, so it commutes with the delivered words), set the
     counter to its final value and remove the loop control.  */
  loops_state_set (LOOPS_NEED_FIXUP);

  if (drop_first_launch)
    {
      /* Make the recording execute, drop the first launch, and emit the
	 rest of the unrolled run in the preheader right after the
	 payload, so that the Dst auto-increment pass sees the executing
	 recording and all the launches in one block, as for an in-place
	 capture.  */
      XVECEXP (PATTERN (exec_capture), 0, 6) = const1_rtx;
      INSN_CODE (exec_capture) = -1;
      rtx_insn *anchor = exec_payload_end;
      for (rtx_insn *d : delivery)
	if (d != delivery.front ())
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      for (uint64_t trip = 1; trip != trips; ++trip)
	for (rtx_insn *d : delivery)
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      emit_insn_after (gen_rtx_SET (counter, final_rtx), anchor);
      for (rtx_insn *d : delivery)
	delete_insn (d);
      if (dump_file)
	fprintf (dump_file,
		 "Exec-while-record: capture insn %d executes trip 1;"
		 " first launch removed; %lu-trip delivery run emitted at"
		 " the record\n", INSN_UID (exec_capture),
		 (unsigned long) trips);
    }
  else
    {
      rtx_insn *anchor = delivery.back ();
      for (uint64_t trip = 1; trip != trips; ++trip)
	for (rtx_insn *d : delivery)
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      emit_insn_after (gen_rtx_SET (counter, final_rtx), anchor);
    }

  delete_insn (step);
  remove_edge (e_branch);
  delete_insn (jump);

  if (dump_file)
    fprintf (dump_file,
	     "Unrolled launch loop bb %d: %lu trips x %u delivered words,"
	     " backedge removed\n", header->index, (unsigned long) trips,
	     trip_words);
  return true;
}

/* Apply unroll_launch_loop to every loop of CFN.  DIRTY_BBS are blocks
   where a user recording may be open.  */

static void
unroll_launch_loops (function *cfn, bitmap dirty_bbs)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;
      unroll_launch_loop (loop, dirty_bbs);
    }
}

/* Launch conversion of isomorphic runs.  A recorded payload may have
   further executions that are not identical to it, typically the last
   copy of a fully unrolled loop, where register allocation chose
   different registers.  If such a run matches the payload under a
   register mapping, it can be replaced with a launch, provided every
   register whose final value differs between the two is dead after the
   run.

   Insns are matched in order: same insn codes and non-register operands,
   with register operands related by a value map built as the match
   proceeds (a use of a run-local definition must map to the matching
   definition; a live-in use must be the same hard register).

   The conversion is refused if the run's trailing Dst advance differs
   from that of the payload's other execution sites (the Dst
   auto-increment pass needs them all alike), or if the payload's
   contents, buffer span or sites are ambiguous, or if the recording does
   not dominate the run.  */

struct conv_capture
{
  rtx_insn *insn = nullptr;
  basic_block bb = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;
  bool valid = true;
  std::vector<rtx_insn *> members; /* slot-occupying payload insns, in order */
  rtx_insn *shadow_end = nullptr;  /* last member */
  unsigned sites = 0;
  int trailing = -2;               /* uniform site context; -2 unset, -1 none */
};

struct conv_launch
{
  rtx_insn *insn;
  unsigned begin;
  unsigned len;
  conv_capture *payload = nullptr;
};

/* Return true if INSN is a TTINCRWC that advances only Dst by a constant,
   using the same test as the Dst auto-increment pass.  */

static bool
conv_pure_dst_increment_p (rtx_insn *insn, HOST_WIDE_INT *stride)
{
  if (GET_CODE (insn) != INSN
      || recog_memoized (insn) != CODE_FOR_rvtt_ttincrwc)
    return false;
  rtx pattern = PATTERN (insn);
  rtx cr = XVECEXP (pattern, 0, 0);
  rtx d = XVECEXP (pattern, 0, 1);
  rtx b = XVECEXP (pattern, 0, 2);
  rtx a = XVECEXP (pattern, 0, 3);
  if (!CONST_INT_P (cr) || !CONST_INT_P (d) || !CONST_INT_P (b)
      || !CONST_INT_P (a))
    return false;
  if (INTVAL (cr) != 0 || INTVAL (b) != 0 || INTVAL (a) != 0)
    return false;
  *stride = INTVAL (d);
  return *stride > 0 && *stride <= 15;
}

/* Return the stride of a pure Dst TTINCRWC immediately following LAST,
   or -1 if there is none.  */

static int
conv_trailing_context (rtx_insn *last)
{
  basic_block bb = BLOCK_FOR_INSN (last);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (rtx_insn *cur = NEXT_INSN (last); cur && cur != end;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      HOST_WIDE_INT stride;
      if (conv_pure_dst_increment_p (cur, &stride))
	return (int) stride;
      return -1;
    }
  return -1;
}

/* Return true if INSN may appear in a matched run: replay-safe,
   non-empty, fixed-encoding Tensix work.  */

static bool
conv_run_insn_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX
      || get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || !get_attr_length (insn))
    return false;
  return rvtt_fixed_replay_rtx_p (PATTERN (insn));
}

/* Return true if X contains a variable-LREG read or write pattern.
   These access a specific physical register outside the normal
   dataflow, so they count as uses of every register.  */

static bool
conv_mentions_varlreg_p (const_rtx x)
{
  if (GET_CODE (x) == UNSPEC_VOLATILE && XINT (x, 1) == UNSPECV_SFPVARLREG)
    return true;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
    if (fmt[i] == 'e')
      {
	if (conv_mentions_varlreg_p (XEXP (x, i)))
	  return true;
      }
    else if (fmt[i] == 'E')
      for (int j = XVECLEN (x, i); j--;)
	if (conv_mentions_varlreg_p (XVECEXP (x, i, j)))
	  return true;
  return false;
}

/* Return true if the value of hard register REGNO after FROM in BB may
   be used: scan the rest of BB and then every successor path, stopping
   a path at a full redefinition, and treating a call or a variable-LREG
   marker as a use.  The exit block is not a use: SFPU registers carry
   no values across function boundaries except through explicit
   instructions, and kernels clobber them freely.  */

static bool
conv_reg_consumed_after_p (unsigned regno, rtx_insn *from, basic_block bb)
{
  rtx reg = regno_reg_rtx[regno];
  rtx_insn *stop = NEXT_INSN (BB_END (bb));
  for (rtx_insn *cur = NEXT_INSN (from); cur && cur != stop;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      if (CALL_P (cur) || conv_mentions_varlreg_p (PATTERN (cur))
	  || reg_referenced_p (reg, PATTERN (cur)))
	return true;
      if (reg_set_p (reg, cur))
	return false;
    }

  auto_bitmap visited;
  std::vector<basic_block> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    work.push_back (e->dest);
  while (!work.empty ())
    {
      basic_block cur_bb = work.back ();
      work.pop_back ();
      if (cur_bb == EXIT_BLOCK_PTR_FOR_FN (cfun)
	  || !bitmap_set_bit (visited, cur_bb->index))
	continue;
      bool killed = false;
      rtx_insn *insn;
      FOR_BB_INSNS (cur_bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn) || conv_mentions_varlreg_p (PATTERN (insn))
	      || reg_referenced_p (reg, PATTERN (insn)))
	    return true;
	  if (reg_set_p (reg, insn))
	    {
	      killed = true;
	      break;
	    }
	}
      if (!killed)
	FOR_EACH_EDGE (e, ei, cur_bb->succs)
	  work.push_back (e->dest);
    }
  return false;
}

/* Register correspondence between a payload and a run.  DEFINED_P and
   DEFINED_R are the registers defined so far in each; P2R and R2P map
   between them; PAIRS lists every definition pair, for the liveness
   check.  */

struct conv_map
{
  std::map<unsigned, unsigned> p2r, r2p;
  std::vector<std::pair<unsigned, unsigned>> pairs;
  std::map<unsigned, bool> defined_p, defined_r;
};

/* Return true if payload rtx A matches run rtx B under MAP.  IN_DEF is
   true in a SET_DEST or CLOBBER, where SFPU register pairs are added to
   PENDING_DEFS rather than MAP.  A register use must agree with MAP; a
   live-in use must be the same hard register and not redefined in the
   run.  Everything else must match exactly.  */

static bool
conv_match_rtx (rtx a, rtx b, bool in_def, conv_map &map,
		std::vector<std::pair<unsigned, unsigned>> &pending_defs)
{
  if (GET_CODE (a) != GET_CODE (b) || GET_MODE (a) != GET_MODE (b))
    return false;
  switch (GET_CODE (a))
    {
    case REG:
      {
	if (REG_NREGS (a) != 1 || REG_NREGS (b) != 1)
	  return false;
	unsigned pa = REGNO (a), rb = REGNO (b);
	if (!SFPU_REG_P (pa) || !SFPU_REG_P (rb))
	  return false;
	if (in_def)
	  {
	    pending_defs.emplace_back (pa, rb);
	    return true;
	  }
	if (map.defined_p.count (pa))
	  return map.p2r.count (pa) && map.p2r[pa] == rb
		 && map.r2p.count (rb) && map.r2p[rb] == pa;
	/* Live-in use: the identical register, not shadowed by a run-local
	   definition.  */
	return pa == rb && !map.defined_r.count (rb);
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return conv_match_rtx (SET_SRC (a), SET_SRC (b), false, map,
			     pending_defs)
	     && conv_match_rtx (SET_DEST (a), SET_DEST (b), true, map,
				pending_defs);

    case CLOBBER:
      return conv_match_rtx (XEXP (a, 0), XEXP (b, 0), true, map,
			     pending_defs);

    case USE:
      return conv_match_rtx (XEXP (a, 0), XEXP (b, 0), false, map,
			     pending_defs);

    case UNSPEC:
    case UNSPEC_VOLATILE:
      if (XINT (a, 1) != XINT (b, 1))
	return false;
      /* FALLTHROUGH */
    case PARALLEL:
      {
	if (XVECLEN (a, 0) != XVECLEN (b, 0))
	  return false;
	for (int ix = 0; ix != XVECLEN (a, 0); ++ix)
	  if (!conv_match_rtx (XVECEXP (a, 0, ix), XVECEXP (b, 0, ix),
			       in_def, map, pending_defs))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Return true if payload insn P matches run insn R under MAP: same insn
   code and matching patterns.  On success, add the insn's register
   definitions to MAP.  */

static bool
conv_match_insn (rtx_insn *p, rtx_insn *r, conv_map &map)
{
  if (recog_memoized (p) != recog_memoized (r))
    return false;
  std::vector<std::pair<unsigned, unsigned>> pending_defs;
  if (!conv_match_rtx (PATTERN (p), PATTERN (r), false, map, pending_defs))
    return false;
  /* Definitions take effect after all of the instruction's uses.  */
  for (auto const &def : pending_defs)
    {
      map.p2r[def.first] = def.second;
      map.r2p[def.second] = def.first;
      map.defined_p[def.first] = true;
      map.defined_r[def.second] = true;
      map.pairs.push_back (def);
    }
  return true;
}

/* Find the captures and launches of CFN and replace each run that
   matches a recorded payload (see above) with a launch of it.  Add
   changed blocks to DIRTY_BBS.  Give up entirely if any capture's slots
   cannot be determined.  */

static void
convert_isomorphic_runs (function *cfn, bitmap dirty_bbs)
{
  /* Rediscover captures and launches structurally.  */
  std::vector<conv_capture *> captures;
  std::vector<conv_launch> launches;
  std::set<rtx_insn *> shadow;
  bool bail = false;

  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (bail)
	    break;
	  if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN)
	    continue;
	  if (recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
	    continue;
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx begin = XVECEXP (pattern, 0, 5);
	  rtx exec = XVECEXP (pattern, 0, 6);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (begin)
	      || !CONST_INT_P (exec) || !CONST_INT_P (load))
	    {
	      bail = true; /* variable replay: buffer contents unprovable */
	      break;
	    }
	  if (INTVAL (load) == 0)
	    {
	      launches.push_back ({ insn, (unsigned) UINTVAL (begin),
				    (unsigned) UINTVAL (len), nullptr });
	      continue;
	    }
	  conv_capture *cap = new conv_capture;
	  cap->insn = insn;
	  cap->bb = bb;
	  if (bitmap_bit_p (dirty_bbs, bb->index))
	    /* A user recording may be open around this capture.  */
	    cap->valid = false;
	  cap->begin = UINTVAL (begin);
	  cap->len = UINTVAL (len);
	  cap->exec = INTVAL (exec) != 0;
	  unsigned remaining = cap->len;
	  rtx_insn *cur = insn;
	  rtx_insn *bb_end = NEXT_INSN (BB_END (bb));
	  while (remaining)
	    {
	      cur = NEXT_INSN (cur);
	      if (!cur || cur == bb_end)
		{
		  cap->valid = false;
		  break;
		}
	      if (!NONDEBUG_INSN_P (cur))
		continue;
	      shadow.insert (cur);
	      /* The recorded insns must all be ones a run could match.  */
	      if (!conv_run_insn_p (cur))
		{
		  cap->valid = false;
		  continue;
		}
	      cap->members.push_back (cur);
	      cap->shadow_end = cur;
	      --remaining;
	    }
	  if (cap->valid && cap->members.size () != cap->len)
	    cap->valid = false;
	  captures.push_back (cap);
	  insn = (cur && cur != bb_end) ? cur : BB_END (bb);
	}
      if (bail)
	break;
    }

  if (!bail)
    {
      /* Captures with overlapping spans are all invalid, and each
	 launch must match exactly one capture.  */
      auto overlap = [] (unsigned b0, unsigned l0, unsigned b1, unsigned l1)
      { return b0 < b1 + l1 && b1 < b0 + l0; };
      for (conv_capture *cap : captures)
	for (conv_capture *other : captures)
	  if (other != cap
	      && overlap (cap->begin, cap->len, other->begin, other->len))
	    cap->valid = false;
      for (conv_launch &launch : launches)
	{
	  for (conv_capture *cap : captures)
	    if (cap->begin == launch.begin && cap->len == launch.len)
	      launch.payload = launch.payload ? nullptr : cap;
	  for (conv_capture *cap : captures)
	    if (overlap (cap->begin, cap->len, launch.begin, launch.len)
		&& !(cap->begin == launch.begin && cap->len == launch.len))
	      cap->valid = false;
	}

      /* All execution sites must have the same trailing Dst advance.  */
      for (conv_capture *cap : captures)
	if (cap->valid)
	  {
	    if (cap->exec)
	      {
		cap->trailing = conv_trailing_context (cap->shadow_end);
		++cap->sites;
	      }
	    for (conv_launch &launch : launches)
	      if (launch.payload == cap)
		{
		  int ctx = conv_trailing_context (launch.insn);
		  if (cap->trailing == -2)
		    cap->trailing = ctx;
		  else if (cap->trailing != ctx)
		    cap->valid = false;
		  ++cap->sites;
		}
	    if (cap->sites == 0)
	      cap->valid = false;
	  }
    }

  bool any_valid = false;
  for (conv_capture *cap : captures)
    any_valid |= cap->valid;

  if (!bail && any_valid)
    {
      calculate_dominance_info (CDI_DOMINATORS);

      FOR_EACH_BB_FN (bb, cfn)
	{
	  if (bitmap_bit_p (dirty_bbs, bb->index))
	    /* A user recording may be open here.  */
	    continue;
	  rtx_insn *stop = NEXT_INSN (BB_END (bb));
	  rtx_insn *insn = BB_HEAD (bb);
	  while (insn && insn != stop)
	    {
	      rtx_insn *next = NEXT_INSN (insn);
	      if (!NONDEBUG_INSN_P (insn) || shadow.count (insn)
		  || !conv_run_insn_p (insn))
		{
		  insn = next;
		  continue;
		}

	      conv_capture *matched = nullptr;
	      conv_map map;
	      rtx_insn *run_last = nullptr;
	      for (conv_capture *cap : captures)
		{
		  if (!cap->valid)
		    continue;
		  /* The recording must reach this run on every path.  */
		  if (cap->bb == bb)
		    {
		      /* Same block: the shadow must precede the run.  */
		      bool before = false;
		      for (rtx_insn *probe = cap->shadow_end; probe;
			   probe = NEXT_INSN (probe))
			{
			  if (probe == insn)
			    {
			      before = true;
			      break;
			    }
			  if (probe == BB_END (bb))
			    break;
			}
		      if (!before)
			continue;
		    }
		  else if (!dominated_by_p (CDI_DOMINATORS, bb, cap->bb))
		    continue;

		  conv_map trial;
		  rtx_insn *cur = insn;
		  rtx_insn *bb_end = NEXT_INSN (BB_END (bb));
		  unsigned matched_len = 0;
		  while (matched_len != cap->len)
		    {
		      if (!cur || cur == bb_end)
			break;
		      if (!NONDEBUG_INSN_P (cur))
			{
			  cur = NEXT_INSN (cur);
			  continue;
			}
		      if (shadow.count (cur) || !conv_run_insn_p (cur)
			  || !conv_match_insn (cap->members[matched_len],
					       cur, trial))
			break;
		      ++matched_len;
		      if (matched_len == cap->len)
			{
			  run_last = cur;
			  break;
			}
		      cur = NEXT_INSN (cur);
		    }
		  if (matched_len == cap->len)
		    {
		      matched = cap;
		      map = trial;
		      break;
		    }
		}

	      if (!matched)
		{
		  insn = next;
		  continue;
		}

	      /* The launch delivers the recorded words, not the run's own,
		 so in reform_mode refuse runs with carried Dst
		 accesses.  */
	      if (reform_mode)
		{
		  bool carried = false;
		  for (rtx_insn *m : matched->members)
		    if (rvtt_dst_autoincr_carried_access_p (m))
		      {
			carried = true;
			break;
		      }
		  if (!carried)
		    for (rtx_insn *cur = insn; cur; cur = NEXT_INSN (cur))
		      {
			if (NONDEBUG_INSN_P (cur)
			    && rvtt_dst_autoincr_carried_access_p (cur))
			  {
			    carried = true;
			    break;
			  }
			if (cur == run_last)
			  break;
		      }
		  if (carried)
		    {
		      rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_ISOMORPHIC_CONVERSION_UNPROVEN, dump_file,
				   "Not converting isomorphic run at insn %d:"
				   " post-autoincr-window-carried-isomorphic-"
				   "conversion-unproven (renamed delivery of a"
				   " carried access)\n", INSN_UID (insn));
		      insn = next;
		      continue;
		    }
		}

	      /* Same trailing Dst advance as the other sites.  */
	      if (conv_trailing_context (run_last) != matched->trailing)
		{
		  if (dump_file)
		    fprintf (dump_file, "Not converting isomorphic run at "
			     "insn %d: trailing Dst-advance context differs "
			     "from the payload's other execution sites\n",
			     INSN_UID (insn));
		  insn = next;
		  continue;
		}

	      /* Registers of differing definition pairs must be dead after
		 the run: the launch writes the payload's registers, not the
		 run's.  */
	      bool live_conflict = false;
	      for (auto const &pair : map.pairs)
		if (pair.first != pair.second
		    && (conv_reg_consumed_after_p (pair.first, run_last, bb)
			|| conv_reg_consumed_after_p (pair.second, run_last,
						      bb)))
		  live_conflict = true;
	      if (live_conflict)
		{
		  if (dump_file)
		    fprintf (dump_file, "Not converting isomorphic run at "
			     "insn %d: renamed register consumed after the "
			     "run\n", INSN_UID (insn));
		  insn = next;
		  continue;
		}

	      /* Convert: one launch replaces the whole run.  */
	      rtx replay = gen_rvtt_ttreplay_int
		(const0_rtx, const0_rtx, const0_rtx, GEN_INT (matched->len),
		 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (matched->begin),
		 const0_rtx, const0_rtx);
	      emit_insn_before (replay, insn);
	      if (dump_file)
		{
		  fprintf (dump_file, "Converted isomorphic run of %u insns "
			   "(bb %d) to launch [%u,+%u); renamed pairs:",
			   matched->len, bb->index, matched->begin,
			   matched->len);
		  bool any = false;
		  for (auto const &pair : map.pairs)
		    if (pair.first != pair.second)
		      {
			fprintf (dump_file, " %u->%u", pair.first,
				 pair.second);
			any = true;
		      }
		  fprintf (dump_file, any ? "\n" : " none\n");
		}
	      rtx_insn *cur = insn;
	      next = NEXT_INSN (run_last);
	      while (cur != run_last)
		{
		  rtx_insn *after = NEXT_INSN (cur);
		  if (NONDEBUG_INSN_P (cur))
		    SET_INSN_DELETED (cur);
		  cur = after;
		}
	      SET_INSN_DELETED (run_last);
	      insn = next;
	    }
	}
      free_dominance_info (CDI_DOMINATORS);
    }

  for (conv_capture *cap : captures)
    delete cap;
}

/* Counted-row formation (-mtt-tensix-optimize-counted-row-formation).
   Sequence discovery needs identical words, so it cannot capture
   template-expanded rows that differ only in (a) per-row immediate loads
   and (b) a rotation of registers by the allocator.  This phase rewrites
   such families of rows into identical words, so that the ordinary
   discovery, allocation and replacement then form one row program:

   1. An insn whose instances differ only in constant operands is moved
      out of the row, to the head of its clone, where it is issued between
      launches.  Only a single-slot immediate load qualifies: its only
      SFPU dataflow is its one destination register (possibly read-modify-
      write) and it has no CC, Dst, RWC or configuration effect.

   2. Each clone is matched against the first one under a register value
      map, as in the launch conversion, and every value whose register
      differs is renamed (definition and all uses) to the recorded
      register, after an in-block value analysis checks that this is
      safe.  A live-in value held by an instruction with several fixed
      outputs is copied with an all-lanes move before the launch.

   3. Candidates are ranked by slot saving; a family whose row is longer
      than the largest free span is refused.  User-recorded slots are
      never taken.

   Moves may not reopen a delay shadow that the nop inserter resolved for
   the original order, since that gives wrong code on hardware (see the
   stall-detection errata in SFPMAD.md).  Each failure is reported under
   a counted-row-* refusal name.  */

/* xtt_effect_set masks cover L0..L15; the allocatable registers are
   L0..L7.  */
static inline uint32_t
crf_reg_bit (unsigned regno)
{
  gcc_checking_assert (SFPU_REG_P (regno));
  return 1u << (regno - SFPU_REG_FIRST);
}

/* Order BRIDGES, (destination, source) pairs checked as one parallel
   copy, so that issuing them as single moves never overwrites a
   register that a later move reads.  Return false if the copy has a
   cycle, which would need a scratch register.  */

static bool
crf_order_bridges (std::vector<std::pair<unsigned, unsigned>> &bridges)
{
  std::vector<std::pair<unsigned, unsigned>> left = bridges, done;
  while (!left.empty ())
    {
      unsigned pick = left.size ();
      for (unsigned i = 0; i != left.size () && pick == left.size (); ++i)
	{
	  bool read_later = false;
	  for (unsigned j = 0; j != left.size (); ++j)
	    if (j != i && left[j].second == left[i].first)
	      read_later = true;
	  if (!read_later)
	    pick = i;
	}
      if (pick == left.size ())
	return false;
      done.push_back (left[pick]);
      left.erase (left.begin () + pick);
    }
  bridges = done;
  return true;
}

/* Return true if INSN may be moved out of a row (see step 1 above): every
   non-register operand is constant, its only SFPU dataflow is its single
   destination register, and it has no CC, Dst, RWC or configuration
   effect.  */

static bool
crf_excludable_insn_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX
      || get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || get_attr_length (insn) != 4
      || !rvtt_fixed_replay_rtx_p (pattern))
    return false;

  /* A lane-gated write (cc_read) is fine, since the insn only moves
     across code without CC writes; a CC write is not.  */
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque || e.cc_write
      || e.config_dests_written || e.config_dests_read
      || e.addr_mod_slot_write
      || e.rwc.kind != xtt_rwc_effect_t::NONE
      || e.dst_mem_read || e.dst_mem_write)
    return false;

  uint32_t w = e.lreg_write & 0xFF;
  if (popcount_hwi (w) != 1)
    return false;
  if ((e.lreg_write & ~0xFFu) || (e.lreg_read & ~(uint32_t) w))
    return false;
  return true;
}

/* Classified SFPU register mentions of an rtx, by pattern position.  */

static void
crf_scan_rtx (rtx x, bool in_def, uint32_t *defs, uint32_t *uses,
	      bool *unhandled)
{
  switch (GET_CODE (x))
    {
    case REG:
      if (SFPU_REG_P (REGNO (x)))
	{
	  if (REG_NREGS (x) != 1)
	    *unhandled = true;
	  else
	    *(in_def ? defs : uses) |= crf_reg_bit (REGNO (x));
	}
      return;

    case SET:
      crf_scan_rtx (SET_SRC (x), false, defs, uses, unhandled);
      crf_scan_rtx (SET_DEST (x), true, defs, uses, unhandled);
      return;

    case CLOBBER:
      crf_scan_rtx (XEXP (x, 0), true, defs, uses, unhandled);
      return;

    case USE:
      crf_scan_rtx (XEXP (x, 0), false, defs, uses, unhandled);
      return;

    case MEM:
      /* Address registers are uses even under a store destination.  */
      crf_scan_rtx (XEXP (x, 0), false, defs, uses, unhandled);
      return;

    case SUBREG:
    case STRICT_LOW_PART:
    case ZERO_EXTRACT:
      /* Partial or indirect register access: not a whole-value def/use.  */
      {
	uint32_t d = 0, u = 0;
	crf_scan_rtx (XEXP (x, 0), false, &d, &u, unhandled);
	if (u | d)
	  *unhandled = true;
      }
      return;

    default:
      {
	const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
	for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
	  if (fmt[i] == 'e')
	    crf_scan_rtx (XEXP (x, i), in_def, defs, uses, unhandled);
	  else if (fmt[i] == 'E')
	    for (int j = XVECLEN (x, i); j--;)
	      crf_scan_rtx (XVECEXP (x, i, j), in_def, defs, uses, unhandled);
      }
      return;
    }
}

/* One whole-register value in the linear in-block dataflow model.  */

struct crf_value
{
  int def_pos = -1;		 /* -1: live into the block */
  rtx_insn *def_insn = nullptr;
  unsigned reg = 0;		 /* original hard register */
  std::vector<unsigned> use_positions;
  int last_pos = -1;
  bool live_out = false;	 /* consumed on some path after the block */
  bool fixed = false;		 /* multi-definition or hidden-effect def */
  bool poisoned = false;	 /* range crosses an opaque event or shadow */
  int renamed_to = -1;		 /* planned final hard register (-1 = keep) */
};

/* Per-position facts for the whole block.  */

struct crf_position
{
  rtx_insn *insn;
  uint32_t defs = 0, uses = 0;	 /* SFPU register masks */
  bool eligible = false;	 /* may be a family member */
  bool excludable = false;	 /* may be moved out of the row */
  bool barrier = false;		 /* breaks family runs */
  bool empty = false;		 /* zero-length marker: transparent */
  bool cc_write = false;
  bool opaque = false;
  uint32_t marker_mask = 0;	 /* zero-length LREG interface marker */
  unsigned phash = 0;		 /* parameterized structural hash */
  int value_of_def[8];		 /* value index defined per reg, or -1 */
  int value_of_use[8];		 /* value index consumed per reg, or -1 */
  crf_position () { memset (value_of_def, -1, sizeof (value_of_def));
		    memset (value_of_use, -1, sizeof (value_of_use)); }
};

struct crf_block
{
  basic_block bb;
  std::vector<crf_position> pos;
  std::vector<crf_value> values;
};

/* Hash of an insn's code and structure with SFPU register numbers
   ignored, and with constants also ignored for insns that can be moved
   out of the row (whose constants are not recorded).  */

static unsigned
crf_param_hash (rtx_insn *insn, bool excludable)
{
  auto hasher = [excludable] (auto &self, unsigned hash, rtx rtl) -> unsigned
  {
    hash = crc32_unsigned (hash, GET_CODE (rtl) + (GET_MODE (rtl) << 16));
    switch (GET_CODE (rtl))
      {
      case UNSPEC:
      case UNSPEC_VOLATILE:
	hash = crc32_unsigned (hash, XINT (rtl, 1));
	/* FALLTHROUGH */
      case PARALLEL:
	{
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
	if (!SFPU_REG_P (REGNO (rtl)))
	  hash = crc32_unsigned (hash, REGNO (rtl));
	break;

      case CONST_INT:
	if (!excludable)
	  hash = crc32_unsigned (hash, unsigned (INTVAL (rtl)));
	break;

      case CLOBBER:
      case USE:
	hash = self (self, hash, XEXP (rtl, 0));
	break;

      default:
	break;
      }
    return hash;
  };
  return hasher (hasher, recog_memoized (insn), PATTERN (insn));
}

/* Build the value model and per-position facts for BB.  Return false if
   BB cannot be modelled (it has a variable-length user capture).  */

static bool
crf_scan_block (basic_block bb, crf_block &blk)
{
  blk.bb = bb;
  blk.pos.clear ();
  blk.values.clear ();

  int cur[8];
  for (int i = 0; i != 8; ++i)
    cur[i] = -1;

  unsigned shadow = 0;

  auto value_at = [&] (unsigned regix, unsigned pos_ix) -> int
  {
    if (cur[regix] < 0)
      {
	/* Live into the block.  */
	blk.values.emplace_back ();
	crf_value &v = blk.values.back ();
	v.reg = SFPU_REG_FIRST + regix;
	v.def_pos = -1;
	cur[regix] = int (blk.values.size ()) - 1;
      }
    crf_value &v = blk.values[cur[regix]];
    v.use_positions.push_back (pos_ix);
    v.last_pos = int (pos_ix);
    return cur[regix];
  };

  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      blk.pos.emplace_back ();
      crf_position &p = blk.pos.back ();
      unsigned pos_ix = blk.pos.size () - 1;
      p.insn = insn;

      bool opaque_event = false;
      bool in_shadow = shadow > 0;

      if (CALL_P (insn) || GET_CODE (insn) == JUMP_INSN)
	opaque_event = CALL_P (insn);
      else if (asm_noperands (PATTERN (insn)) >= 0
	       || (GET_CODE (insn) == INSN && recog_memoized (insn) < 0))
	opaque_event = true;

      rtx pattern = PATTERN (insn);
      bool unhandled = false;
      if (!opaque_event)
	crf_scan_rtx (pattern, false, &p.defs, &p.uses, &unhandled);
      if (unhandled)
	opaque_event = true;

      bool tensix = GET_CODE (insn) == INSN && !opaque_event
	&& recog_memoized (insn) >= 0
	&& GET_CODE (pattern) != USE && GET_CODE (pattern) != CLOBBER
	&& get_attr_type (insn) == TYPE_TENSIX;

      uint32_t fixed_extra = 0;
      if (tensix)
	{
	  replay_span span;
	  auto rtype = get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER
	    ? is_replay_insn (span, insn) : REPLAY_none;
	  if (rtype != REPLAY_none)
	    {
	      if (rtype == REPLAY_variable_capture)
		return false;
	      if (rtype == REPLAY_fixed_capture)
		shadow = span.end;
	      p.barrier = true;
	    }
	  else
	    {
	      xtt_effect_set e = rvtt_insn_effects (insn);
	      if (e.opaque)
		opaque_event = true;
	      else
		{
		  p.cc_write = e.cc_write;
		  /* Registers accessed beyond those in the pattern are
		     treated as fixed def and use.  */
		  fixed_extra = ((e.lreg_write | e.lreg_read) & 0xFF)
		    & ~(p.defs | p.uses);
		  p.defs |= fixed_extra;
		  p.uses |= fixed_extra;
		}
	      p.empty = !get_attr_length (insn);
	      if (p.empty)
		rvtt_lreg_marker (insn, &p.marker_mask);
	      if (in_shadow && !p.empty)
		shadow--;
	    }
	}
      p.opaque = opaque_event;

      if (opaque_event)
	{
	  /* Unknown reads and writes: poison every live value, extend
	     their ranges, and act as a run barrier.  */
	  for (int i = 0; i != 8; ++i)
	    if (cur[i] >= 0)
	      {
		blk.values[cur[i]].poisoned = true;
		blk.values[cur[i]].use_positions.push_back (pos_ix);
		blk.values[cur[i]].last_pos = int (pos_ix);
	      }
	  p.barrier = true;
	  continue;
	}

      /* Uses consume the current values.  */
      for (int i = 0; i != 8; ++i)
	if (p.uses & (1u << i))
	  p.value_of_use[i] = value_at (i, pos_ix);

      /* Definitions begin new values.  */
      unsigned ndefs = popcount_hwi (p.defs);
      for (int i = 0; i != 8; ++i)
	if (p.defs & (1u << i))
	  {
	    blk.values.emplace_back ();
	    crf_value &v = blk.values.back ();
	    v.reg = SFPU_REG_FIRST + i;
	    v.def_pos = int (pos_ix);
	    v.def_insn = insn;
	    v.last_pos = int (pos_ix);
	    v.fixed = ndefs > 1 || (fixed_extra & (1u << i));
	    v.poisoned = in_shadow || (fixed_extra & (1u << i));
	    cur[i] = int (blk.values.size ()) - 1;
	    p.value_of_def[i] = cur[i];
	  }

      if (in_shadow)
	{
	  /* Values used by a user capture payload may be used by its
	     launches, which we cannot see.  */
	  for (int i = 0; i != 8; ++i)
	    if (p.value_of_use[i] >= 0)
	      blk.values[p.value_of_use[i]].poisoned = true;
	  p.barrier = true;
	  continue;
	}

      if (p.barrier || p.empty)
	continue;

      p.excludable = crf_excludable_insn_p (insn);
      if (p.excludable)
	/* Ignored by discovery; attached to its clone later.  */
	continue;
      p.eligible = conv_run_insn_p (insn);
      if (p.eligible)
	p.phash = crf_param_hash (insn, false);
      else
	p.barrier = true;
    }

  /* Values still live at the block's end that some path consumes.  */
  for (int i = 0; i != 8; ++i)
    if (cur[i] >= 0)
      blk.values[cur[i]].live_out
	= conv_reg_consumed_after_p (SFPU_REG_FIRST + i, BB_END (bb), bb);

  return !blk.pos.empty ();
}

/* One clone of a row family: its span of positions and the register map
   relating it to the family's first clone.  */

struct crf_clone
{
  unsigned begin, end;		 /* half-open position span */
  std::map<unsigned, unsigned> p2r, r2p; /* seed reg <-> clone reg */
  std::map<unsigned, bool> defined_p, defined_r;
};

struct crf_seq
{
  unsigned parent = 0;
  unsigned hash = 0;
  unsigned length = 0;		 /* members (excludable/empty not counted) */
  std::vector<crf_clone> clones; /* clones[0] is the seed */
};

/* Like conv_match_rtx, but a live-in register pair may differ: it is
   recorded in MAP as a register to rename instead of failing.  */

static bool
crf_match_rtx (rtx a, rtx b, bool in_def, crf_clone &map,
	       std::vector<std::pair<unsigned, unsigned>> &pending_defs)
{
  if (GET_CODE (a) != GET_CODE (b) || GET_MODE (a) != GET_MODE (b))
    return false;
  switch (GET_CODE (a))
    {
    case REG:
      {
	if (REG_NREGS (a) != 1 || REG_NREGS (b) != 1)
	  return false;
	unsigned pa = REGNO (a), rb = REGNO (b);
	if (!SFPU_REG_P (pa) || !SFPU_REG_P (rb))
	  return pa == rb;
	if (in_def)
	  {
	    pending_defs.emplace_back (pa, rb);
	    return true;
	  }
	if (map.defined_p.count (pa) || map.defined_r.count (rb))
	  /* Run-local value: must be the corresponding definition.  */
	  return map.defined_p.count (pa) && map.defined_r.count (rb)
	    && map.p2r.count (pa) && map.p2r[pa] == rb
	    && map.r2p.count (rb) && map.r2p[rb] == pa;
	/* Live-in use: record the (possibly differing) correspondence.  */
	auto pi = map.p2r.find (pa), ri = map.r2p.find (rb);
	if (pi != map.p2r.end () || ri != map.r2p.end ())
	  return pi != map.p2r.end () && pi->second == rb
	    && ri != map.r2p.end () && ri->second == pa;
	map.p2r[pa] = rb;
	map.r2p[rb] = pa;
	return true;
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return crf_match_rtx (SET_SRC (a), SET_SRC (b), false, map,
			    pending_defs)
	&& crf_match_rtx (SET_DEST (a), SET_DEST (b), true, map,
			  pending_defs);

    case CLOBBER:
      return crf_match_rtx (XEXP (a, 0), XEXP (b, 0), true, map,
			    pending_defs);

    case USE:
    case MEM:
      return crf_match_rtx (XEXP (a, 0), XEXP (b, 0), false, map,
			    pending_defs);

    case UNSPEC:
    case UNSPEC_VOLATILE:
      if (XINT (a, 1) != XINT (b, 1))
	return false;
      /* FALLTHROUGH */
    case PARALLEL:
      {
	if (XVECLEN (a, 0) != XVECLEN (b, 0))
	  return false;
	for (int ix = 0; ix != XVECLEN (a, 0); ++ix)
	  if (!crf_match_rtx (XVECEXP (a, 0, ix), XVECEXP (b, 0, ix),
			      in_def, map, pending_defs))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Like conv_match_insn, using crf_match_rtx.  */

static bool
crf_match_insn (rtx_insn *p, rtx_insn *r, crf_clone &map)
{
  if (recog_memoized (p) != recog_memoized (r))
    return false;
  std::vector<std::pair<unsigned, unsigned>> pending_defs;
  if (!crf_match_rtx (PATTERN (p), PATTERN (r), false, map, pending_defs))
    return false;
  for (auto const &def : pending_defs)
    {
      map.p2r[def.first] = def.second;
      map.r2p[def.second] = def.first;
      map.defined_p[def.first] = true;
      map.defined_r[def.second] = true;
    }
  return true;
}

/* Return true if P is skipped when forming rows: a zero-length marker
   or an insn that can be moved out of the row (attached to its clone
   later).  */

static inline bool
crf_transparent_p (crf_position const &p)
{
  return p.empty || p.excludable;
}

/* Like extend_sequence, but using crf_match_insn instead of word
   equality.  */

static void
crf_extend (std::map<unsigned, std::vector<unsigned>> &map,
	    std::vector<crf_seq> &list, crf_block &blk,
	    unsigned parent, unsigned length, unsigned begin, unsigned end)
{
  crf_position &p = blk.pos[end - 1];
  unsigned hash = parent ? crc32_unsigned (list[parent].hash, p.phash)
    : p.phash;

  auto slot = map.emplace (hash, std::vector<unsigned> ());
  for (auto ix : slot.first->second)
    {
      if (list[ix].parent != parent)
	continue;
      /* A joining clone matches all members from scratch against the
         sequence's seed.  */
      crf_clone cand;
      cand.begin = begin;
      cand.end = end;
      bool ok = true;
      crf_seq &seq0 = list[ix];
      unsigned spos = seq0.clones.front ().begin;
      unsigned cpos = begin;
      for (unsigned member = 0; member != length; ++member)
	{
	  while (crf_transparent_p (blk.pos[spos]))
	    ++spos;
	  while (crf_transparent_p (blk.pos[cpos]))
	    ++cpos;
	  if (!crf_match_insn (blk.pos[spos].insn, blk.pos[cpos].insn,
			       cand))
	    {
	      ok = false;
	      break;
	    }
	  ++spos;
	  ++cpos;
	}
      if (!ok)
	continue;
      if (begin <= seq0.clones.back ().begin)
	continue;
      seq0.clones.push_back (std::move (cand));
      return;
    }

  slot.first->second.push_back (unsigned (list.size ()));
  list.emplace_back ();
  crf_seq &seq = list.back ();
  seq.parent = parent;
  seq.hash = hash;
  seq.length = length;
  crf_clone seed;
  seed.begin = begin;
  seed.end = end;
  /* Matching the seed with itself sets up the identity map.  */
  {
    unsigned spos = begin;
    for (unsigned member = 0; member != length; ++member)
      {
	while (crf_transparent_p (blk.pos[spos]))
	  ++spos;
	crf_match_insn (blk.pos[spos].insn, blk.pos[spos].insn, seed);
	++spos;
      }
  }
  seq.clones.push_back (std::move (seed));
}

/* Find row families in BLK and add them to LIST, like build_sequences:
   seed a length-1 sequence at each eligible position and grow sequences
   with several clones one member at a time, up to MAX_RESIDUAL members.
   Skipped positions are stepped over; a barrier or ineligible position
   ends a clone.  LIST[0] is the null sequence.  */

static void
crf_build_sequences (std::vector<crf_seq> &list, crf_block &blk,
		     unsigned max_residual)
{
  list.clear ();
  list.push_back (crf_seq ()); /* null */
  std::map<unsigned, std::vector<unsigned>> map;

  unsigned n = blk.pos.size ();
  for (unsigned ix = 0; ix != n; ++ix)
    {
      crf_position &p = blk.pos[ix];
      if (p.barrier || crf_transparent_p (p) || !p.eligible)
	continue;
      crf_extend (map, list, blk, 0, 1, ix, ix + 1);
    }

  unsigned from = 1, length = 1;
  unsigned max_length = max_residual;
  while (length++ < max_length)
    {
      map.clear ();
      unsigned seq_end = list.size ();
      for (unsigned seq_ix = from; seq_ix != seq_end; ++seq_ix)
	{
	  if (list[seq_ix].clones.size () == 1)
	    continue;
	  for (unsigned clone_ix = 0,
		 clone_end = list[seq_ix].clones.size ();
	       clone_ix != clone_end; ++clone_ix)
	    {
	      unsigned span_begin = list[seq_ix].clones[clone_ix].begin;
	      unsigned span_end = list[seq_ix].clones[clone_ix].end;
	    skip_transparent:
	      if (span_end >= blk.pos.size ())
		continue;
	      if (crf_transparent_p (blk.pos[span_end]))
		{
		  span_end++;
		  goto skip_transparent;
		}
	      crf_position &nxt = blk.pos[span_end];
	      if (nxt.barrier || !nxt.eligible)
		continue;
	      crf_extend (map, list, blk, seq_ix, length, span_begin,
			  span_end + 1);
	    }
	}
      from = seq_end;
    }
}

/* A verified family: the renames, bridges and moves to apply.  */

struct crf_plan
{
  unsigned residual = 0;		   /* recorded slot words (= members) */
  int saving = 0;			   /* modeled issued-slot saving */
  std::vector<crf_clone> clones;	   /* surviving, disjoint */
  /* Per clone: member positions, in step with the seed's.  */
  std::vector<std::vector<unsigned>> members;
  /* Value index -> final hard register.  */
  std::map<unsigned, unsigned> renames;
  /* Value index -> clone that required the rename (-1 for a rename
     forced by another).  */
  std::map<unsigned, int> rename_source;
  /* Per clone: bridge moves (dest <- src) at the clone head.  */
  std::vector<std::vector<std::pair<unsigned, unsigned>>> bridges;
  /* Per clone: moved positions inside the span, to the head (used by a
     member) or the tail (used only later).  */
  std::vector<std::vector<unsigned>> moves_head;
  std::vector<std::vector<unsigned>> moves_tail;
};

/* Return true if seed rtx A and clone rtx B are equal under the final
   register assignment in PLAN.  Unlike crf_match_rtx, which records
   live-in differences, this requires them to be resolved, since the
   recorded words are replayed for every clone.  */

static bool
crf_final_lockstep_rtx (rtx a, rtx b, bool in_def,
			const crf_position &pa, const crf_position &pb,
			const crf_plan &plan, const crf_block &blk,
			const std::vector<std::pair<unsigned, unsigned>>
			  &bridges)
{
  if (!a || !b)
    return a == b;
  if (GET_CODE (a) != GET_CODE (b) || GET_MODE (a) != GET_MODE (b))
    return false;
  switch (GET_CODE (a))
    {
    case REG:
      {
	if (REG_NREGS (a) != 1 || REG_NREGS (b) != 1)
	  return false;
	unsigned ra = REGNO (a), rb = REGNO (b);
	if (!SFPU_REG_P (ra) || !SFPU_REG_P (rb))
	  return ra == rb;
	auto final_of = [&] (const crf_position &p, unsigned r) -> unsigned
	{
	  int bit = int (r) - SFPU_REG_FIRST;
	  int vix = in_def ? p.value_of_def[bit] : p.value_of_use[bit];
	  if (vix < 0)
	    return r;
	  auto it = plan.renames.find (unsigned (vix));
	  return it != plan.renames.end () ? it->second
					   : blk.values[vix].reg;
	};
	unsigned fa = final_of (pa, ra), fb = final_of (pb, rb);
	if (fa == fb)
	  return true;
	/* A bridge at the clone head copies a live-in value into the
	   seed's register, so the clone may still have it in its own.
	   Definitions are never bridged.  */
	if (!in_def)
	  for (auto const &br : bridges)
	    if (br.first == fa && br.second == fb)
	      return true;
	return false;
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return crf_final_lockstep_rtx (SET_SRC (a), SET_SRC (b), false,
				     pa, pb, plan, blk, bridges)
	&& crf_final_lockstep_rtx (SET_DEST (a), SET_DEST (b), true,
				   pa, pb, plan, blk, bridges);

    case CLOBBER:
      return crf_final_lockstep_rtx (XEXP (a, 0), XEXP (b, 0), true,
				     pa, pb, plan, blk, bridges);

    case USE:
    case MEM:
      return crf_final_lockstep_rtx (XEXP (a, 0), XEXP (b, 0), false,
				     pa, pb, plan, blk, bridges);

    case UNSPEC:
    case UNSPEC_VOLATILE:
      if (XINT (a, 1) != XINT (b, 1))
	return false;
      /* FALLTHROUGH */
    case PARALLEL:
      {
	if (XVECLEN (a, 0) != XVECLEN (b, 0))
	  return false;
	for (int ix = 0; ix != XVECLEN (a, 0); ++ix)
	  if (!crf_final_lockstep_rtx (XVECEXP (a, 0, ix),
				       XVECEXP (b, 0, ix), in_def,
				       pa, pb, plan, blk, bridges))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Store in OUT the positions of the members of clone C in BLK, in
   order.  Fewer than LENGTH entries means the clone is incomplete.  */

static void
crf_clone_members (crf_block const &blk, crf_clone const &c, unsigned length,
		   std::vector<unsigned> &out)
{
  out.clear ();
  for (unsigned pos = c.begin; pos != c.end && out.size () != length; ++pos)
    if (!crf_transparent_p (blk.pos[pos]) && blk.pos[pos].eligible
	&& !blk.pos[pos].barrier)
      out.push_back (pos);
}

/* The final register of a value under the plan.  */

static inline unsigned
crf_final_reg (crf_block const &blk, crf_plan const &plan, int vix)
{
  auto it = plan.renames.find (vix);
  return it != plan.renames.end () ? it->second : blk.values[vix].reg;
}

/* Final-assignment def/use register masks of the instruction at POS.  */

static void
crf_final_masks (crf_block const &blk, crf_plan const &plan, unsigned pos,
		 uint32_t *defs, uint32_t *uses)
{
  crf_position const &p = blk.pos[pos];
  *defs = 0;
  *uses = 0;
  for (int i = 0; i != 8; ++i)
    {
      if (p.value_of_def[i] >= 0)
	*defs |= crf_reg_bit (crf_final_reg (blk, plan, p.value_of_def[i]));
      if (p.value_of_use[i] >= 0)
	*uses |= crf_reg_bit (crf_final_reg (blk, plan, p.value_of_use[i]));
    }
  *uses |= p.marker_mask & 0xFF;
}

/* Return true if the moved insn at POS can go to the clone head (before
   ANCHOR) or tail (after LAST) under the final register assignment,
   checking dependences against every insn crossed; crossing a CC write
   would change its lane gating.  Positions in SKIP move with it.  */

static bool
crf_move_ok (crf_block const &blk, crf_plan const &plan, unsigned pos,
	     unsigned from, unsigned to,
	     std::vector<unsigned> const &skip)
{
  uint32_t mdefs, muses;
  crf_final_masks (blk, plan, pos, &mdefs, &muses);
  for (unsigned ix = from; ix != to; ++ix)
    {
      if (ix == pos)
	continue;
      if (std::find (skip.begin (), skip.end (), ix) != skip.end ())
	continue;
      crf_position const &q = blk.pos[ix];
      if (q.empty && !q.marker_mask)
	continue;
      if (q.opaque || q.cc_write)
	return false;
      uint32_t qdefs, quses;
      crf_final_masks (blk, plan, ix, &qdefs, &quses);
      if ((qdefs & (mdefs | muses)) || (quses & mdefs))
	return false;
    }
  return true;
}


static bool crf_occupancy_ok (crf_block &blk, crf_plan &plan,
			      int *conflict_a = nullptr,
			      int *conflict_b = nullptr);

/* Return the final insn order of PLAN over BLK as indices into blk.pos;
   a negative entry -1 - C stands for the next bridge move of clone C.
   Head moves and then bridges go just before a clone's first member,
   tail moves just after its last member, and other positions stay in
   block order.  Both crf_shadow_contract_ok and crf_apply use this
   order.  */

static std::vector<int>
crf_plan_order (crf_block &blk, crf_plan &plan)
{
  unsigned n = blk.pos.size ();
  std::vector<char> is_moved (n, 0);
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      for (unsigned pos : plan.moves_head[c])
	is_moved[pos] = 1;
      for (unsigned pos : plan.moves_tail[c])
	is_moved[pos] = 1;
    }

  std::vector<int> order;
  order.reserve (n + 8);
  std::map<unsigned, unsigned> anchor_clone, tail_clone;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      anchor_clone[plan.members[c].front ()] = c;
      tail_clone[plan.members[c].back ()] = c;
    }
  for (unsigned pos = 0; pos != n; ++pos)
    {
      auto ac = anchor_clone.find (pos);
      if (ac != anchor_clone.end ())
	{
	  for (unsigned m : plan.moves_head[ac->second])
	    order.push_back (int (m));
	  for (unsigned b = 0; b != plan.bridges[ac->second].size (); ++b)
	    order.push_back (-1 - int (ac->second));	/* bridge of clone */
	}
      if (!is_moved[pos])
	order.push_back (int (pos));
      auto tc = tail_clone.find (pos);
      if (tc != tail_clone.end ())
	for (unsigned m : plan.moves_tail[tc->second])
	  order.push_back (int (m));
    }
  return order;
}

/* Return true if the final order of PLAN keeps every XTT_DELAY
   requirement satisfied.  The nop inserter (rtl-rvtt-schedule.cc) checks
   them against the original order: a consumer affected by a stall
   erratum (xtt_dynamic_bug; see the "automatic stalling does not detect"
   list in SFPMAD.md) directly after a dynamic-delay producer gets an
   SFPNOP, but a pair separated by another word needs none.  Moving an
   insn out of such a gap would reopen the hazard, e.g. moving an SFPLOADI
   from between an SFPMUL and an SFPSWAP lets the SFPSWAP read a stale
   result.  So recheck each adjacent pair in the final order with the
   inserter's test, using the final registers.  A barrier or opaque
   position ends a segment.  Renames preserve values, so they cannot add
   dependences.  */

static bool
crf_shadow_contract_ok (crf_block &blk, crf_plan &plan)
{
  /* The same order crf_apply produces.  */
  std::vector<int> order = crf_plan_order (blk, plan);

  unsigned bug_mask = TARGET_XTT_TENSIX_BH ? XTT_DYNAMIC_BUG_BH
    : TARGET_XTT_TENSIX_QSR ? XTT_DYNAMIC_BUG_QSR : 0;

  int prod = -1;		/* order entry of the open delay producer */
  unsigned bridge_ix = 0;	/* running index into the clone's bridges */
  int bridge_clone = -1;
  for (unsigned oi = 0; oi != order.size (); ++oi)
    {
      int e = order[oi];
      uint32_t cuses;
      bool slot_word, is_nop, is_bridge = e < 0;
      rtx_insn *cinsn = nullptr;
      if (is_bridge)
	{
	  /* A bridge is an all-lanes move that has not been emitted yet,
	     so it has no erratum attribute: treat it conservatively.  */
	  unsigned c = unsigned (-1 - e);
	  if (bridge_clone != int (c))
	    {
	      bridge_clone = int (c);
	      bridge_ix = 0;
	    }
	  cuses = crf_reg_bit (plan.bridges[c][bridge_ix++].second);
	  slot_word = true;
	  is_nop = false;
	}
      else
	{
	  crf_position const &q = blk.pos[e];
	  if (q.barrier || q.opaque
	      || GET_CODE (q.insn) != INSN
	      || recog_memoized (q.insn) < 0
	      || get_attr_type (q.insn) != TYPE_TENSIX)
	    {
	      prod = -1;	/* conservative segment end */
	      continue;
	    }
	  cinsn = q.insn;
	  uint32_t cdefs;
	  crf_final_masks (blk, plan, unsigned (e), &cdefs, &cuses);
	  slot_word = get_attr_length (cinsn) != 0;
	  is_nop = recog_memoized (cinsn) == CODE_FOR_rvtt_sfpnop;
	}

      if (prod >= 0)
	{
	  rtx_insn *pinsn = blk.pos[order[prod]].insn;
	  bool hazard;
	  if (get_attr_xtt_delay (pinsn) == XTT_DELAY_STATIC)
	    hazard = !is_nop;
	  else
	    {
	      uint32_t pdefs, puses;
	      crf_final_masks (blk, plan, unsigned (order[prod]),
			       &pdefs, &puses);
	      hazard = (pdefs & cuses) != 0;
	      if (hazard && bug_mask && !is_bridge)
		hazard = (bug_mask & get_attr_xtt_dynamic_bug (cinsn)) != 0;
	    }
	  if (hazard)
	    {
	      rvtt_refuse (RVTT_REF_COUNTED_ROW_VACATED_DELAY_SHADOW, dump_file,
			   "Refusing counted-row family [%u,%u):"
			   " counted-row-vacated-delay-shadow: the final"
			   " order puts %s insn %d in insn %d's undischarged"
			   " delay shadow (erratum consumer; SFPMAD.md"
			   " stall-detection bug list)\n",
			   plan.clones.front ().begin,
			   plan.clones.front ().end,
			   is_bridge ? "bridge for" : "dependent",
			   is_bridge ? INSN_UID (blk.pos[order[prod]].insn)
				     : INSN_UID (cinsn),
			   INSN_UID (pinsn));
	      return false;
	    }
	}

      if (!slot_word)
	continue;		/* zero-length marker: the shadow stays open */
      prod = (!is_bridge && get_attr_xtt_delay (cinsn) != XTT_DELAY_NONE)
	? int (oi) : -1;
    }
  return true;
}

/* Return true if INSN's definition of REG is tied to a source by a
   matching constraint, so that both share one encoded register field and
   a rename must keep the previous value.  */

static bool
crf_tied_rmw_p (rtx_insn *insn, unsigned reg)
{
  extract_insn_cached (insn);
  int dest_op = -1;
  for (int i = 0; i < recog_data.n_operands; ++i)
    if (recog_data.operand_type[i] != OP_IN
	&& REG_P (recog_data.operand[i])
	&& REGNO (recog_data.operand[i]) == reg)
      {
	if (recog_data.operand_type[i] == OP_INOUT)
	  return true;
	dest_op = i;
      }
  if (dest_op < 0)
    return false;
  for (int i = 0; i < recog_data.n_operands; ++i)
    {
      if (i == dest_op || !REG_P (recog_data.operand[i])
	  || REGNO (recog_data.operand[i]) != reg)
	continue;
      for (const char *c = recog_data.constraints[i]; *c;)
	if (ISDIGIT (*c))
	  {
	    char *end;
	    if (strtol (c, &end, 10) == dest_op)
	      return true;
	    c = end;
	  }
	else
	  ++c;
    }
  return false;
}

/* Verify one candidate family and build its PLAN.  Return true on
   success; on failure report the named refusal.  */

static bool
crf_verify_family (crf_block &blk, crf_seq &seq, unsigned budget,
		   bool sticky, crf_plan &plan, unsigned ref)
{
  unsigned length = seq.length;

  /* Clones are in ascending order; keep a maximal disjoint set.  */
  plan.clones.clear ();
  {
    unsigned bound = 0;
    for (auto const &c : seq.clones)
      {
	if (plan.clones.size () && c.begin < bound)
	  continue;
	plan.clones.push_back (c);
	bound = c.end;
      }
  }
  if (plan.clones.size () < 2 || ref >= plan.clones.size ())
    return false;

  plan.residual = length;
  if (length < MIN_SEQUENCE)
    return false;
  if (length > budget)
    {
      rvtt_refuse (RVTT_REF_COUNTED_ROW_SLOT_BUDGET, dump_file,
		   "Refusing counted-row family [%u,%u):"
		   " counted-row-slot-budget: residual %u exceeds the largest"
		   " available replay span %u\n",
		   plan.clones.front ().begin, plan.clones.front ().end,
		   length, budget);
      return false;
    }

  plan.members.assign (plan.clones.size (), {});
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      crf_clone_members (blk, plan.clones[c], length, plan.members[c]);
      if (plan.members[c].size () != length)
	return false;
    }

  /* Companion soundness under possible index tracking; multi-result
     insns are left to the exact-match formation.  */
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    for (unsigned m = 0; m != length; ++m)
      {
	crf_position const &mp = blk.pos[plan.members[c][m]];
	rtx_insn *insn = mp.insn;
	xtt_effect_set e = rvtt_insn_effects (insn);
	xtt_multiresult_group group;
	if (rvtt_multiresult_group (insn, e, &group))
	  {
	    rvtt_refuse (RVTT_REF_COUNTED_ROW_RESIDUAL_NOT_UNIFORM, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " counted-row-residual-not-uniform: member %u is a"
			 " multi-result instruction\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
	/* Rename planning needs a single definition register per member;
	   refuse members that define several (a two-register SFPSWAP, or
	   extra registers treated as fixed def and use).  */
	if (popcount_hwi (mp.defs) > 1)
	  {
	    rvtt_refuse (RVTT_REF_COUNTED_ROW_MULTIDEF_MEMBER, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " counted-row-multidef-member: member %u defines"
			 " multiple registers\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
	if (sticky && (e.lreg_write & 0xF))
	  {
	    rvtt_refuse (RVTT_REF_SHADOW_STATE_UNPROVED, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " shadow-state-unproved: member %u writes the value"
			 " bank under possibly-enabled index tracking\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
      }

  /* Plan the renames.  When a value has a definition and a live-in use
     that need different registers, SEED_REMAP retargets the seed's own
     definition at member M and planning restarts.  A failure specific to
     one clone drops that clone; failures on the seed side, or a remap
     that does not converge, refuse the family.  */
  plan.bridges.assign (plan.clones.size (), {});
  plan.moves_head.assign (plan.clones.size (), {});
  plan.moves_tail.assign (plan.clones.size (), {});
  std::map<unsigned, unsigned> seed_remap; /* member index -> target reg */

  auto seed_def_reg = [&] (unsigned m) -> int
  {
    auto it = seed_remap.find (m);
    if (it != seed_remap.end ())
      return int (it->second);
    crf_position const &p = blk.pos[plan.members[ref][m]];
    if (!p.defs)
      return -1;
    gcc_assert (popcount_hwi (p.defs) == 1);
    return SFPU_REG_FIRST + exact_log2 (p.defs);
  };

  unsigned iter_limit = 8 + unsigned (plan.clones.size ());
  int drop_clone = -1;
  for (unsigned iter = 0; ; ++iter)
    {
      if (drop_clone >= 0)
	{
	  gcc_assert (unsigned (drop_clone) != ref);
	  plan.clones.erase (plan.clones.begin () + drop_clone);
	  plan.members.erase (plan.members.begin () + drop_clone);
	  plan.bridges.erase (plan.bridges.begin () + drop_clone);
	  plan.moves_head.erase (plan.moves_head.begin () + drop_clone);
	  plan.moves_tail.erase (plan.moves_tail.begin () + drop_clone);
	  if (unsigned (drop_clone) < ref)
	    --ref;
	  drop_clone = -1;
	  if (plan.clones.size () < 2)
	    return false;
	}
      if (iter == iter_limit)
	{
	  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
		       "Refusing counted-row family [%u,%u):"
		       " counted-row-map-live-out: chain closure did not"
		       " converge\n", plan.clones.front ().begin,
		       plan.clones.front ().end);
	  return false;
	}

      plan.renames.clear ();
      plan.rename_source.clear ();
      for (auto &b : plan.bridges)
	b.clear ();
      bool remapped = false;
      bool refused = false;
      /* For each renamed value, the member whose definition required
	 it.  */
      std::map<unsigned, unsigned> plan_def_member;

      uint32_t seed_writes = 0;
      for (unsigned m = 0; m != length; ++m)
	{
	  int r = seed_def_reg (m);
	  if (r >= 0)
	    seed_writes |= crf_reg_bit (r);
	}

      /* Rename value VIX0 to TARGET.  If its definition is
	 read-modify-write, the previous value of the register must move
	 too, since both share one encoded field.  Return 1 on success, 0
	 on a conflict with the plan, -1 if the value cannot be
	 renamed.  */
      int planning_clone = -1;
      auto plan_rename = [&] (int vix0, unsigned target) -> int
      {
	std::vector<int> work (1, vix0);
	while (!work.empty ())
	  {
	    int vix = work.back ();
	    work.pop_back ();
	    crf_value const &v = blk.values[vix];
	    if (v.reg == target && !plan.renames.count (vix))
	      continue;
	    auto it = plan.renames.find (vix);
	    if (it != plan.renames.end ())
	      {
		if (it->second == target)
		  continue;
		return 0;
	      }
	    if (v.fixed || v.poisoned || v.live_out || v.def_pos < 0)
	      return -1;
	    plan.renames[vix] = target;
	    plan.rename_source[vix] = planning_clone;
	    /* The definition reads the previous value through a tied
	       operand.  */
	    crf_position const &dp = blk.pos[v.def_pos];
	    int bit = int (v.reg) - SFPU_REG_FIRST;
	    if (dp.value_of_use[bit] >= 0
		&& crf_tied_rmw_p (v.def_insn, v.reg))
	      work.push_back (dp.value_of_use[bit]);
	  }
	return 1;
      };

      for (unsigned c = 0;
	   c != plan.clones.size () && !refused && drop_clone < 0
	   && !remapped; ++c)
	{
	  if (c == ref)
	    continue;
	  planning_clone = int (c);
	  crf_clone map;
	  for (unsigned m = 0;
	       m != length && !refused && drop_clone < 0 && !remapped; ++m)
	    {
	      unsigned spos = plan.members[ref][m];
	      unsigned cpos = plan.members[c][m];
	      std::map<unsigned, unsigned> before_r2p = map.r2p;
	      if (!crf_match_insn (blk.pos[spos].insn, blk.pos[cpos].insn,
				   map))
		{
		  drop_clone = int (c);
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RESIDUAL_NOT_UNIFORM,
			       dump_file,
			       "Dropping counted-row clone"
			       " [%u,%u): counted-row-residual-not-uniform:"
			       " diverges at member %u\n",
			       plan.clones[c].begin, plan.clones[c].end, m);
		  break;
		}

	      crf_position const &cp = blk.pos[cpos];

	      /* Live-in pairs first seen at this member.  */
	      for (auto const &pr : map.r2p)
		{
		  if (map.defined_r.count (pr.first))
		    continue;
		  if (before_r2p.count (pr.first))
		    continue;
		  unsigned t = pr.first, s = pr.second;
		  if (t == s)
		    continue;
		  int ci = int (t) - SFPU_REG_FIRST;
		  int vu = cp.value_of_use[ci];
		  if (vu < 0)
		    continue;
		  crf_value const &v = blk.values[vu];
		  if (!v.fixed && !v.poisoned && !v.live_out
		      && v.def_pos >= 0)
		    {
		      int rr = plan_rename (vu, s);
		      if (rr == 1)
			continue;
		      if (rr == 0)
			{
			  /* Definition and live-in need different
			     registers: retarget the seed definition and
			     replan.  */
			  auto dm = plan_def_member.find (vu);
			  if (dm != plan_def_member.end ())
			    {
			      seed_remap[dm->second] = s;
			      remapped = true;
			      break;
			    }
			}
		      drop_clone = int (c);
		      rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
				   "Dropping counted-row clone"
				   " [%u,%u): counted-row-map-live-out:"
				   " live-in value in r%u cannot move to"
				   " r%u\n", plan.clones[c].begin,
				   plan.clones[c].end, t, s);
		      break;
		    }
		  /* A live-in that cannot be renamed is copied with an
		     all-lanes move, unless the recorded words overwrite
		     the source while it is still needed.  */
		  bool used_later = false;
		  for (unsigned up : v.use_positions)
		    if (up >= plan.clones[c].end)
		      used_later = true;
		  if (v.live_out)
		    used_later = true;
		  if (used_later && (seed_writes & crf_reg_bit (t)))
		    {
		      drop_clone = int (c);
		      rvtt_refuse (RVTT_REF_COUNTED_ROW_BRIDGE_CLOBBER,
				   dump_file,
				   "Dropping counted-row clone"
				   " [%u,%u): counted-row-bridge-clobber:"
				   " r%u is written by the recorded program"
				   " but consumed after the clone\n",
				   plan.clones[c].begin,
				   plan.clones[c].end, t);
		      break;
		    }
		  plan.bridges[c].emplace_back (s, t);
		}
	      if (refused || drop_clone >= 0 || remapped)
		break;

	      /* Definitions.  */
	      if (cp.defs)
		{
		  gcc_assert (popcount_hwi (cp.defs) == 1);
		  int ci = exact_log2 (cp.defs);
		  int vd = cp.value_of_def[ci];
		  gcc_assert (vd >= 0);
		  crf_value const &v = blk.values[vd];
		  int want = seed_def_reg (m);
		  gcc_assert (want >= 0);
		  auto it = plan.renames.find (vd);
		  if (it != plan.renames.end ()
		      && int (it->second) != want)
		    {
		      /* A live-in use needs a different register:
			 retarget the seed definition and replan.  */
		      seed_remap[m] = it->second;
		      remapped = true;
		      break;
		    }
		  if (int (v.reg) != want || it != plan.renames.end ())
		    {
		      if (v.fixed || v.poisoned || v.live_out)
			{
			  drop_clone = int (c);
			  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT,
				       dump_file,
				       "Dropping counted-row"
				       " clone [%u,%u):"
				       " counted-row-map-live-out: pinned or"
				       " live-out definition of r%u at"
				       " member %u\n", plan.clones[c].begin,
				       plan.clones[c].end, v.reg, m);
			  break;
			}
		      int rr = plan_rename (vd, want);
		      if (rr != 1)
			{
			  drop_clone = int (c);
			  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT,
				       dump_file,
				       "Dropping counted-row"
				       " clone [%u,%u):"
				       " counted-row-map-live-out:"
				       " definition of r%u at member %u"
				       " cannot move to r%u\n",
				       plan.clones[c].begin,
				       plan.clones[c].end, v.reg, m, want);
			  break;
			}
		    }
		  if (!plan_def_member.count (vd))
		    plan_def_member[vd] = m;
		}
	    }
	}

      /* Bridges are plain all-lanes moves.  With possible index
	 tracking a move into the value bank also moves the companion,
	 and a cyclic set of moves cannot be ordered.  */
      if (!remapped && drop_clone < 0 && !refused)
	for (unsigned c = 0; c != plan.clones.size () && drop_clone < 0; ++c)
	  {
	    bool value_bank = false;
	    for (auto const &br : plan.bridges[c])
	      if (crf_reg_bit (br.first) & 0xF)
		value_bank = true;
	    if (sticky && value_bank)
	      {
		drop_clone = int (c);
		rvtt_refuse (RVTT_REF_SHADOW_STATE_UNPROVED, dump_file,
			     "Dropping counted-row clone [%u,%u):"
			     " shadow-state-unproved: a bridge writes the"
			     " value bank under possibly-enabled index"
			     " tracking\n", plan.clones[c].begin,
			     plan.clones[c].end);
	      }
	    else if (!crf_order_bridges (plan.bridges[c]))
	      {
		drop_clone = int (c);
		rvtt_refuse (RVTT_REF_COUNTED_ROW_BRIDGE_CLOBBER, dump_file,
			     "Dropping counted-row clone [%u,%u):"
			     " counted-row-bridge-clobber: the bridges form a"
			     " register cycle\n", plan.clones[c].begin,
			     plan.clones[c].end);
	      }
	  }

      /* SEED_REMAP entries rename the seed's own definitions.  */
      planning_clone = -1;
      if (!remapped && drop_clone < 0 && !refused)
	for (auto const &sr : seed_remap)
	  {
	    crf_position const &p = blk.pos[plan.members[ref][sr.first]];
	    int vd = p.value_of_def[exact_log2 (p.defs)];
	    gcc_assert (vd >= 0);
	    if (plan_rename (vd, sr.second) != 1)
	      {
		rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
			     "Refusing counted-row family"
			     " [%u,%u): counted-row-map-live-out: seed chain"
			     " closure needs an unrenameable value\n",
			     plan.clones.front ().begin,
			     plan.clones.front ().end);
		return false;
	      }
	  }

      if (!remapped && drop_clone < 0 && !refused)
	{
	  /* An excluded insn whose value is used by a clone member moves
	     to that clone's head; one inside a span whose value is not used
	     in the family moves past the tail.  The uses must follow the
	     new position and no CC write or opaque insn may be crossed;
	     the occupancy simulation below has the final say.  */
	  for (auto &mh : plan.moves_head)
	    mh.clear ();
	  for (auto &mt : plan.moves_tail)
	    mt.clear ();

	  /* Clone of the first member using an excluded insn's value,
	     following chains through other excluded insns.  */
	  std::map<unsigned, int> consumer_clone; /* position -> clone or -1 */
	  std::vector<unsigned> excl_all;
	  for (unsigned pos = 0; pos != blk.pos.size (); ++pos)
	    if (blk.pos[pos].excludable)
	      excl_all.push_back (pos);
	  std::map<unsigned, unsigned> member_clone; /* member pos -> clone */
	  for (unsigned c = 0; c != plan.clones.size (); ++c)
	    for (unsigned m = 0; m != length; ++m)
	      member_clone[plan.members[c][m]] = c;
	  for (auto pit = excl_all.rbegin (); pit != excl_all.rend ();
	       ++pit)
	    {
	      unsigned pos = *pit;
	      crf_position const &p = blk.pos[pos];
	      int cc = -1;
	      for (int i = 0; i != 8 && cc < 0; ++i)
		{
		  if (p.value_of_def[i] < 0)
		    continue;
		  for (unsigned up
			 : blk.values[p.value_of_def[i]].use_positions)
		    {
		      auto mi = member_clone.find (up);
		      if (mi != member_clone.end ())
			{
			  cc = int (mi->second);
			  break;
			}
		      auto ei = consumer_clone.find (up);
		      if (ei != consumer_clone.end () && ei->second >= 0)
			{
			  cc = ei->second;
			  break;
			}
		    }
		}
	      consumer_clone[pos] = cc;
	    }

	  /* No CC write or opaque insn in [LO, HI).  */
	  auto move_window_ok = [&] (unsigned lo, unsigned hi) -> bool
	  {
	    for (unsigned ix = lo; ix < hi; ++ix)
	      if (blk.pos[ix].cc_write || blk.pos[ix].opaque)
		return false;
	    return true;
	  };

	  bool moves_ok = true;
	  for (unsigned pos : excl_all)
	    {
	      int cc = consumer_clone[pos];
	      if (cc >= 0)
		{
		  unsigned anchor = plan.members[cc].front ();
		  if (pos < anchor)
		    {
		      /* Already just before the clone: leave it.  */
		      bool clean = true;
		      for (unsigned ix = pos + 1; ix != anchor; ++ix)
			if (!crf_transparent_p (blk.pos[ix]))
			  clean = false;
		      if (clean)
			continue;
		    }
		  /* All uses must follow the new position; excluded insns
		     moving to the same head keep their order.  */
		  crf_position const &p = blk.pos[pos];
		  bool uses_ok = true;
		  for (int i = 0; i != 8; ++i)
		    if (p.value_of_def[i] >= 0)
		      for (unsigned up
			     : blk.values[p.value_of_def[i]].use_positions)
			if (up < anchor
			    && !(blk.pos[up].excludable
				 && consumer_clone.count (up)
				 && consumer_clone[up] == cc))
			  uses_ok = false;
		  unsigned lo = MIN (pos, anchor), hi = MAX (pos, anchor);
		  if (!uses_ok || !move_window_ok (lo, hi))
		    {
		      if (unsigned (cc) == ref)
			{
			  rvtt_refuse
			    (RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			     dump_file,
			     "Refusing counted-row"
			     " family [%u,%u):"
			     " counted-row-excluded-member-"
			     "unmovable: insn %d cannot reach its"
			     " consumer clone\n",
			     plan.clones.front ().begin,
			     plan.clones.front ().end,
			     INSN_UID (blk.pos[pos].insn));
			  return false;
			}
		      rvtt_refuse
			(RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			 dump_file,
			 "Dropping counted-row clone"
			 " [%u,%u):"
			 " counted-row-excluded-member-unmovable:"
			 " insn %d cannot reach its consumer"
			 " clone\n", plan.clones[cc].begin,
			 plan.clones[cc].end,
			 INSN_UID (blk.pos[pos].insn));
		      drop_clone = cc;
		      moves_ok = false;
		      break;
		    }
		  plan.moves_head[cc].push_back (pos);
		  continue;
		}
	      /* Not used in the family: if inside a clone, move it past
		 the tail.  */
	      for (unsigned c = 0; c != plan.clones.size (); ++c)
		if (pos > plan.members[c].front ()
		    && pos < plan.members[c].back ())
		  {
		    unsigned last = plan.members[c].back ();
		    crf_position const &p = blk.pos[pos];
		    bool uses_ok = true;
		    for (int i = 0; i != 8; ++i)
		      if (p.value_of_def[i] >= 0)
			for (unsigned up
			       : blk.values[p.value_of_def[i]]
				   .use_positions)
			  if (up <= last)
			    uses_ok = false;
		    if (!uses_ok || !move_window_ok (pos, last + 1))
		      {
			if (c == ref)
			  return false;
			rvtt_refuse
			  (RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			   dump_file,
			   "Dropping counted-row clone"
			   " [%u,%u):"
			   " counted-row-excluded-member-"
			   "unmovable: insn %d cannot move past"
			   " the tail\n", plan.clones[c].begin,
			   plan.clones[c].end,
			   INSN_UID (blk.pos[pos].insn));
			drop_clone = int (c);
			moves_ok = false;
		      }
		    else
		      plan.moves_tail[c].push_back (pos);
		    break;
		  }
	      if (!moves_ok)
		break;
	    }
	  (void) moves_ok;
	}

      /* Check register occupancy under the final assignment.  A conflict
	 with an unrelated renameable value is resolved by moving that
	 value into the vacated register; otherwise drop the clone that
	 required the conflicting rename.  */
      if (!remapped && drop_clone < 0 && !refused)
	{
	  bool occupancy = false;
	  for (unsigned swap = 0; swap != 64; ++swap)
	    {
	      int a = -2, b = -2;
	      if (crf_occupancy_ok (blk, plan, &a, &b))
		{
		  occupancy = true;
		  break;
		}
	      int renamed = -1, bystander = -1;
	      if (a >= 0 && plan.renames.count (a)
		  && b >= 0 && !plan.renames.count (b))
		{
		  renamed = a;
		  bystander = b;
		}
	      else if (b >= 0 && plan.renames.count (b)
		       && a >= 0 && !plan.renames.count (a))
		{
		  renamed = b;
		  bystander = a;
		}
	      if (renamed >= 0)
		{
		  crf_value const &bv = blk.values[bystander];
		  if (!bv.fixed && !bv.poisoned && !bv.live_out
		      && bv.def_pos >= 0)
		    {
		      plan.renames[bystander] = blk.values[renamed].reg;
		      plan.rename_source[bystander] = -1;
		      if (dump_file)
			fprintf (dump_file, "counted-row bystander swap:"
				 " value of r%u (insn %d) -> r%u\n",
				 bv.reg, INSN_UID (bv.def_insn),
				 blk.values[renamed].reg);
		      continue;
		    }
		}
	      /* Not cascadable: drop the responsible clone.  */
	      int src = -1;
	      for (int v : { a, b })
		if (v >= 0 && plan.rename_source.count (v)
		    && plan.rename_source[v] >= 0)
		  src = plan.rename_source[v];
	      if (src >= 0 && unsigned (src) != ref)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Dropping counted-row clone"
			       " [%u,%u): counted-row-rename-interference:"
			       " unresolvable occupancy conflict\n",
			       plan.clones[src].begin,
			       plan.clones[src].end);
		  drop_clone = src;
		}
	      break;
	    }
	  if (drop_clone < 0 && !occupancy)
	    return false;
	}

      if (drop_clone >= 0 || remapped)
	continue;
      if (refused)
	return false;
      break;
    }

  // The occupancy step may have renamed further values after the match,
  // so recheck every member pair under the final assignment: any
  // remaining difference would be wrong code.
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (c == ref)
	continue;
      for (unsigned m = 0; m != length; ++m)
	{
	  unsigned spos = plan.members[ref][m];
	  unsigned cpos = plan.members[c][m];
	  if (!crf_final_lockstep_rtx (PATTERN (blk.pos[spos].insn),
				       PATTERN (blk.pos[cpos].insn),
				       false, blk.pos[spos],
				       blk.pos[cpos], plan, blk,
				       plan.bridges[c]))
	    {
	      if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_FINAL_LOCKSTEP_DIVERGENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-final-lockstep-"
			       "divergence: member %u of clone %u diverges"
			       " from the seed under the final register"
			       " assignment\n",
			       plan.clones.front ().begin,
			       plan.clones.front ().end, m, c);
		  fprintf (dump_file, "  seed (ref %u):  ", ref);
		  print_rtl_single (dump_file, blk.pos[spos].insn);
		  fprintf (dump_file, "  clone %u: ", c);
		  print_rtl_single (dump_file, blk.pos[cpos].insn);
		  for (auto const &rn : plan.renames)
		    fprintf (dump_file, "  rename: val %u (r%u, insn %d)"
			     " -> r%u\n", rn.first,
			     blk.values[rn.first].reg,
			     blk.values[rn.first].def_insn
			     ? INSN_UID (blk.values[rn.first].def_insn)
			     : -1, rn.second);
		}
	      return false;
	    }
	}
    }

  /* Saving: each clone but the first becomes one launch; each bridge
     and the capture word cost a slot.  */
  {
    int bridges_total = 0;
    for (auto const &b : plan.bridges)
      bridges_total += b.size ();
    plan.saving = int (plan.clones.size () - 1) * int (length - 1)
      - bridges_total - 1;
  }
  if (plan.saving < 1)
    {
      if (dump_file)
	fprintf (dump_file, "Not canonicalizing counted-row family"
		 " [%u,%u): modeled saving %d\n",
		 plan.clones.front ().begin, plan.clones.front ().end,
		 plan.saving);
      return false;
    }

  /* A plan that changes nothing is left to the exact-match formation.  */
  {
    bool any_change = !plan.renames.empty ();
    for (unsigned c = 0; c != plan.clones.size () && !any_change; ++c)
      any_change = !plan.moves_head[c].empty ()
	|| !plan.moves_tail[c].empty () || !plan.bridges[c].empty ();
    if (!any_change)
      return false;
  }

  // The moves and bridges must not reopen a delay hazard.
  if (!crf_shadow_contract_ok (blk, plan))
    return false;

  return true;
}

/* Return true if the final register assignment of PLAN has no
   conflicting live ranges over BLK in the final order, and no CC write
   falls within the range of any rewritten value, so the rewrites are
   correct whatever the lane state.  */

static bool
crf_occupancy_ok (crf_block &blk, crf_plan &plan,
		  int *conflict_a, int *conflict_b)
{
  unsigned n = blk.pos.size ();

  std::vector<int> time (n, -1);
  std::vector<char> is_moved (n, 0);
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      for (unsigned pos : plan.moves_head[c])
	is_moved[pos] = 1;
      for (unsigned pos : plan.moves_tail[c])
	is_moved[pos] = 1;
    }

  std::vector<int> bridge_time (plan.clones.size (), -1);
  std::map<unsigned, unsigned> anchor_clone, tail_clone;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      anchor_clone[plan.members[c].front ()] = c;
      tail_clone[plan.members[c].back ()] = c;
    }

  int t = 0;
  for (unsigned pos = 0; pos != n; ++pos)
    {
      auto ac = anchor_clone.find (pos);
      if (ac != anchor_clone.end ())
	{
	  unsigned c = ac->second;
	  for (unsigned mpos : plan.moves_head[c])
	    time[mpos] = t++;
	  bridge_time[c] = t++;
	}
      if (!is_moved[pos])
	time[pos] = t++;
      auto tc = tail_clone.find (pos);
      if (tc != tail_clone.end ())
	for (unsigned mpos : plan.moves_tail[tc->second])
	  time[mpos] = t++;
    }

  struct interval { long start, end; int val; };
  std::vector<std::vector<interval>> per_reg (8);

  /* Uses of a bridged value inside the bridging clone become uses of
     the bridge.  */
  std::map<unsigned, std::vector<unsigned>> value_bridge_clones;
  for (unsigned c = 0; c < plan.clones.size (); ++c)
    for (auto const &br : plan.bridges[c])
      {
	int vix = -1;
	for (unsigned m = 0; m != plan.members[c].size () && vix < 0; ++m)
	  {
	    crf_position const &p = blk.pos[plan.members[c][m]];
	    int ci = int (br.second) - SFPU_REG_FIRST;
	    if (p.value_of_use[ci] >= 0
		&& blk.values[p.value_of_use[ci]].reg == br.second)
	      vix = p.value_of_use[ci];
	  }
	if (vix >= 0)
	  value_bridge_clones[vix].push_back (c);
      }

  long horizon = 2L * t + 4;
  for (unsigned vix = 0; vix != blk.values.size (); ++vix)
    {
      crf_value const &v = blk.values[vix];
      long start = v.def_pos < 0 ? -1 : 2L * time[v.def_pos] + 1;
      long end = start;
      auto vb = value_bridge_clones.find (vix);
      for (unsigned up : v.use_positions)
	{
	  bool moved_use = false;
	  if (vb != value_bridge_clones.end ())
	    for (unsigned c : vb->second)
	      if (up >= plan.clones[c].begin && up < plan.clones[c].end)
		moved_use = true;
	  if (moved_use)
	    continue;
	  long ut = 2L * time[up];
	  if (ut > end)
	    end = ut;
	}
      if (vb != value_bridge_clones.end ())
	for (unsigned c : vb->second)
	  {
	    long bt = 2L * bridge_time[c];
	    if (bt > end)
	      end = bt;
	  }
      if (v.live_out)
	end = horizon;
      unsigned freg = crf_final_reg (blk, plan, vix);
      per_reg[freg - SFPU_REG_FIRST].push_back ({ start, end,
						  int (vix) });
    }

  /* A bridge destination is live from the bridge to the end of the
     clone (conservatively).  */
  for (unsigned c = 0; c < plan.clones.size (); ++c)
    for (auto const &br : plan.bridges[c])
      {
	long start = 2L * bridge_time[c] + 1;
	long end = 2L * time[plan.members[c].back ()];
	per_reg[br.first - SFPU_REG_FIRST].push_back ({ start, end, -1 });
      }

  for (unsigned r = 0; r != 8; ++r)
    {
      auto &iv = per_reg[r];
      std::sort (iv.begin (), iv.end (),
		 [] (interval const &a, interval const &b)
		 { return a.start < b.start; });
      /* A def at 2t+1 may follow uses at 2t; any other overlap is a
	 conflict.  */
      long reach = LONG_MIN;
      int reach_val = -1;
      for (unsigned i = 0; i < iv.size (); ++i)
	{
	  if (i && iv[i].start <= reach)
	    {
	      if (conflict_a)
		{
		  *conflict_a = reach_val;
		  *conflict_b = iv[i].val;
		}
	      if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-rename-interference:"
			       " two values occupy r%u:\n",
			       plan.clones.front ().begin,
			       plan.clones.front ().end, SFPU_REG_FIRST + r);
		  for (auto const &e : iv)
		    fprintf (dump_file, "    val %d [%ld,%ld] def insn %d"
			     " orig r%u\n", e.val, e.start, e.end,
			     e.val >= 0 && blk.values[e.val].def_insn
			     ? INSN_UID (blk.values[e.val].def_insn) : -1,
			     e.val >= 0 ? blk.values[e.val].reg : 0);
		}
	      return false;
	    }
	  if (iv[i].end >= reach)
	    {
	      reach = iv[i].end;
	      reach_val = iv[i].val;
	    }
	}
    }

  /* The lane state must be constant over every rewritten value's
     range.  */
  long wmin = LONG_MAX, wmax = LONG_MIN;
  auto widen = [&] (long s, long e) { wmin = MIN (wmin, s);
				      wmax = MAX (wmax, e); };
  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      if (v.def_pos >= 0)
	widen (2L * time[v.def_pos], 2L * time[v.def_pos]);
      for (unsigned up : v.use_positions)
	widen (2L * time[up], 2L * time[up]);
    }
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (!plan.bridges[c].empty ())
	widen (2L * bridge_time[c], 2L * time[plan.members[c].back ()]);
      for (unsigned pos : plan.moves_head[c])
	widen (2L * time[pos],
	       2L * time[plan.members[c].back ()]);
      for (unsigned pos : plan.moves_tail[c])
	widen (2L * time[plan.members[c].front ()], 2L * time[pos]);
    }
  if (wmin <= wmax)
    for (unsigned pos = 0; pos != n; ++pos)
      if (blk.pos[pos].cc_write && time[pos] >= 0
	  && 2L * time[pos] >= wmin && 2L * time[pos] <= wmax)
	{
	  rvtt_refuse (RVTT_REF_COUNTED_ROW_LANE_STATE, dump_file,
		       "Refusing counted-row family [%u,%u):"
		       " counted-row-lane-state: CC write (insn %d) inside"
		       " the rewritten window\n", plan.clones.front ().begin,
		       plan.clones.front ().end,
		       INSN_UID (blk.pos[pos].insn));
	  return false;
	}

  return true;
}

/* Apply the verified PLAN to BLK: make all register replacements in one
   change group (so recog checks each rewritten insn), fix the REG_DEAD
   and REG_UNUSED notes, move the excluded insns and emit the bridges.  */

static bool
crf_apply (crf_block &blk, crf_plan &plan)
{
  /* Rename definitions only in definition positions (a SET_DEST not
     inside a MEM) and uses only in use positions, since one insn can
     mention the same register as two values with different targets.
     Renames can chain (L5->L1 while L1->L3), so collect all locations
     from the original patterns first.  */
  std::vector<std::pair<rtx_insn *, std::pair<rtx *, unsigned>>> changes;
  auto queue_reg = [&changes] (rtx_insn *insn, unsigned from, unsigned to,
			       bool def_side)
  {
    auto walk = [&] (auto &self, rtx *loc, bool in_def) -> void
    {
      rtx x = *loc;
      if (!x)
	return;
      switch (GET_CODE (x))
	{
	case REG:
	  if (REGNO (x) == from && in_def == def_side)
	    changes.push_back ({ insn, { loc, to } });
	  return;
	case SET:
	  self (self, &SET_SRC (x), false);
	  self (self, &SET_DEST (x), true);
	  return;
	case CLOBBER:
	  self (self, &XEXP (x, 0), true);
	  return;
	case USE:
	case MEM:
	  self (self, &XEXP (x, 0), false);
	  return;
	default:
	  {
	    const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
	    for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
	      if (fmt[i] == 'e')
		self (self, &XEXP (x, i), in_def);
	      else if (fmt[i] == 'E')
		for (int j = XVECLEN (x, i); j--;)
		  self (self, &XVECEXP (x, i, j), in_def);
	  }
	  return;
	}
    };
    walk (walk, &PATTERN (insn), false);
  };

  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      if (v.def_pos >= 0)
	queue_reg (blk.pos[v.def_pos].insn, v.reg, rn.second, true);
      unsigned prev = (unsigned) -1;
      for (unsigned up : v.use_positions)
	{
	  if (up == prev)
	    continue;
	  prev = up;
	  queue_reg (blk.pos[up].insn, v.reg, rn.second, false);
	}
    }

  for (auto const &ch : changes)
    validate_change (ch.first, ch.second.first,
		     gen_rtx_REG (XTT32SImode, ch.second.second), 1);

  if (!apply_change_group ())
    {
      rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_CONSTRAINT, dump_file,
		   "Refusing counted-row family [%u,%u):"
		   " counted-row-rename-constraint: a rewritten instruction"
		   " failed re-recognition\n", plan.clones.front ().begin,
		   plan.clones.front ().end);
      return false;
    }

  /* REG_DEAD and REG_UNUSED notes of the rewritten insns.  */
  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      rtx to_reg = gen_rtx_REG (XTT32SImode, rn.second);
      auto fix_notes = [&] (rtx_insn *insn)
      {
	for (rtx note = REG_NOTES (insn); note; note = XEXP (note, 1))
	  if ((REG_NOTE_KIND (note) == REG_DEAD
	       || REG_NOTE_KIND (note) == REG_UNUSED)
	      && REG_P (XEXP (note, 0))
	      && REGNO (XEXP (note, 0)) == v.reg)
	    XEXP (note, 0) = to_reg;
      };
      if (v.def_pos >= 0)
	fix_notes (blk.pos[v.def_pos].insn);
      for (unsigned up : v.use_positions)
	fix_notes (blk.pos[up].insn);
    }

  /* Walk the order from crf_plan_order, which crf_shadow_contract_ok
     checked, keeping LAST as the previous insn: an unmoved position
     stays put, a moved insn is placed after LAST, and a bridge is
     emitted after LAST.  Moved entries always come after the
     predecessor of their clone's first member.  */
  {
    std::vector<int> order = crf_plan_order (blk, plan);
    std::vector<char> is_moved (blk.pos.size (), 0);
    for (unsigned c = 0; c != plan.clones.size (); ++c)
      {
	for (unsigned mpos : plan.moves_head[c])
	  is_moved[mpos] = 1;
	for (unsigned mpos : plan.moves_tail[c])
	  is_moved[mpos] = 1;
      }
    rtx_insn *last = nullptr;
    for (int e : order)
      if (e >= 0 && !is_moved[e])
	{
	  /* Moved entries at the start go before the first fixed
	     position.  */
	  last = blk.pos[e].insn;
	  break;
	}
    gcc_assert (last);
    last = PREV_INSN (last);

    unsigned bridge_ix = 0;
    int bridge_clone = -1;
    for (int e : order)
      {
	if (e >= 0)
	  {
	    rtx_insn *insn = blk.pos[e].insn;
	    if (is_moved[e])
	      reorder_insns (insn, insn, last);
	    last = insn;
	  }
	else
	  {
	    unsigned c = unsigned (-1 - e);
	    if (bridge_clone != int (c))
	      {
		bridge_clone = int (c);
		bridge_ix = 0;
	      }
	    auto const &br = plan.bridges[c][bridge_ix++];
	    rtx mv = gen_rtx_SET (gen_rtx_REG (XTT32SImode, br.first),
				  gen_rtx_REG (XTT32SImode, br.second));
	    rtx_insn *mvi = emit_insn_after (mv, last);
	    gcc_assert (recog_memoized (mvi) >= 0);
	    last = mvi;
	  }
      }
  }

  if (dump_file)
    {
      int bridges_total = 0, moved_total = 0;
      for (unsigned c = 0; c != plan.clones.size (); ++c)
	{
	  bridges_total += int (plan.bridges[c].size ());
	  moved_total += int (plan.moves_head[c].size ()
			      + plan.moves_tail[c].size ());
	}
      fprintf (dump_file, "Canonicalized counted-row family: %u clones,"
	       " %u members, %d renames, %d bridges, %d moved,"
	       " modeled saving %d slots\n",
	       unsigned (plan.clones.size ()),
	       unsigned (plan.members[0].size ()),
	       int (plan.renames.size ()), bridges_total, moved_total,
	       plan.saving);
      for (unsigned c = 0; c != plan.clones.size (); ++c)
	fprintf (dump_file,
		 "  clone %u at [%u,%u): %u+%u moved, %u bridged\n",
		 c, plan.clones[c].begin, plan.clones[c].end,
		 unsigned (plan.moves_head[c].size ()),
		 unsigned (plan.moves_tail[c].size ()),
		 unsigned (plan.bridges[c].size ()));
    }
  return true;
}


/* Form the recording and launches for an applied plan, as
   replace_sequence does: the first clone is recorded (executing where
   the target allows) and every other clone becomes a launch.  Mark the
   slots used as persistent.  */

static void
crf_form (crf_block &blk, crf_plan &plan, unsigned slot_start)
{
  unsigned length = plan.residual;
  bool not_quasar_fix = !(riscv_tt_fix_qsr_replay > 0);

  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode),
     GEN_INT (slot_start), GEN_INT (not_quasar_fix), GEN_INT (1));
  emit_insn_before (capture, blk.pos[plan.members[0].front ()].insn);

  bool keep = not_quasar_fix;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (c == 0 && keep)
	continue;
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (slot_start),
	 const0_rtx, const0_rtx);
      emit_insn_after (replay, blk.pos[plan.members[c].back ()].insn);
      if (c != 0)
	for (unsigned m = 0; m != length; ++m)
	  SET_INSN_DELETED (blk.pos[plan.members[c][m]].insn);
    }

  if (dump_file)
    fprintf (dump_file, "Formed counted-row record [%u,+%u): %u launch"
	     " sites\n", slot_start, length,
	     unsigned (plan.clones.size ()) - keep);
}

/* Rewrite counted-row families in CFN so that the ordinary discovery
   records one row program per family.  Candidates are ranked by saving,
   shorter rows first on ties, and each applied family's length is
   deducted from the slot budget.  */

static void
canonicalize_counted_rows (function *cfn,
			   std::vector<replay_span> const &replay_spans,
			   std::vector<bool> &persistent_slots,
			   bitmap dirty_bbs, bool sticky)
{
  auto spans = available_replay_spans (replay_spans, persistent_slots);
  if (spans.empty ())
    return;
  unsigned budget = spans.front ().end;

  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      if (bitmap_bit_p (dirty_bbs, bb->index))
	continue;

      /* Repeatedly apply the best verifiable family and rescan.  The
	 insns of an applied family are frozen, so a later family cannot
	 undo its rewriting.  */
      std::set<rtx_insn *> frozen;
      for (unsigned round = 0; round != 8 && budget >= MIN_SEQUENCE;
	   ++round)
	{
	  crf_block blk;
	  if (!crf_scan_block (bb, blk))
	    break;

	  std::vector<crf_seq> list;
	  crf_build_sequences (list, blk, budget);

	  /* Rank by saving (descending), then length and position
	     (ascending).  */
	  std::vector<unsigned> order;
	  for (unsigned ix = 1; ix < list.size (); ++ix)
	    {
	      crf_seq &seq = list[ix];
	      if (seq.clones.size () < 2 || seq.length < MIN_SEQUENCE)
		continue;
	      int bound = int (seq.clones.size () - 1)
		* int (seq.length - 1) - 1;
	      if (bound < 1)
		continue;
	      /* Families with nothing to rewrite are left to the
		 exact-match formation.  */
	      bool any_divergence = false;
	      for (auto const &c : seq.clones)
		{
		  for (auto const &pr : c.p2r)
		    if (pr.first != pr.second)
		      {
			any_divergence = true;
			break;
		      }
		  if (any_divergence)
		    break;
		  for (unsigned pos = c.begin;
		       pos != c.end && !any_divergence; ++pos)
		    if (blk.pos[pos].excludable)
		      any_divergence = true;
		  if (any_divergence)
		    break;
		}
	      if (!any_divergence)
		continue;
	      order.push_back (ix);
	    }
	  auto rank = [&list] (unsigned ix, int *bound, unsigned *residual,
			       unsigned *begin)
	  {
	    crf_seq &s = list[ix];
	    *bound = int (s.clones.size () - 1) * int (s.length - 1);
	    *residual = s.length;
	    *begin = s.clones.front ().begin;
	  };
	  std::sort (order.begin (), order.end (),
		     [&rank] (unsigned a, unsigned b)
	  {
	    int ba, bb_;
	    unsigned ra, rb, pa, pb;
	    rank (a, &ba, &ra, &pa);
	    rank (b, &bb_, &rb, &pb);
	    if (ba != bb_)
	      return ba > bb_;
	    if (ra != rb)
	      return ra < rb;
	    return pa < pb;
	  });
	  /* Limit the search; the ranking is total, so this is
	     deterministic.  */
	  if (order.size () > 64)
	    order.resize (64);
	  if (dump_file)
	    for (unsigned oi = 0; oi != order.size () && oi != 12; ++oi)
	      {
		crf_seq &s = list[order[oi]];
		fprintf (dump_file, "counted-row candidate %u: [%u,%u)"
			 " len %u clones %u\n", oi,
			 s.clones.front ().begin, s.clones.front ().end,
			 s.length, unsigned (s.clones.size ()));
	      }

	  /* Verify every ranked candidate and apply the best verified
	     plan, since verification may drop clones.  */
	  bool applied = false;
	  crf_plan best;
	  bool have_best = false;
	  for (unsigned ix : order)
	    for (unsigned ref = 0;
		 ref != 8 && ref < list[ix].clones.size (); ++ref)
	    {
	      crf_plan plan;
	      if (!crf_verify_family (blk, list[ix], budget, sticky, plan,
				      ref))
		continue;
	      bool touches_frozen = false;
	      for (auto const &rn : plan.renames)
		{
		  crf_value const &v = blk.values[rn.first];
		  if (v.def_pos >= 0
		      && frozen.count (blk.pos[v.def_pos].insn))
		    touches_frozen = true;
		  for (unsigned up : v.use_positions)
		    if (frozen.count (blk.pos[up].insn))
		      touches_frozen = true;
		}
	      if (touches_frozen)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-rename-interference:"
			       " rewrite touches an already-canonicalized"
			       " family\n", plan.clones.front ().begin,
			       plan.clones.front ().end);
		  continue;
		}
	      if (dump_file)
		fprintf (dump_file, "counted-row verified [%u,%u) ref %u:"
			 " %u clones, saving %d\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, ref,
			 unsigned (plan.clones.size ()), plan.saving);
	      if (!have_best || plan.saving > best.saving
		  || (plan.saving == best.saving
		      && plan.residual < best.residual))
		{
		  best = std::move (plan);
		  have_best = true;
		}
	    }
	  if (have_best && crf_apply (blk, best))
	    {
	      /* Best-fit slot span (smallest that holds the record).  */
	      auto avail = available_replay_spans (replay_spans,
						   persistent_slots);
	      unsigned start = 0;
	      bool found = false;
	      for (auto pos = avail.rbegin (); pos != avail.rend (); ++pos)
		if (pos->end >= best.residual)
		  {
		    start = pos->begin;
		    found = true;
		    break;
		  }
	      gcc_assert (found); /* budget model guaranteed a fit */
	      std::fill (persistent_slots.begin () + start,
			 persistent_slots.begin () + start + best.residual,
			 true);
	      crf_form (blk, best, start);
	      for (unsigned c = 0; c != best.clones.size (); ++c)
		for (unsigned m = 0; m != best.members[c].size (); ++m)
		  frozen.insert (blk.pos[best.members[c][m]].insn);
	      auto navail = available_replay_spans (replay_spans,
						    persistent_slots);
	      budget = navail.empty () ? 0 : navail.front ().end;
	      applied = true;
	    }
	  if (!applied)
	    break;
	}
    }
}

/* The replay pass looks for sequences of instructions that repeat and replaces
   the repeated portions w/ a REPLAY instruction */

/* Return true if INSN is a mod-write: a SETC16 address-modifier write
   (rvtt_ttsetc16_int), or a Dst access through an address modifier that
   increments (one whose ADDR_MODE effect is not NONE).  A no-exec
   recording that starts while a mod-write may still be draining hangs
   the hardware (rvtt-cost.md).  rtl-rvtt-dst-autoincr.cc guards its own
   groups; recordings placed by this pass are checked with this
   function.

   TTINCRWC and other RWC writers are not mod-writes; the LLK places
   recordings right after raw STALLWAIT words, which is known to work.
   Undecodable insns (calls, asm, opaque words) count for no distance.

   Set *WORDS to the number of issue words INSN contributes to the
   separation: the machine-description length for Tensix insns, one for a
   raw pure-RWC word, the issue length for scalar insns, and one for a
   jump.  */

static bool
placement_modwrite_hazard_p (rtx_insn *insn, unsigned *words)
{
  *words = 0;
  if (!NONDEBUG_INSN_P (insn))
    return false;
  if (CALL_P (insn))
    return false;		/* audit boundary: zero cover */
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
    return false;
  if (asm_noperands (pat) >= 0)
    {
      xtt_rwc_effect_t rwc;
      if (rvtt_raw_pure_dst_rwc (insn, &rwc))
	*words = 1;		/* pure Dst/RWC word */
      return false;
    }
  if (JUMP_P (insn))
    {
      *words = 1;		/* conservative pre-shortening floor */
      return false;
    }
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX)
    {
      *words = get_attr_length (insn) / 4;
      return false;
    }
  if (recog_memoized (insn) == CODE_FOR_rvtt_ttsetc16_int)
    {
      *words = get_attr_length (insn) / 4;
      return true;
    }
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    return false;		/* unknown: counts for nothing */
  *words = get_attr_length (insn) / 4;
  if ((e.dst_mem_write || e.dst_mem_read)
      && e.rwc.kind != xtt_rwc_effect_t::NONE)
    return true;
  return false;
}

/* Return true if a mod-write may lie less than WINDOW issue words before
   CAP on some CFG path, counting the words strictly between them.  A
   path with at least WINDOW words, or reaching the function entry, is
   safe.  Set *DIST to the distance found.  */

static bool
capture_modwrite_within_window_p (rtx_insn *cap, unsigned window,
				  unsigned *dist)
{
  basic_block cbb = BLOCK_FOR_INSN (cap);
  unsigned cover = 0;
  bool at_or_after_cap = true;
  rtx_insn *insn;
  bool hazard = false;
  FOR_BB_INSNS_REVERSE (cbb, insn)
    {
      if (at_or_after_cap)
	{
	  if (insn == cap)
	    at_or_after_cap = false;
	  continue;
	}
      if (cover >= window)
	return false;
      unsigned w;
      if (placement_modwrite_hazard_p (insn, &w))
	{
	  *dist = cover;
	  return true;
	}
      cover += w;
    }
  if (cover >= window)
    return false;

  /* Backward minimum-distance walk, pruned at WINDOW.  */
  hash_map<basic_block, unsigned> best;
  std::vector<std::pair<unsigned, basic_block>> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, cbb->preds)
    work.emplace_back (cover, e->src);
  while (!work.empty ())
    {
      auto it = std::min_element (work.begin (), work.end ());
      unsigned cost = it->first;
      basic_block bb = it->second;
      work.erase (it);
      if (cost >= window || bb == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      unsigned *seen = best.get (bb);
      if (seen && *seen <= cost)
	continue;
      best.put (bb, cost);
      unsigned c = cost;
      bool covered = false;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (c >= window)
	    {
	      covered = true;
	      break;
	    }
	  unsigned w;
	  if (placement_modwrite_hazard_p (insn, &w))
	    {
	      *dist = c;
	      hazard = true;
	      break;
	    }
	  c += w;
	}
      if (hazard)
	return true;
      if (covered || c >= window)
	continue;
      FOR_EACH_EDGE (e, ei, bb->preds)
	work.emplace_back (c, e->src);
    }
  return false;
}

/* Undo hoisted no-exec captures formed by this pass that are placed
   unsafely (rvtt-cost.md); user recordings are left alone.  Every launch
   of the span is replaced with a copy of the payload, which is what the
   launch executes, and the capture and its payload are deleted.  A
   capture is undone if:

   1. it is inside a loop and its payload stores to Dst: re-recording
      each iteration after the previous iteration's launched stores hangs
      the device, with or without TTINCRWC rows;
   2. it may start less than rvtt_modwrite_drained_frontend_window issue
      words after a mod-write (placement_modwrite_hazard_p), wherever it
      is placed; or
   3. its payload stores to Dst and it does not dominate every launch of
      its span.  The replay buffer persists across kernel invocations and
      functions, so such a launch may replay the store from a recording
      made on another path, call or kernel.  */

static void
unhoist_hazard_rerecords (function *cfn)
{
  for (rtx_insn *cap : formed_noexec_captures)
    {
      if (!cap || cap->deleted () || !INSN_P (cap))
	continue;
      rtx pat = PATTERN (cap);
      if (GET_CODE (pat) != UNSPEC_VOLATILE
	  || recog_memoized (cap) != CODE_FOR_rvtt_ttreplay_int)
	continue;
      if (XVECEXP (pat, 0, 6) != const0_rtx
	  || XVECEXP (pat, 0, 7) == const0_rtx)
	continue; /* Converted to exec-while-record: not a hazard.  */
      basic_block bb = BLOCK_FOR_INSN (cap);
      if (!bb)
	continue;
      class loop *loop = bb->loop_father;
      /* Rule 1 applies only to placements inside a loop.  */
      bool in_loop = loop && loop->num != 0;
      if (!CONST_INT_P (XVECEXP (pat, 0, 3))
	  || !CONST_INT_P (XVECEXP (pat, 0, 5)))
	continue;
      unsigned len = UINTVAL (XVECEXP (pat, 0, 3));
      unsigned begin = UINTVAL (XVECEXP (pat, 0, 5));

      /* Collect the payload shadow and test it for a Dst store.  */
      std::vector<rtx_insn *> payload;
      bool has_store = false;
      bool scan_ok = true;
      unsigned remaining = len;
      for (rtx_insn *insn = NEXT_INSN (cap); remaining;
	   insn = NEXT_INSN (insn))
	{
	  if (!insn || BLOCK_FOR_INSN (insn) != bb)
	    {
	      scan_ok = false;
	      break;
	    }
	  if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN)
	    continue;
	  rtx ppat = PATTERN (insn);
	  if (GET_CODE (ppat) == USE || GET_CODE (ppat) == CLOBBER)
	    continue;
	  if (recog_memoized (insn) < 0
	      || get_attr_type (insn) != TYPE_TENSIX
	      || !get_attr_length (insn))
	    continue;
	  payload.push_back (insn);
	  if (recog_memoized (insn) == CODE_FOR_rvtt_sfpstore_int
	      || recog_memoized (insn) == CODE_FOR_rvtt_sfpstoresrcs_int)
	    has_store = true;
	  --remaining;
	}
      if (!scan_ok)
	continue;

      /* Collect the launches of this span, for rule 3.  */
      std::vector<rtx_insn *> launch_insns;
      basic_block lbb;
      FOR_EACH_BB_FN (lbb, cfn)
	{
	  rtx_insn *insn, *lnext;
	  for (insn = BB_HEAD (lbb); insn; insn = lnext)
	    {
	      lnext = insn == BB_END (lbb) ? nullptr : NEXT_INSN (insn);
	      if (!NONDEBUG_INSN_P (insn) || insn == cap
		  || GET_CODE (insn) != INSN
		  || recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
		continue;
	      rtx lpat = PATTERN (insn);
	      if (XVECEXP (lpat, 0, 7) != const0_rtx
		  || !CONST_INT_P (XVECEXP (lpat, 0, 3))
		  || !CONST_INT_P (XVECEXP (lpat, 0, 5))
		  || UINTVAL (XVECEXP (lpat, 0, 3)) != len
		  || UINTVAL (XVECEXP (lpat, 0, 5)) != begin)
		continue;
	      launch_insns.push_back (insn);
	    }
	}

      /* Apply rules 1-3 above.  A dominating recording is safe, as it
	 executes before every launch in the same invocation.  */
      bool dststore_rule = in_loop && has_store;
      unsigned modwrite_dist = 0;
      unsigned window = rvtt_modwrite_drained_frontend_window ();
      bool modwrite_rule
	= !dststore_rule
	  && capture_modwrite_within_window_p (cap, window, &modwrite_dist);
      bool exit_rule = false;
      if (!dststore_rule && !modwrite_rule && has_store)
	{
	  calculate_dominance_info (CDI_DOMINATORS);
	  bool dominates_all = !launch_insns.empty ();
	  for (rtx_insn *li : launch_insns)
	    {
	      basic_block lb = BLOCK_FOR_INSN (li);
	      if (!lb || !dominated_by_p (CDI_DOMINATORS, lb, bb))
		{
		  dominates_all = false;
		  break;
		}
	    }
	  exit_rule = !dominates_all;
	}
      if (!dststore_rule && !modwrite_rule && !exit_rule)
	continue;

      /* Replace every launch of the exact span with the inline payload
	 and delete the capture and its shadow.  */
      unsigned launches = 0;
      for (rtx_insn *insn : launch_insns)
	{
	  if (!insn || insn->deleted ())
	    continue;
	  for (rtx_insn *pw : payload)
	    emit_insn_before (copy_insn (PATTERN (pw)), insn);
	  delete_insn (insn);
	  ++launches;
	}
      for (rtx_insn *pw : payload)
	delete_insn (pw);
      delete_insn (cap);
      if (dump_file)
	{
	  if (dststore_rule)
	    rvtt_refuse
	      (RVTT_REF_NOEXEC_RERECORD_DSTSTORE_COMPOSITION_UNAUDITED,
	       dump_file,
	       "Replay refusal: noexec-rerecord-dststore-"
	       "composition-unaudited (capture bb %d in loop %d "
	       "un-hoisted, %u launches inlined)\n",
	       bb->index, loop->num, launches);
	  else if (modwrite_rule)
	    rvtt_refuse (RVTT_REF_NOEXEC_RECORD_MODWRITE_WINDOW_UNAUDITED,
			 dump_file,
			 "Replay refusal: noexec-record-modwrite-"
			 "window-unaudited (capture bb %d %u issue words after "
			 "an audited mod-write, window %u; un-hoisted, "
			 "%u launches inlined)\n",
			 bb->index, modwrite_dist, window, launches);
	  else
	    rvtt_refuse (RVTT_REF_NOEXEC_RECORD_DSTSTORE_NONDOMINATING_LAUNCH_PERSIST_UNAUDITED, dump_file,
			 "Replay refusal: noexec-record-dststore-"
			 "nondominating-launch-persist-unaudited"
			 " (capture bb %d "
			 "does not dominate all %u launches of its span; "
			 "un-hoisted, %u launches inlined)\n",
			 bb->index, (unsigned) launch_insns.size (), launches);
	}
    }
  if (dom_info_available_p (CDI_DOMINATORS))
    free_dominance_info (CDI_DOMINATORS);
  formed_noexec_captures.clear ();
  formed_playback_launches.clear ();
}

/* Form replays in CFN, with BUFFER_SIZE slots.  Both pass instances
   call this; reform_mode tells them apart.  See the comment at the top
   of the file for the steps.  */

static void
transform (function *cfn, unsigned buffer_size)
{
  basic_block bb;
  std::vector<replay_span> replay_spans;
  formed_noexec_captures.clear ();
  formed_playback_launches.clear ();

  /* The allocator only sees slots used by typed rvtt_ttreplay_int
     insns.  A raw ".ttinsn" REPLAY word in an asm could own any slots,
     and its range cannot be decoded, so give up on the whole function.
     Other raw words are ordinary sequence boundaries.  A non-constant
     raw word cannot be checked; this is a known limitation.  */
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *scan_insn;
      FOR_BB_INSNS (bb, scan_insn)
	{
	  uint32_t raw_word;
	  if (NONDEBUG_INSN_P (scan_insn)
	      && asm_noperands (PATTERN (scan_insn)) >= 0
	      && rvtt_raw_ttinsn_word (scan_insn, &raw_word)
	      && rvtt_raw_replay_owner_word_p (raw_word))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "replay-raw-capture-present: raw .ttinsn word "
			 "0x%08x (insn %d) carries the REPLAY opcode; "
			 "refusing all replay allocation in this function\n",
			 raw_word, INSN_UID (scan_insn));
	      return;
	    }
	}
    }

  /* A user capture starts a recording.  If the typed insns show that
     it ends (rvtt_replay_epoch_close: the payload's typed lengths add up
     to the declared length), formation may proceed around it.
     Otherwise only the region where the recording may still be open is
     excluded: from the capture to the next explicit TTREPLAY on each
     path, since a TTREPLAY issued while recording would be recorded
     rather than executed.  Blocks in which such a region contains a
     non-empty word or an asm are added to DIRTY_BBS and excluded from
     formation and the later rewrites.  Without -mtt-tensix-optimize-
     replay-hoist (riscv_tt_opt_replay_hoist), any open recording
     disables formation in the whole function.  */
  bool scoped = riscv_tt_opt_replay_hoist > 0;
  auto_bitmap dirty_bbs;      /* excluded from formation/rewrites */
  auto_bitmap open_exit_bbs;  /* recording state possibly open at exit */

  /* Determine replay_spans ranges */
  replay_spans.emplace_back (0, buffer_size);
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *insn;
      unsigned shadow = 0;
      bool open_unprovable = false;
      rtx_insn *skip_until = nullptr;
      FOR_BB_INSNS (bb, insn)
	{
	  if (GET_CODE (insn) != INSN)
	    continue;
	  rtx pattern = PATTERN (insn);

	  if (GET_CODE (pattern) == USE)
	    continue;
	  if (GET_CODE (pattern) == CLOBBER)
	    continue;

	  if (skip_until)
	    {
	      /* Inside a closed user capture payload.  */
	      if (insn == skip_until)
		skip_until = nullptr;
	      continue;
	    }

	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;

	  replay_span span;
	  auto type = is_replay_insn (span, insn);
	  if (type == REPLAY_none)
	    {
	      if (scoped)
		{
		  if (open_unprovable && get_attr_length (insn))
		    /* A non-empty word while a recording may be open.  */
		    bitmap_set_bit (dirty_bbs, bb->index);
		}
	      else if (shadow && get_attr_length (insn))
		shadow--;
	      continue;
	    }
	  if (scoped)
	    /* An explicit TTREPLAY closes any open recording.  */
	    open_unprovable = false;
	  else if (shadow)
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "User capturing or replaying during capture\n");
	      return;
	    }

	  if (type == REPLAY_variable_capture)
	    /* Using remainder of the buffer.  */
	    span.end = buffer_size;
	  else
	    {
	      if (type == REPLAY_fixed_capture)
		{
		  if (!scoped)
		    shadow = span.end;
		  else
		    {
		      xtt_replay_epoch epoch
			= rvtt_replay_epoch_close (insn, span.end);
		      switch (epoch.status)
			{
			case xtt_replay_epoch::CLOSED:
			  if (dump_file)
			    fprintf (dump_file,
				     "User capture [%u,+%u): typed epoch"
				     " closed at insn %d; payload retains"
				     " %u multi-result insn(s)\n",
				     span.begin, span.end,
				     INSN_UID (epoch.close_at),
				     epoch.multiresult_members);
			  if (epoch.close_at != insn)
			    skip_until = epoch.close_at;
			  break;

			case xtt_replay_epoch::OWNER_DURING_CAPTURE:
			  if (dump_file)
			    fprintf (dump_file,
				     "User capturing or replaying"
				     " during capture\n");
			  return;

			case xtt_replay_epoch::OPAQUE_PAYLOAD:
			case xtt_replay_epoch::CROSSES_BLOCK:
			  if (dump_file)
			    {
			      fprintf (dump_file,
				       "User capture [%u,+%u): recording epoch"
				       " unprovable (%s",
				       span.begin, span.end,
				       epoch.status
				       == xtt_replay_epoch::OPAQUE_PAYLOAD
				       ? "opaque payload"
				       : "payload crosses the block");
			      if (epoch.blocker)
				fprintf (dump_file, " at insn %d",
					 INSN_UID (epoch.blocker));
			      fprintf (dump_file, "); refusing formation"
				       " until the next replay owner\n");
			    }
			  open_unprovable = true;
			  bitmap_set_bit (dirty_bbs, bb->index);
			  break;
			}
		    }
		}

	      span.end += span.begin;
	    }

	  /* Cut out [from,to) from replay_spans.  */
	  for (auto pos = replay_spans.begin (), end = replay_spans.end ();
	       pos != end;)
	    if (pos->end <= span.begin)
	      ++pos; /* not reached, continue */
	    else if (pos->begin >= span.end)
	      break; /* gone past, we're done */
	    else if (pos->begin >= span.begin && pos->end <= span.end)
	      replay_spans.erase (pos), --end; /* entirely consumed */
	    else if (pos->begin >= span.begin)
	      {
		pos->begin = span.end; /* snip front */
		break;
	      }
	    else if (pos->end <= span.end)
	      pos->end = span.begin, ++pos; /* snip back */
	    else
	      {
		/* punch hole, and we're done */
		unsigned e = pos->end;
		pos->end = span.begin;
		replay_spans.emplace (pos, span.end, e);
		break;
	      }
	}
      if (scoped)
	{
	  if (open_unprovable)
	    bitmap_set_bit (open_exit_bbs, bb->index);
	}
      else if (shadow)
	{
	  if (dump_file)
	    fprintf (dump_file, "User capturing across basic block\n");
	  return;
	}
    }

  /* Propagate a possibly open recording through the CFG until each path
     reaches an explicit TTREPLAY, marking blocks as described above.  */
  if (scoped && !bitmap_empty_p (open_exit_bbs))
    {
      std::vector<basic_block> work;
      auto_bitmap visited;
      unsigned ix;
      bitmap_iterator bi;
      edge e;
      edge_iterator ei;
      EXECUTE_IF_SET_IN_BITMAP (open_exit_bbs, 0, ix, bi)
	FOR_EACH_EDGE (e, ei, BASIC_BLOCK_FOR_FN (cfn, ix)->succs)
	  work.push_back (e->dest);
      while (!work.empty ())
	{
	  basic_block cur = work.back ();
	  work.pop_back ();
	  if (cur == EXIT_BLOCK_PTR_FOR_FN (cfn)
	      || !bitmap_set_bit (visited, cur->index))
	    continue;
	  bool closed = false;
	  bool dirty = false;
	  rtx_insn *insn;
	  FOR_BB_INSNS (cur, insn)
	    {
	      if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN)
		continue;
	      rtx pattern = PATTERN (insn);
	      if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
		continue;
	      if (asm_noperands (pattern) >= 0 || recog_memoized (insn) < 0)
		{
		  dirty = true;
		  continue;
		}
	      if (get_attr_type (insn) != TYPE_TENSIX)
		continue;
	      if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
		{
		  closed = true;
		  break;
		}
	      if (get_attr_length (insn))
		dirty = true;
	    }
	  if (dirty)
	    bitmap_set_bit (dirty_bbs, cur->index);
	  if (!closed)
	    FOR_EACH_EDGE (e, ei, cur->succs)
	      work.push_back (e->dest);
	}
    }

  /* Convert replay_spans to be [start, +length) */
  for (auto &slot : replay_spans)
    slot.end -= slot.begin;
  /* Sort in decreasing length */
  std::sort (replay_spans.begin (), replay_spans.end (),
	     [] (replay_span const a, replay_span const b)
	     {
	       return a.end > b.end
		 || (a.end == b.end && a.begin < b.begin);
	     });
  /* Remove spans that are too short */
  while (!replay_spans.empty ()
	 && replay_spans.back ().end < MIN_SEQUENCE)
    replay_spans.pop_back ();
  if (replay_spans.empty ())
    {
      if (dump_file)
	fprintf (dump_file, "No replay buffer slots replay_spans\n");
      return;
    }
  
  if (dump_file)
    {
      for (auto const &slot : replay_spans)
	fprintf (dump_file, "Slots [%u,+%u) \n",
		 slot.begin, slot.end);
      fprintf (dump_file, "\n");
    }

  /* Whether index tracking may be enabled, for span_companion_sound_p.  */
  bool sticky = rvtt_shadow_coupling_possible (cfn);

  std::vector<bool> persistent_slots (buffer_size, false);
  if (riscv_tt_opt_replay_hoist > 0)
    hoist_counted_loops (cfn, replay_spans, persistent_slots, dirty_bbs,
			 sticky);

  /* Rewrite counted-row families (see canonicalize_counted_rows).  In
     reform_mode this runs once, after the auto-increment pass; it never
     moves insns with Dst or RWC effects and never renames address
     modifier operands, so it leaves carried accesses alone.  */
  if (riscv_tt_opt_counted_row > 0)
    canonicalize_counted_rows (cfn, replay_spans, persistent_slots,
			       dirty_bbs, sticky);

  replay_block info; /* insn info */
  replay_list list; /* list of sequences */
  replay_map map; /* map of sequences */
  replay_active active; /* pointers to active (under-consideration) sequences */
  FOR_EACH_BB_FN (bb, cfn)
    {
      if (bitmap_bit_p (dirty_bbs, bb->index))
	/* A user recording may be open here.  */
	continue;
      if (!scan_insns (info, bb))
	continue;

      /* This is N^2 */
      unsigned lwm
	= build_sequences (map, list, info, replay_spans.front ().end);

      active_triage (info, active, list, lwm);

      /* This is the knapsack problem :( */
      auto spans = available_replay_spans (replay_spans, persistent_slots);
      if (spans.empty ())
	continue;

      while (!active.empty ())
	{
	  auto *seq = pick_replay (active, spans.front ().end, info, sticky);
	  if (!seq)
	    break;

	  /* See reform_carried_launch_arithmetic_ok.  A refused candidate
	     is marked in companion_ok so it is not picked again.  */
	  if (reform_mode
	      && payload_contains_carried_p (info, seq->clones.front ())
	      && !reform_carried_launch_arithmetic_ok (info, *seq))
	    {
	      seq->companion_ok = 0;
	      continue;
	    }

	  /* Try to hoist the recording, with either the replay-hoist or
	     the record-hoist flag.  */
	  peel_plan peel;
	  hoist_lift_plan lift;
	  basic_block preheader
	    = (riscv_tt_opt_replay_hoist > 0
	       || riscv_tt_opt_replay_record_hoist > 0)
	    ? hoist_preheader (*seq, info, dirty_bbs, &peel, &lift) : nullptr;

	  /* Try a wider window for a hoisted (not peeled) recording; see
	     window_sizing_clones_exact_p.  The wider candidate must pass
	     the full hoist checks itself.  */
	  unsigned trim_len = 0, trim_end = 0;
	  if (riscv_tt_opt_replay_window_sizing > 0 && preheader
	      && !peel.valid)
	    {
	      if (reform_mode)
		{
		  rvtt_refuse
		    (RVTT_REF_WINDOW_SIZING_REFORM_COMPOSITION_UNAUDITED,
		     dump_file,
		     "window-sizing refused:"
		     " window-sizing-reform-composition-unaudited:"
		     " widening a re-formation pick would need the"
		     " carried launch-arithmetic audit re-derived for"
		     " the partial trim; keeping the picked window\n");
		}
	      else if (replay_sequence *wide
		       = window_sizing_widen (active, seq, info,
					      spans.front ().end, sticky,
					      &trim_len, &trim_end))
		{
		  peel_plan peel_w;
		  hoist_lift_plan lift_w;
		  basic_block ph_w
		    = hoist_preheader (*wide, info, dirty_bbs, &peel_w,
				       &lift_w);
		  if (ph_w && !peel_w.valid)
		    {
		      seq = wide;
		      peel = peel_w;
		      lift = lift_w;
		      preheader = ph_w;
		    }
		  else
		    {
		      trim_len = trim_end = 0;
		      rvtt_refuse (RVTT_REF_WINDOW_SIZING_HOIST_REFUSED,
				   dump_file,
				   "window-sizing refused:"
				   " window-sizing-hoist-refused: widened"
				   " window [%u,+%u) does not re-prove the"
				   " hoist admission; keeping the picked"
				   " window\n",
				   wide->clones.front ().begin, wide->length);
		    }
		}
	    }

	  auto slot = spans.begin ();
	  /* Is there a better fit?
	     FIXME: should we only accept exact fit?  */
	  for (auto probe = slot;
	       ++probe != spans.end () && probe->end >= seq->length;)
	    slot = probe;

	  unsigned len = preheader
	    ? (peel.valid
	       ? replace_hoisted_sequence_peel (*seq, info, slot->begin,
						preheader, peel)
	       : replace_hoisted_sequence (*seq, info, slot->begin,
					   lift.valid ? lift.placement
					   : preheader))
	    : replace_sequence (*seq, info, slot->begin);
	  if (trim_len)
	    window_sizing_commit_trim (*seq, info, slot->begin, trim_len,
				       trim_end);
	  if (preheader)
	    std::fill (persistent_slots.begin () + slot->begin,
		       persistent_slots.begin () + slot->begin + len, true);
	  slot->begin += len;
	  slot->end -= len;

	  if (slot->end < MIN_SEQUENCE)
	    spans.erase (slot);
	  else
	    for (auto pos = slot;
		 ++pos != spans.end ()
		   && slot->end < pos->end;
		 ++slot)
	      std::swap (slot, pos);

	  if (spans.empty ())
	    break;

	  /* Remove unuseable sequences */
	  active_invalidate (active, seq, spans.front ().end);
	}
    }

  /* These clean up after the hoist, so they share its flag.  */
  if (riscv_tt_opt_replay_hoist > 0)
    {
      unroll_launch_loops (cfn, dirty_bbs);
      convert_isomorphic_runs (cfn, dirty_bbs);
    }

  /* Undo unsafe hoisted no-exec captures.  */
  unhoist_hazard_rerecords (cfn);
}

namespace {

const pass_data pass_data_rvtt_replay =
{
  RTL_PASS, /* type */
  "rvtt_replay", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_replay : public rtl_opt_pass
{
public:
  pass_rvtt_replay (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_replay, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    /* With -mtt-tensix-optimize-post-autoincr-window, formation is done
       only by pass_rvtt_replay_reform; see reform_mode.  */
    return TARGET_XTT_TENSIX && riscv_tt_opt_replay > 0
      && !(riscv_tt_opt_post_autoincr_window > 0);
  }

  /* opt_pass methods: */
  virtual unsigned execute (function *fn) override
  {
    bool loops_needed = riscv_tt_opt_replay_hoist > 0
      || riscv_tt_opt_replay_record_hoist > 0;
    if (loops_needed)
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    transform (fn, riscv_tt_replay_size);
    if (loops_needed)
      {
	loop_optimizer_finalize ();
	free_dominance_info (CDI_DOMINATORS);
      }
    return 0;
  }
}; /* class pass_rvtt_replay */

/* The same formation, run after pass_rvtt_dst_autoincr in reform_mode
   (-mtt-tensix-optimize-post-autoincr-window).  */

const pass_data pass_data_rvtt_replay_reform =
{
  RTL_PASS, /* type */
  "rvtt_replay_reform", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_replay_reform : public rtl_opt_pass
{
public:
  pass_rvtt_replay_reform (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_replay_reform, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_replay > 0
      && riscv_tt_opt_post_autoincr_window > 0;
  }

  /* opt_pass methods: */
  virtual unsigned execute (function *fn) override
  {
    bool loops_needed = riscv_tt_opt_replay_hoist > 0
      || riscv_tt_opt_replay_record_hoist > 0;
    if (loops_needed)
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    reform_mode = true;
    transform (fn, riscv_tt_replay_size);
    reform_mode = false;
    if (loops_needed)
      {
	loop_optimizer_finalize ();
	free_dominance_info (CDI_DOMINATORS);
      }
    return 0;
  }
}; /* class pass_rvtt_replay_reform */

} /* anon namespace */

/* Create the replay pass that runs after reload, before Dst
   auto-increment and MOP formation.  */

rtl_opt_pass *
make_pass_rvtt_replay (gcc::context *ctxt)
{
  return new pass_rvtt_replay (ctxt);
}

/* Create the replay pass that runs after pass_rvtt_dst_autoincr.  Its
   gate is the complement of pass_rvtt_replay's, so exactly one of them
   forms replays.  */

rtl_opt_pass *
make_pass_rvtt_replay_reform (gcc::context *ctxt)
{
  return new pass_rvtt_replay_reform (ctxt);
}
