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
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-mop-tables.h"
#include "rvtt-macro-epoch.h"

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

// Minimum acceptable sequence length.  4 mirrors
// XTT_REPLAY_LOOP_UNROLL_MIN_WORDS (rvtt-cost.md): smaller rows cannot
// amortize a record/playback window.  Self-declared uncalibrated there --
// no silicon point separates 3 from 4 (DG2 audit item; a calibration
// experiment remains the pricing lane's follow-up).
constexpr unsigned MIN_SEQUENCE = 4;

// Information about a tensix insn wrt replayability.  For an insn to be
// replayable it must be the same as the original and same generation.
// Sequences must not stradle a must_end insn.  Empty insns are ignored.
struct replay_info
{
  rtx_insn *insn;
  unsigned hash;       // hash for insn, used in extending sequences
  unsigned generation; // Oldest SI value used (in synth insns)
  bool must_end = true; // Cannot be extended (followed by asm, non-Tensix)
  bool empty = false; // Is an empty tensix insn -- doesn't increase length

  replay_info (rtx_insn *insn, unsigned gen, unsigned hash, bool empty)
    : insn (insn),  hash (hash), generation (gen), empty (empty) {}
};

// The replay info about all instructions in a BB
using replay_block = std::vector<replay_info>;

// A half-open interval
struct replay_span
{
  unsigned begin;
  unsigned end;

  replay_span () {}
  replay_span (unsigned b, unsigned e)
    : begin (b), end (e)
  {}
};

// A sequence of insns, and all the clones of that instance.
// Each instance is its own clone.
struct replay_sequence
{
  unsigned parent; // The 1-shorter sequence from whence this grew
  unsigned hash;
  unsigned length; // number of insns (does not include empty insns)
  int companion_ok = -1; // cached span_companion_sound_p verdict (-1 unset)

  // Instances of this sequence. By construction these are in increasing
  // starting insn. During construction these might overlap.  We deal with that
  // before use.
  std::vector<replay_span> clones;

  replay_sequence ()
    : parent (0), hash (0), length (0)
  {}
  replay_sequence (int parent, unsigned hash, unsigned length)
    : parent (parent), hash (hash), length (length)
  {}
};

// Set of sequences, by contstruction these are in incressing length first and
// within each length by starting insn position.
using replay_list = std::vector<replay_sequence>;

// Map from hash to set of sequences, used to find matches during construction
using replay_map = std::map<unsigned, std::vector<unsigned>>;

// It is cheaper to remove/copy pointers than sequence info itself.
using replay_active = std::vector<replay_sequence *>;

enum REPLAY_TYPE {REPLAY_none, REPLAY_playback, REPLAY_fixed_capture, REPLAY_variable_capture};

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

// Scan insns o block computing hashes and must_end.

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
	      // We don't know where this ends, so have to stop scanning the
	      // BB.
	      break;

	    if (type == REPLAY_fixed_capture)
	      shadow = span.end;
	    // A replay owner is a slot-occupying word.  A sequence
	    // spanning it would form a capture whose recording swallows
	    // the owner word (a REPLAY issued while recording is
	    // recorded, not executed) and the counted-row phase's inline
	    // reference body along with it.  End sequence continuity
	    // here: the owner and its recorded shadow separate runs.
	    may_continue = false;
	    continue;
	  }

	// Only machine-described replay-safe instructions may enter a payload.
	// Explicit replay owners are handled above so their reserved slots remain
	// visible to the allocator.  In particular, an opaque asm remains a
	// boundary even if it happens to print a constant `.ttinsn' word in the
	// final assembly.
	if (get_attr_type (insn) != TYPE_TENSIX
	    || replay_class != XTT_REPLAY_SAFE)
	  goto not_tensix;

	bool is_empty = !get_attr_length (insn);
	if (shadow)
	  {
	    // We're in the shadow of a replay capture
	    if (!is_empty)
	      shadow--;
	    continue;
	  }

	if (may_continue)
	  info.back ().must_end = false;

	// Just use crc32, it's right there
	unsigned age = 0;
	auto hasher = [&reg_ages, &age] (auto &self, unsigned hash, rtx rtl) -> unsigned
	{
	  hash = crc32_unsigned (hash, GET_CODE (rtl) + (GET_MODE (rtl) << 16));
	  switch (GET_CODE (rtl))
	    {
	    default:
	      gcc_unreachable ();

	    case UNSPEC:
	    case UNSPEC_VOLATILE:
	      hash = crc32_unsigned (hash, XINT (rtl, 1));
	      // FALLTHROUGH

	    case PARALLEL:
	      {
		// All 3 have the vector at position 0
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
	      // MEMs are to store a synthesized insn.  All are equivalent.
	      // In broken code, we could meet simple sets moving to/from MEM.
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

static void
extend_sequence (replay_map &map, replay_list &list, replay_block &block,
		 unsigned parent, unsigned length, unsigned begin, unsigned end)
{
  auto &insn = block[end - 1];

  unsigned hash = parent ? crc32_unsigned (list[parent].hash, insn.hash) : insn.hash;
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
      auto ignore = [] (const_rtx *a, const_rtx *b, rtx *na, rtx *nb) {
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
      };
      if (!rtx_equal_p (PATTERN (seq_insn.insn), PATTERN (insn.insn),
			ignore))
	continue;

      // Clones must be in ascending order (the invalidation presumes that)
      gcc_assert (begin > seq.clones.back ().begin);

      // This might create overlapping clones, but we still need this as a
      // later extension could only apply to one of these.
      seq.clones.emplace_back (begin, end);
      return;
    }

  slot.first->second.emplace_back (unsigned (list.size ()));

  // New sequence
  list.emplace_back (parent, hash, length);
  // It is its own clone
  list.back ().clones.emplace_back (begin, end);  
}

// Build sequences of insns and their copies.  This is fundamentally O(N^2).
// Return number index of first sequence >= MIN_SEQUENCE.

static unsigned
build_sequences (replay_map &map, replay_list &list, replay_block &block, unsigned max_length)
{
  list.clear ();
  list.push_back (replay_sequence ()); // null sequence
  map.clear ();

  // Initialize sequences of length 1.  These are the seeds from whence
  // sequences grow. Historically we started sequences at load insns (those
  // being the first of a loop), to further reduce N.
  for (unsigned ix = 0, end_ix = block.size (); ix != end_ix; ++ix)
    {
      if (block[ix].empty)
	continue;

      extend_sequence (map, list, block, 0, 1, ix, ix + 1);
    }
  unsigned lwm = unsigned (list.size ());

  // Grow each sequence by 1, until we can grow no more, or we get too long
  unsigned from = 1, length = 1;
  while (length++ < max_length)
    {
      map.clear ();

      unsigned seq_end = list.size ();
      for (unsigned seq_ix = from; seq_ix != seq_end; seq_ix++)
	{
	  if (list[seq_ix].clones.size () == 1)
	    // There is only one instance, no point extending this.
	    continue;

	  // Warning, list is extended inside this loop. Beware iterator
	  // invalidation
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

	      extend_sequence (map, list, block, seq_ix, length, span.begin, span.end + 1);
	    }
	}

      if (length < MIN_SEQUENCE)
	lwm = list.size ();
      from = seq_end;
    }

  return lwm;
}

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


// LIST has been computed, but sequences might contain overlapping runs.
// Remove overlaps, and push a pointer to valid ones into the ACTIVE array.

static void
active_triage (replay_block const &block, replay_active &active, replay_list &list, unsigned from)
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

      // Remember this if it has more than one instance.
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
	      if (dump_file)
		fprintf (dump_file,
			 "Refusing capture: shadow-state-unproved:"
			 " insn %d is effect-opaque under possibly-enabled"
			 " index tracking\n", INSN_UID (insn));
	      return false;
	    }
	  if (!multi && (e.lreg_write & 0xF))
	    {
	      if (dump_file)
		fprintf (dump_file,
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
	  // Boundary integrity: adjacent zero-length companion markers
	  // outside the span must not be split from their instruction.
	  if (ix + 1 >= span.end)
	    for (unsigned probe = span.end; probe != block.size (); ++probe)
	      {
		if (!block[probe].empty)
		  break;
		uint32_t mask;
		if (rvtt_lreg_marker (block[probe].insn, &mask)
		    && (mask & group_mask))
		  {
		    if (dump_file)
		      fprintf (dump_file,
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
		    if (dump_file)
		      fprintf (dump_file,
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
      // Quasar exec-while-load doesn't work, so we need an extra replay
      unsigned saving = (seq->clones.size () - 1) * (seq->length - 1)
	- !(riscv_tt_fix_qsr_replay > 0);
      if (best < saving || (best == saving && result && result->length < seq->length))
	{
	  best = saving;
	  result = seq;
	}
    }

  return result;
}

static unsigned
replace_sequence (replay_sequence &seq, replay_block &block, unsigned replay_start)
{
  unsigned length = seq.length;
  bool not_quasar_fix = !(riscv_tt_fix_qsr_replay > 0);
  if (dump_file)
    {
      unsigned saving = (seq.length - 1) * (unsigned (seq.clones.size ()) - 1)
      - not_quasar_fix;
      fprintf (dump_file, "Capturing %ssequence [%u,%u) %u instances to [%u,+%u) saving=%u\n",
	       not_quasar_fix ? "and executing " : "",
	       seq.clones.front ().begin, seq.clones.front ().end,
	       unsigned (seq.clones.size ()),
	       replay_start, length, saving);
      dump_sequence (dump_file, block, seq.clones.front (), replay_start);
      fprintf (dump_file, "\n");
    }

  // Cannot exec while capturing on quasar
  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode),
     GEN_INT (replay_start), GEN_INT (not_quasar_fix), GEN_INT (1));
  emit_insn_before (capture, block[seq.clones.front ().begin].insn);

  // Make sure we've not deleted anything in this instance already
  for (auto pos = block.data () + seq.clones.front ().begin,
	 end = block.data () + seq.clones.front ().end;
       pos != end; pos++)
    gcc_assert (GET_CODE (pos->insn) == INSN);

  for (auto clone = seq.clones.begin () + not_quasar_fix; clone != seq.clones.end (); ++clone)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode),
	 GEN_INT (replay_start), const0_rtx, const0_rtx);
      auto *insn = emit_insn_after (replay, block[clone->end - 1].insn);
      if (dump_file)
	{
	  fprintf (dump_file, not_quasar_fix ? "Replaying " : "Replaying original ");
	  dump_insn_slim (dump_file, insn);
	}
      if (not_quasar_fix)
	{
	  unsigned ix = replay_start;
	  for (auto pos = block.data () + clone->begin, end = block.data () + clone->end;
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

// Return true when X is a post-RA, fixed-encoding SFPU pattern.  Hard LREGs,
// constants and compiler scratch outputs are fixed.  A GPR or MEM means that
// the instruction word can still be synthesized at run time and therefore
// cannot be recorded without executing at its original location.
static bool
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

   The RTL loop-iv/SCEV helpers (get_simple_loop_desc and friends) assert
   LOOPS_HAVE_PREHEADERS, which this pass deliberately never establishes:
   refusal paths must not mutate the CFG, so loops are initialized with
   AVOID_CFG_MODIFICATIONS only.  Following the accepted replay-unroll
   discipline, the trip count is instead proven by a bounded constant-chain
   evaluation keyed to the pass's own dedicated-preheader proof:

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
// definition, on a call (potential clobber), or when no definition is found
// within a small bound.
static bool
constant_reaching_value (basic_block preheader, rtx reg, uint64_t *value)
{
  basic_block bb = preheader;
  for (unsigned depth = 0; depth != 4; ++depth)
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
// reduced to the counter mode's precision) -- the launch-loop unroll below
// consumes them to replace the removed per-trip updates.
static bool
provable_constant_trips (class loop *loop, basic_block preheader,
			 uint64_t *trips, rtx_insn **step_out = nullptr,
			 uint64_t *final_out = nullptr)
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
  if (!constant_reaching_value (preheader, counter, &init))
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
      if (!constant_reaching_value (preheader, bound, &bound_val))
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

/* Generic replay-hoist profitability model.

   Hoisting converts one in-loop recording pass per trip (re-recording the
   payload, WITH execution) into a single record-only capture pass in the
   preheader plus one playback launch per trip.  The in-loop recording is
   not pure overhead: while being recorded the payload performs the loop's
   real work, so removing it saves only the delivery/overhead premium
   over replay reissue, while the added preheader record-only pass
   executes nothing.  The pricing arithmetic lives in the next comment
   block and the full silicon-anchor derivation with the constants in
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

   TRIPS must be provable (see provable_constant_trips above).  An unknown
   or merely estimated trip count refuses the hoist, which keeps the
   emitted code byte-identical to the unhoisted form.  The decision inputs
   are exactly the provable trip count, the capture length, the longest
   contiguous sibling-launch run, and the cost-table constants.  */

/* Interlock-aware replay-hoist pricing (2026-08-18 recalibration; Lane
   BP's five-shape diagnosis + 14-shape silicon validation matrix,
   laneBP-evidence-20260818/DIAGNOSIS-AND-FIX-SPEC-laneBP.md; re-record
   branch re-derived 2026-08-19 against the Reduce-class and Log-class
   silicon anchors after the first spelling inverted both -- full
   derivation and the five-anchor table in rvtt-cost.md).

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
       before = deliver_record           ; pin-11 calibration restored:
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

   Planner-emitted macro launches (lane CK): an SFPLOADMACRO pattern is
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

static HOST_WIDE_INT
exec_interlocked_slots (replay_block const &block, replay_span span)
{
  // QSR carries no audited latency facts (the simulator refuses these
  // opcode semantics, rvtt-cost.md): the whole target is unpriceable,
  // matching the interlock scheduler's target-level refusal.
  if (TARGET_XTT_TENSIX_QSR)
    return -1;
  HOST_WIDE_INT slot = 0;
  HOST_WIDE_INT ready[16];
  uint32_t unproved = 0;	// regs whose pending producer is unaudited
  for (int i = 0; i != 16; ++i)
    ready[i] = 0;

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
      uint32_t deps = (e.lreg_read
		       | (planner_record ? 0 : e.lreg_write)) & 0xFFFF;
      if (deps & unproved)
	{
	  if (dump_file)
	    fprintf (dump_file, "  reissue-unproved edge: consumer insn %d"
		     " (deps 0x%x) of an unaudited producer (mask 0x%x)\n",
		     INSN_UID (pos->insn), deps, unproved);
	  return -1;
	}
      HOST_WIDE_INT at = slot;
      for (int i = 0; i != 16; ++i)
	if ((deps & (1u << i)) && ready[i] > at)
	  at = ready[i];
      unsigned words = get_attr_length (pos->insn) / 4;
      HOST_WIDE_INT done = at + words;
      if (e.next_slot_stall)
	++done;
      for (int i = 0; i != 16; ++i)
	if (e.lreg_write & (1u << i))
	  {
	    if (e.result_latency < 0)
	      unproved |= 1u << i;
	    else
	      {
		unproved &= ~(1u << i);
		ready[i] = done + e.result_latency;
	      }
	  }
      slot = done;
    }
  return slot;
}

/* Delivered instruction words of the span (multi-word instructions count
   each word; zero-length ghosts none).  */

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

static bool
hoist_profitable_p (class loop *loop, basic_block preheader,
		    replay_block const &block, replay_span payload,
		    bool body_rerecords, unsigned launch_run)
{
  bool record_hoist_mode
    = body_rerecords && riscv_tt_opt_replay_record_hoist > 0;
  uint64_t niter;
  bool trips_proven = provable_constant_trips (loop, preheader, &niter);
  /* Lane FW: a runtime trip count is admitted to the record-hoist
     pricing under a structural trips >= 1 fact -- the hoisted record
     lands in the DEDICATED preheader of a single-block loop, so
     executing the record implies at least one body execution (the
     preheader's single successor is the body); a zero-trip entry never
     reaches the record.  The pricing branch below decides admission at
     the 2-trip break-even.  */
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
	    fprintf (dump_file,
		     "record-hoist refused: record-hoist-trip-count-unproven\n");
	}
      return false;
    }

  HOST_WIDE_INT words = delivered_words (block, payload);
  /* Lane FW: under the record-hoist measurement flag the reissue-latency
     audit gate is discharged structurally rather than per-producer.  Its
     exec-side estimate feeds only the default model's pricing (the
     record-hoist branch below prices pure delivery: the executed word
     stream is IDENTICAL in both worlds by the fixed-encoding admission,
     so per-word execution -- audited or not -- cancels).  Its reissue
     soundness half is carried by the unhoisted world itself: every
     window here has at least two clones, so the identical word stream is
     ALREADY delivered by playback launches at expander pace in the
     unhoisted world (the always-on former's formation, the
     silicon-witnessed class); converting the first clone from
     exec-while-record delivery to one more playback of that same stream
     adds no reissue exposure a proven latency could bound.  The gate
     stays for the default hoist model, whose pricing consumes the
     estimate, and for unproven targets (no silicon-witnessed playback
     class to carry the discharge -- QSR keeps the refusal).  */
  bool reissue_gate_discharged
    = record_hoist_mode
      && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH);
  HOST_WIDE_INT eslots = 0;
  if (!reissue_gate_discharged)
    {
      eslots = exec_interlocked_slots (block, payload);
      if (eslots < 0)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Not hoisting: replay-reissue-latency-unproved: a"
		       " consumed payload producer carries no audited result"
		       " latency (loop %d, %ld words)\n",
		       loop->num, (long) words);
	      if (record_hoist_mode)
		fprintf (dump_file,
			 "record-hoist refused:"
			 " replay-reissue-latency-unproved\n");
	    }
	  return false;
	}
    }

  HOST_WIDE_INT exec = eslots * XTT_REPLAY_COST_REPLAY_SLOT_X100;
  HOST_WIDE_INT deliver_body = words * XTT_REPLAY_COST_RISC_PUSH_X100;
  HOST_WIDE_INT deliver_record
    = (1 + words) * XTT_REPLAY_COST_RISC_PUSH_X100;
  HOST_WIDE_INT min_benefit = (riscv_tt_replay_hoist_min_benefit >= 0
			       ? (HOST_WIDE_INT)
				 riscv_tt_replay_hoist_min_benefit
			       : XTT_REPLAY_HOIST_MIN_BENEFIT);

  /* Record-hoist measurement pricing (-mtt-tensix-optimize-replay-record-
     hoist, re-record bodies only).  The candidate window is proven
     iteration-invariant by the admission walk in hoist_preheader (every
     payload word fixed-encoding), so the EXECUTED word stream of the two
     worlds is identical: each playback launch expands to exactly the
     recorded words at the same stream positions the in-body clones held,
     and the hoisted no-exec record executes nothing (the Replay Expander
     consumes its payload in the frontend).  Execution-side terms therefore
     cancel between the worlds and the modeled delta is pure delivery: the
     in-body world re-delivers the capture word plus the payload every trip
     where the hoisted world delivers one launch word, a per-trip saving of
     `words' pushed words, bought once at the preheader record's full
     delivery plus the record-engine overhead.  This is the DX-F3
     issue-side accounting (laneDX-evidence-20260820, lcm decomposition:
     the in-loop `ttreplay 0,len,1,1' re-delivers len words per row while
     the hand kernel records once at init).  The default model's
     saturation/MAX pricing keeps the opposite verdict for this class from
     the Log-class silicon anchors (rvtt-cost.md, re-record derivation);
     this flag exists to build the silicon A/B legs for the DX-F3 class,
     the same measurement-flag pattern as -mtt-tensix-mop-form-force --
     with the difference that every structural proof still gates admission
     and the delivery model itself is monotone: for proven trips >= 2 the
     hoisted world delivers strictly fewer words on every execution.  */
  if (body_rerecords && riscv_tt_opt_replay_record_hoist > 0)
    {
      /* The hoisted world converts the first clone from inline delivery
	 to one more playback launch per trip: charge that added launch
	 boundary at the audited turnaround constant.  Lane EE's
	 calibration measured 1.3-1.8 cycles per launch boundary on
	 serial-chain windows (laneEE-evidence-20260821, boundary fits on
	 ceil/log/rsqrt) -- above the 0.7-slot table constant; the
	 under-charge (~60-110 cs/trip) is absorbed by the MIN_BENEFIT
	 margin and noted in rvtt-cost.md.  */
      HOST_WIDE_INT record_once
	= deliver_record + XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
      HOST_WIDE_INT per_trip
	= deliver_body - XTT_REPLAY_COST_TURNAROUND_X100;
      if (runtime_trips)
	{
	  /* Runtime trip count (lane FW; rvtt-cost.md RECORD-HOIST
	     RUNTIME-TRIP derivation).  The delivery delta is monotone in
	     the realized trip count: each trip saves per_trip delivered
	     centislots, bought once at record_once.  With trips >= 1
	     structural (dedicated preheader of a single-block loop) the
	     worst realized outcome is the single-trip exposure
	     record_once - per_trip -- about one record delivery -- and
	     every trip from 2 on wins.  Admit when the 2-trip benefit
	     clears the same audited margin proven trip counts must
	     clear; refuse by name otherwise.  */
	  HOST_WIDE_INT benefit2 = 2 * per_trip - record_once;
	  HOST_WIDE_INT exposure = record_once - per_trip;
	  if (dump_file)
	    fprintf (dump_file,
		     "Record-hoist runtime-trip pricing (loop %d): words"
		     " %ld, per_trip %ld, record_once %ld, 2-trip benefit"
		     " %ld (min %ld), single-trip exposure %ld\n",
		     loop->num, (long) words, (long) per_trip,
		     (long) record_once, (long) benefit2,
		     (long) min_benefit, (long) exposure);
	  if (per_trip <= 0 || benefit2 < min_benefit)
	    {
	      if (dump_file)
		fprintf (dump_file,
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
      HOST_WIDE_INT benefit = trips * per_trip - record_once;
      if (dump_file)
	fprintf (dump_file,
		 "Record-hoist pricing (loop %d): trips %ld, words %ld,"
		 " deliver_body %ld/trip, boundary %d/trip, record_once %ld,"
		 " benefit %ld (min %ld)\n",
		 loop->num, (long) trips, (long) words,
		 (long) deliver_body, XTT_REPLAY_COST_TURNAROUND_X100,
		 (long) record_once, (long) benefit,
		 (long) min_benefit);
      if (benefit < min_benefit)
	{
	  if (dump_file)
	    fprintf (dump_file,
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
  HOST_WIDE_INT after
    = MAX ((HOST_WIDE_INT) XTT_REPLAY_COST_RISC_PUSH_X100,
	   exec + XTT_REPLAY_COST_TURNAROUND_X100);
  // The re-record shapes split on which resource paces the in-loop
  // record-with-execution pass (rvtt-cost.md, re-record derivation):
  // execution-bound (exec >= deliver_record) exposes the record
  // engine's per-pass overhead on the critical path and hides the
  // hoisted preheader pass's delivery behind the loop's own execution
  // backlog (Reduce-class silicon A/B); delivery-bound keeps the
  // pin-11-calibrated delivery pricing (Log/Log1p refusals) with the
  // engine overhead absorbed in the per-word delivery slack.
  bool exec_bound_rerecord = body_rerecords && exec >= deliver_record;
  HOST_WIDE_INT record;
  HOST_WIDE_INT before;
  if (!body_rerecords)
    {
      before = MAX (deliver_body, exec);
      record = deliver_record + XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
    }
  else if (exec_bound_rerecord)
    {
      before = exec + XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
      record = XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
    }
  else
    {
      before = deliver_record;
      record = deliver_record + XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
      // Execution-saturation term (delivery-bound re-record bodies
      // only; silicon-witnessed on the unary-maxmin shape): when the
      // body's contiguous run of sibling launches of this same buffer
      // has enough execution surplus to hide the record pass's
      // delivery, hoisting relieves nothing per trip.  An
      // execution-bound record pass is never hidden this way: its cost
      // is its own execution plus the exposed record-engine overhead,
      // which no sibling surplus can absorb (Reduce-class silicon A/B,
      // rvtt-cost.md).
      HOST_WIDE_INT surplus = (HOST_WIDE_INT) launch_run
	* (exec - XTT_REPLAY_COST_RISC_PUSH_X100);
      if (surplus >= deliver_record)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Record delivery hidden: contiguous launch run %u exec"
		     " surplus %ld >= record delivery %ld\n",
		     launch_run, (long) surplus, (long) deliver_record);
	  before = after;
	}
    }
  HOST_WIDE_INT benefit = trips * (before - after) - record;

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
	     (long) deliver_body, (long) deliver_record, (long) record,
	     (long) before, (long) after, (long) benefit,
	     (long) min_benefit);

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
	      // Clones are within one block; a broken chain refuses the
	      // discount conservatively toward "separated" (fires are
	      // gated by the benefit model as before).
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
		// Zero-length ghost: no delivered word.
		continue;
	      if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
		  && riscv_tt_opt_dst_autoincr > 0)
		// Typed per-row Dst increment the later auto-increment
		// pass absorbs around replay launches.
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

/* Playback launches THIS PASS emitted this function (lane FW): their
   recorded slot content is the pass's own audited payload, so the
   record-hoist loop replay-preservation walk may admit them where a
   user-authored launch (unknowable recorded content) refuses.  */
static std::vector<rtx_insn *> formed_playback_launches;

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

  return preheader && !(entry->flags & EDGE_ABNORMAL) && single_succ_p (preheader)
    ? preheader : nullptr;
}

// A volatile store whose address is not provably outside the
// instruction-FIFO aperture can deliver ANY word -- including a REPLAY
// record that re-records hoisted slots (lane FW fail-closed widening of
// the loop scan below; the flag-gated record-hoist path re-audits
// refused loops with the interval walk in rvtt-macro-epoch.cc, which
// also classifies the stored WORD).  Named data objects other than the
// recorded ABI anchor __instrn_buffer (crosscall precedent) and stack
// slots are provably not the FIFO; a constant address outside the
// aperture range is too; everything else refuses.
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

// A raw asm or an unknown callee can own or overwrite replay state without
// exposing that fact to this function's RTL.  Typed barriers are harmless
// here: they remain outside the payload and do not change the selected replay
// slots.  A typed owner is conservatively a boundary for this first hoisting
// implementation even though global slot accounting has already excluded its
// declared range.  Volatile stores that could target the instruction FIFO
// refuse fail-closed (they could push a REPLAY record word); see
// volatile_store_maybe_fifo_p.
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

static basic_block
hoist_preheader (replay_sequence const &seq, replay_block const &block,
		 bitmap dirty_bbs)
{
  basic_block bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return nullptr;
  bool record_hoist = riscv_tt_opt_replay_record_hoist > 0;
  if (loop->num_nodes != 1 || loop->header != bb)
    {
      /* Lane FW: under the record-hoist flag a MULTI-BLOCK loop admits
	 when the capture bb dominates the loop latch -- the capture
	 (and so its clone deliveries) executes on every completed trip,
	 which is the fact the per-trip pricing consumes; the
	 replay-preservation audit below walks EVERY block of the loop,
	 so slot liveness needs no single-block shape.  Real measured
	 vehicles are exactly this shape: the profiler zone code splits
	 the tile loop into several blocks (buffer-management branches
	 around an always-executed body).  A capture bb that does NOT
	 dominate the latch executes conditionally -- its per-trip
	 delivery saving is unpriced -- and refuses by the same name.  */
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
	      fprintf (dump_file, "Not hoisting: candidate bb %d is not a single-bb loop header\n",
		       bb->index);
	      if (record_hoist)
		fprintf (dump_file,
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
      /* Lane FW: under the record-hoist flag, re-audit the refused loop
	 with the interval-resolving replay-preservation walk (LLK tile
	 loops always carry raw sync words and computed FIFO pushes; the
	 walk proves them unable to modify replay-buffer state, admits
	 this pass's own playback launches -- the multi-record calendar
	 -- and keeps everything unresolvable refused by name).  The
	 walk covers every block of the loop (multi-block tile loops
	 admit under the latch-dominance shape check above).  */
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
		       "Not hoisting: loop contains call, opaque asm, or replay owner\n");
	      /* For the record-hoist this is also the in-loop slot-liveness
		 proof: an in-loop replay owner (or an asm/call that could hide
		 one) could re-record the hoisted capture's slots between the
		 preheader record and a later trip's launch.  Every other
		 window this pass forms lives entirely inside one basic block
		 (record to last launch), the loop is single-block, and
		 persistent-slot marking excludes the hoisted range from all
		 later formation, so this refusal closes the only re-record
		 path into the hoisted slots.  */
	      if (record_hoist)
		fprintf (dump_file,
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
	  fprintf (dump_file, "Not hoisting: loop has no dedicated preheader\n");
	  if (record_hoist)
	    fprintf (dump_file,
		     "record-hoist refused: record-hoist-no-dedicated-preheader\n");
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
	    fprintf (dump_file,
		     "record-hoist refused: record-hoist-preheader-recording-open\n");
	}
      return nullptr;
    }

  /* Record-hoist invariance admission (before pricing): every payload
     word must be fixed-encoding -- hard LREGs, constants, and compiler
     scratch only.  A GPR or MEM operand means the delivered instruction
     word is composed at run time; its value at the preheader is not
     proven equal to its value at the original in-body record point, so
     the recorded program could differ from what each trip's launch must
     replay.  No rebase model exists for such words in this first
     increment: refuse by name.  (The flag-off hoist path keeps its
     original post-pricing check below, byte-identically.)  */
  if (record_hoist)
    for (auto pos = block.data () + seq.clones.front ().begin,
	  end = block.data () + seq.clones.front ().end;
	 pos != end; ++pos)
      if (!pos->empty && !fixed_replay_rtx_p (PATTERN (pos->insn)))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist refused: record-hoist-variant-encoding:"
		     " payload insn %d is a run-time-composed word\n",
		     INSN_UID (pos->insn));
	  return nullptr;
	}

  /* Admission-side mirror of the fail-closed re-record sweep's rule 1
     (lane FW): a Dst-store payload whose no-exec record would land in a
     preheader that itself sits inside a natural loop is EXACTLY the
     shape unhoist_hazard_rerecords un-hoists at the end of transform
     (noexec-rerecord-dststore-composition-unaudited, lane FJ) -- and the
     un-hoist's identity restoration is relative to the HOISTED world
     (every launch becomes an inline payload copy), a strict delivery
     pessimization against never having hoisted.  Forming a provably
     doomed hoist is a known-losing transform: refuse it here by the
     sweep's own name and keep the in-body formation byte-identically.
     (The dominating loop-free-preheader Dst-store class stays admitted:
     the sweep's rule 3 keeps it -- the witnessed init-record class.)  */
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
	      if (dump_file)
		fprintf (dump_file,
			 "record-hoist refused:"
			 " noexec-rerecord-dststore-composition-unaudited:"
			 " Dst-store payload, preheader bb %d inside loop %d"
			 " (the re-record sweep would un-hoist)\n",
			 preheader->index, ph_loop->num);
	      return nullptr;
	    }
    }

  /* Downstream-fallback composition pricing (lane FZ; rvtt-cost.md
     "RECORD-HOIST x MOD-WRITE COMPOSITION").  The record-hoist pricing
     below is licensed by the streams-identical premise: the hoisted and
     unhoisted worlds EXECUTE the same word stream, so the modeled delta
     is pure delivery.  A no-exec record hoisted to within the audited
     drained-frontend window of a row the dst-autoincr pass would
     otherwise transform into a mod-write voids that premise: the
     dst-autoincr group guard is certain to refuse the group (the
     silicon-refuted no-exec-record x mod-write composition, fail-closed
     and correct), so the hoisted world executes the explicit-increment
     fallback while the unhoisted world executes the mod-write form --
     different executed streams whose delta the delivery-only model
     cannot price.  The one silicon point on the composed shape measured
     it NET NEGATIVE (lcm-fresh ON-28, +6.0 cyc/tile against the
     unhoisted+mod-write world; rvtt-cost.md entry), so a hoist that
     induces the fallback refuses by name and keeps today's bytes.  The
     oracle mirrors the group guard's own distance semantics and audited
     window (single source, rtl-rvtt-dst-autoincr.cc); no distance an
     admitted hoist leaves behind can flip the guard.  Gated on the
     dst-autoincr pass actually running: with it disabled both worlds
     keep the explicit increments and the premise holds.  */
  if (record_hoist && TARGET_XTT_TENSIX && riscv_tt_opt_dst_autoincr > 0)
    {
      unsigned dist = 0;
      if (rvtt_dst_autoincr_hoist_capture_composition_p (preheader, &dist))
	{
	  if (dump_file)
	    fprintf (dump_file,
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

  if (!hoist_profitable_p (loop, preheader, block, seq.clones.front (),
			   /*body_rerecords=*/true,
			   max_contiguous_launch_run (seq, block)))
    return nullptr;

  for (auto pos = block.data () + seq.clones.front ().begin,
	end = block.data () + seq.clones.front ().end;
       pos != end; ++pos)
    if (!pos->empty && !fixed_replay_rtx_p (PATTERN (pos->insn)))
      {
	if (dump_file)
	  fprintf (dump_file, "Not hoisting: payload insn %d is not fixed encoding\n",
		   INSN_UID (pos->insn));
	return nullptr;
      }

  return preheader;
}

/* No-exec captures THIS PASS hoisted into preheaders this function, for
   the fail-closed re-record sweep at the end of transform () (lane FJ,
   FE-F1 follow-up; see unhoist_hazard_rerecords).  User-authored records
   are never entered here and stay untouched.  */
static std::vector<rtx_insn *> formed_noexec_captures;


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

  /* The first insn of the emitted sequence is the no-exec capture:
     register it for the fail-closed re-record sweep.  */
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
	     "Hoisted no-exec capture [%u,+%u) to preheader bb %d; %u playbacks\n\n",
	     replay_start, length, preheader->index,
	     unsigned (seq.clones.size ()));
  return length;
}

// Remove or adjust those sequences that are invalidated by having used SEQ.
// (SEQ itself auto-invalidates).

static bool
active_invalidate (replay_active &active, replay_sequence *seq, unsigned max_length)
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

      // Keep this one
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
		&& fixed_replay_rtx_p (PATTERN (insn)))
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
	// Recording state may be open here (unprovable user epoch).
	continue;

      replay_block info;
      replay_sequence seq;
      if (!counted_loop_payload (loop, info, seq))
	continue;
      if (!span_companion_sound_p (info, seq.clones.front (), sticky))
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
      // The counted-loop payload is its own single clone; across trips the
      // launch is always separated from the next by the loop-control
      // delivery, so the contiguous launch run is 1.
      if (!hoist_profitable_p (loop, preheader, info, seq.clones.front (),
			       /*body_rerecords=*/false,
			       /*launch_run=*/1))
	continue;

      auto spans = available_replay_spans (replay_spans, persistent_slots);
      auto slot = std::find_if (spans.begin (), spans.end (),
				[&seq] (replay_span span)
				{ return span.end >= seq.length; });
      if (slot == spans.end ())
	continue;

      unsigned length
	= replace_hoisted_sequence (seq, info, slot->begin, preheader);
      std::fill (persistent_slots.begin () + slot->begin,
		 persistent_slots.begin () + slot->begin + length, true);
      if (dump_file)
	fprintf (dump_file,
		 "Counted-loop replay payload bb %d length %u captured at %u\n",
		 bb->index, length, slot->begin);
    }
}

// ---- Complete unroll of proven-trip replay-launch loops ----
//
// After hoisting, a counted loop's body can be reduced to pure replay
// delivery: playback launches of an already-recorded capture plus typed
// Dst-counter steps, with only the induction-variable update and the
// conditional branch as per-trip work.  Driving that loop control through
// the RISC costs two delivered scalar words per trip and separates
// consecutive launches in the final instruction stream.  When the trip
// count is provable (the same provable_constant_trips discipline the hoist
// itself uses -- estimated or profile counts refuse), the body replicates
// textually: emit TRIPS copies of the per-trip delivery back to back,
// materialize the counter's proven final value once (later passes delete it
// when dead), and remove the loop control entirely.  This is the
// no-source-pragma counterpart of the accepted replay-aware complete unroll:
// the gimple-side unroll request needs the payload before recording, while
// this shape only exists after replay formation has hoisted the capture.
//
// Admission is purely structural.  Every non-debug insn in the single-block
// body must be one of:
//   (a) a fixed-encoding TTREPLAY playback launch,
//   (b) a typed TTINCRWC with constant operands (the per-trip Dst step the
//       counted-loop capture leaves explicit; the Dst auto-increment pass
//       runs later and sees every launch site with equivalent RWC coverage),
//   (c) the loop's single counter-step insn, or
//   (d) the final conditional jump.
// Anything else -- another scalar insn, a recording, a non-playback replay
// owner, an asm, a call, memory, USE/CLOBBER markers -- refuses and leaves
// the loop byte-identical.  No operation identity, opcode calendar,
// coefficient value, or instruction-word fingerprint participates.
//
// Cost: the per-trip benefit is the two removed loop-control words (positive
// for every proven trips >= 2); the only cost is straight-line code size,
// bounded by the cost-table constant XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS on
// the total delivered words of the unrolled run.
//
// Interaction with the hoist's execution-saturation context term: the
// contiguous launch run this unroll creates exists only in the hoisted
// world, so it never re-prices the hoist decision.  The LAUNCH_RUN input of
// hoist_profitable_p measures sibling launches present in the body
// independently of the hoist under evaluation; a run manufactured by a
// post-hoist delivery optimization is a consequence of the decision, not
// context for it (see rvtt-cost.md).

static bool
unroll_launch_loop (class loop *loop, bitmap dirty_bbs)
{
  basic_block header = loop->header;
  if (loop->num_nodes != 1 || loop->header != loop->latch)
    return false;
  if (bitmap_bit_p (dirty_bbs, header->index))
    // Recording state may be open here (unprovable user epoch).
    return false;

  // Pragma scope.  "#pragma GCC unroll" governs payload duplication: the
  // gimple replay-unroll REQUEST defers to it and an annotated payload is
  // never replicated.  This unroll is a delivery transformation on the
  // residual launch loop the (equally pragma-blind, post-reload) hoist
  // leaves behind: the capture stays recorded once and only delivered
  // launch words replicate -- the same class of rewrite as the hoist
  // itself, which has always fired on annotated loops.  Loop structures
  // are rebuilt after reload, so loop->unroll is normally cleared here;
  // honor it if it ever survives (a preserved "#pragma GCC unroll 1" must
  // keep even its launch loop).
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
  // A fallthrough cannot re-enter its own block, so the backedge must be
  // the taken branch and the fallthrough the unique exit.
  if (e_branch->dest != header || e_fall->dest == header
      || (e_branch->flags & EDGE_ABNORMAL) || (e_fall->flags & EDGE_ABNORMAL))
    return false;

  // A loop without a playback launch is silently out of scope; refusal
  // diagnostics below are only meaningful for launch-carrying bodies.
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

  // Classify the body.  DELIVERY collects the per-trip delivered words in
  // program order; STEP is the single scalar counter update.
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
	      && fixed_replay_rtx_p (pattern))
	    {
	      ++launches;
	      delivery.push_back (insn);
	      continue;
	    }
	  if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
	      && fixed_replay_rtx_p (pattern))
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
  if (!provable_constant_trips (loop, preheader, &trips, &counter_step,
				&final_value))
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d trip count is not"
		 " provably constant\n", header->index);
      return false;
    }
  // The one scalar insn must be exactly the proven counter step; any other
  // scalar state would be silently frozen by removing the loop.
  if (counter_step != step || trips < 2)
    return false;

  rtx step_set = single_set (step);
  rtx counter = SET_DEST (step_set);
  machine_mode counter_mode = GET_MODE (counter);
  rtx final_rtx = gen_int_mode (final_value, counter_mode);
  // The counter's proven exit value replaces the removed per-trip updates.
  // Post-reload only single-insn constants may be materialized directly.
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

  // ---- Execute-while-recording increment ----
  //
  // The hoisted capture in the dedicated preheader records without
  // executing, so the first trip's launch re-delivers work the recording
  // already streamed through the buffer.  When the structural conditions
  // below hold, flip the capture to execute-while-loading and drop the
  // first trip's launch: the payload's first execution happens at the
  // record itself (the same semantics the planner CC path emits, and the
  // in-place replace_sequence has always used).  Conditions, all
  // refusing by leaving the plain unroll behavior:
  //   - not Quasar (cannot exec while capturing there; the same guard
  //     replace_sequence applies);
  //   - the first delivered word of the trip is a playback launch of
  //     exactly the capture's span (payload execution moves from that
  //     launch site to the record site);
  //   - the preheder's only Tensix content after the capture is the
  //     capture's own payload: scalar insns cannot interact with Tensix
  //     state, so crossing them preserves the payload's CC, Dst, and RWC
  //     context; any other Tensix word between record and first launch
  //     refuses.
  // The typed Dst auto-increment pass models an executing capture as a
  // row of its own (ROW_CAPTURE_EXEC), so its later ownership placement
  // sees the record-time execution site like any other row.
  rtx_insn *exec_capture = nullptr;
  rtx_insn *exec_payload_end = nullptr;
  bool drop_first_launch = false;
  if (!(riscv_tt_fix_qsr_replay > 0) && riscv_tt_opt_replay_exec_record > 0)
    {
      replay_span lead_span;
      if (is_replay_insn (lead_span, delivery.front ()) == REPLAY_playback)
	{
	  // Find the capture of this span in the preheader and prove it is
	  // the last Tensix content (past its own payload words).
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
		      // An empty-template asm (the compiler memory-barrier
		      // idiom) emits nothing; every real asm is an
		      // unclassified word.  Position decides (lane FJ,
		      // FE-F1 follow-up): the transformation moves the
		      // payload's execution from the first launch back to
		      // the record, so only words BETWEEN the record and
		      // that launch are crossed -- a word before the
		      // record is outside the motion window, exactly like
		      // the typed Tensix words the branch below already
		      // admits (the LLK per-tile wrapper's raw
		      // TTI_STALLWAIT word sits there on every
		      // llk_math_eltwise_sfpu_common.h tile loop).  A raw
		      // word inside the payload span corrupts the typed
		      // slot count, and one after the payload is crossed
		      // by the motion: both keep refusing.
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
		  continue;	// scalar work / empty barrier / pre-record raw word
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
	      // is_replay_insn's raw span carries {begin = start slot,
	      // end = length}.
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
		// Tensix work BEFORE the record retires before it and is
		// unaffected by executing the payload at the record point.
		continue;
	      if (!get_attr_length (pinsn))
		// Zero-length architectural markers deliver no word.
		continue;
	      // A Tensix word between the capture's payload and the first
	      // launch: refuse (the payload's execution would cross it).
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

  // Commit.  Replicate the per-trip delivery TRIPS-1 further times in body
  // order (the scalar counter step commutes with every delivered word: it
  // touches only the counter register), set the counter's proven final
  // value, and remove the loop control.  The loop structure loses its
  // backedge; record the pending fixup before mutating the CFG.
  loops_state_set (LOOPS_NEED_FIXUP);

  if (drop_first_launch)
    {
      // Flip the record to execute-while-loading, drop the first trip's
      // now-redundant launch, and emit the WHOLE unrolled delivery run in
      // the preheader directly after the executed payload: trip 1's
      // remaining typed Dst steps followed by trips 2..N.  Everything
      // crossed is scalar work (proven above), and the later Dst
      // auto-increment ownership pass then sees the record-time execution
      // row and every launch row in ONE block -- the same shared-placement
      // shape an in-place capture produces.
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

// ---- Launch conversion of isomorphic instruction runs ----
//
// After formation, a payload recorded in the replay buffer may have further
// executions that were not textually identical to the recorded sequence --
// typically the final copy of a completely unrolled counted loop, whose
// separate register allocation chose different temporaries (and may clobber
// registers the recorded rows preserve).  When such a run is
// effect-isomorphic to a payload under a register value map, executing it as
// one more launch is equivalent provided every register whose final contents
// differ between the two worlds is dead after the run.
//
// Matching is purely structural: identical instruction codes and
// non-register operands in lockstep, with register operands related by an
// evolving value map (a use of a run-local definition must correspond to the
// matched definition; a live-in use must be the identical hard register).
// No operation names, opcode calendars, immediate fingerprints, or raw
// encodings participate in any decision.
//
// Refusals, all leaving the function byte-identical:
//   - any lockstep mismatch (code, immediate, structure, or value map);
//   - a register of a differing definition pair live after the run;
//   - a run whose trailing Dst-advance context differs from the uniform
//     trailing context of the payload's other execution sites: the typed Dst
//     auto-increment ownership pass runs later and must see every execution
//     site with equivalent RWC coverage, so a conversion may not create the
//     only uncovered site;
//   - payloads whose recorded contents, buffer span, or execution sites are
//     ambiguous, and runs not dominated by their recording.

struct conv_capture
{
  rtx_insn *insn = nullptr;
  basic_block bb = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;
  bool valid = true;
  std::vector<rtx_insn *> members; // slot-occupying payload insns, in order
  rtx_insn *shadow_end = nullptr;  // last member
  unsigned sites = 0;
  int trailing = -2;               // uniform site context; -2 unset, -1 none
};

struct conv_launch
{
  rtx_insn *insn;
  unsigned begin;
  unsigned len;
  conv_capture *payload = nullptr;
};

// A typed TTINCRWC advancing only Dst by a constant stride, mirroring the
// Dst auto-increment pass's row separator test.

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

// The trailing Dst-advance context of an execution site whose last issued
// instruction is LAST: the stride of an immediately following pure typed Dst
// TTINCRWC, or -1.

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

// An insn eligible to appear in a matched run: replay-safe, slot-occupying,
// fixed-encoding Tensix work.

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
  return fixed_replay_rtx_p (PATTERN (insn));
}

// Is hard register REGNO consumed by a real instruction on any path from
// after FROM (in BB) before being fully redefined?  Mirrors the load-macro
// pass's lifetime test (reg_referenced_p before reg_set_p, a later full
// definition ends the old value's lifetime).  The exit block is not a
// consumer: SFPU register state is not an implicit cross-function
// interface in this programming model -- an explicit hand-off is an
// ordinary instruction definition or use (sfpwritelreg/sfpreadlreg), the
// ABI's blanket call-saved marking otherwise has no residual-contents
// contract (kernels clobber the file without saving; accepted-risk
// precedent from the region-scoped ownership review), and calls are
// conservatively treated as consumers.  Declared asm register operands
// appear as pattern references and are honored; undeclared asm dependence
// on residual register contents has no contract.

/* Architectural LREG interface markers (the typed variable-LREG read and
   write patterns) observe or pin a specific physical register out of band:
   a read is modeled as a fresh definition whose value is architecturally
   the register's current contents.  Any such marker is conservatively a
   consumer.  */

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

// Structural isomorphism of one payload/run instruction pair under the
// evolving value map.  DEFINED_P/DEFINED_R track registers defined so far in
// the payload and run; P2R/R2P is the current correspondence; PAIRS collects
// every definition pair for the liveness proof.

struct conv_map
{
  std::map<unsigned, unsigned> p2r, r2p;
  std::vector<std::pair<unsigned, unsigned>> pairs;
  std::map<unsigned, bool> defined_p, defined_r;
};

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
	// Live-in use: the identical register, not shadowed by a run-local
	// definition.
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
      // FALLTHROUGH
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

static bool
conv_match_insn (rtx_insn *p, rtx_insn *r, conv_map &map)
{
  if (recog_memoized (p) != recog_memoized (r))
    return false;
  std::vector<std::pair<unsigned, unsigned>> pending_defs;
  if (!conv_match_rtx (PATTERN (p), PATTERN (r), false, map, pending_defs))
    return false;
  // Definitions take effect after all of the instruction's uses.
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

static void
convert_isomorphic_runs (function *cfn, bitmap dirty_bbs)
{
  // Rediscover captures and launches structurally.
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
	      bail = true; // variable replay: buffer contents unprovable
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
	    // Recording state may already be open around this capture.
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
	      // Anything in the shadow that does not occupy a slot (or that
	      // this conversion could not itself have matched) makes the
	      // recorded contents unsuitable.
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
      // Buffer-span ambiguity: overlapping spans invalidate all parties;
      // each launch must resolve to exactly one capture.
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

      // Uniform trailing Dst-advance context across every execution site.
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
	    // Recording state may be open here (unprovable user epoch).
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
		  // The recording must reach this run on every path.
		  if (cap->bb == bb)
		    {
		      // Same block: the shadow must precede the run.
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

	      // Trailing Dst-advance context parity with the other sites.
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

	      // Every register of a differing definition pair must be dead
	      // after the run: the launch clobbers the payload's registers
	      // and no longer writes the run's.
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

	      // Convert: one launch replaces the whole run.
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

/* ==== Counted-row parameterized formation (canonicalization phase) ====

   docs/COUNTED_ROW_FORMATION.md.  The word-exact sequence discovery above
   cannot form a record over template-expanded "rows" that repeat modulo
   (a) per-row immediate materializations and (b) the register allocator's
   rotation of equivalent assignments.  This phase, gated by
   -mtt-tensix-optimize-counted-row-formation (default off), REWRITES such
   parameterized clone families into word-exact form so the existing
   discovery, budgeting, capture and launch machinery -- unchanged -- forms
   one parameterized row program:

   1. Invariant-violation exclusion.  A member position whose clones
      disagree only on compile-time-constant operands is EXCLUDED from the
      residual and moved to its clone's head, where it will be issued
      between launches.  The excluded class is derived from the invariance
      proof itself (the clones' reaching constant operands differ) plus the
      audited effect set: a single-slot materialization whose only SFPU
      dataflow is the write (and read-modify-write) of its single
      destination register and which carries no CC, Dst, RWC, or
      configuration effect.  No instruction identity participates.

   2. Clone canonicalization for register rotation.  Clones are matched
      against the first clone under an evolving register value map (the
      launch-conversion matcher's exact test, run as a transform): every
      hard-register value whose mapping differs is REWRITTEN to the
      recorded register -- the whole value, definition and every use, with
      a linear in-block value analysis proving the rewrite sound.  A
      live-in value pinned by a fixed multi-definition instruction is
      bridged with one all-lanes register move issued before the launch
      and priced as a slot.

   3. Slot-budget honesty.  Candidates are ranked by modeled slot saving
      (shorter residuals with more clones win ties); a family whose
      residual exceeds the largest available replay-buffer span refuses by
      name.  The budget never grows by stealing user-recorded slots.

   Refusals, each by name in the dump, leaving code byte-identical:
   counted-row-excluded-member-unmovable, counted-row-residual-not-uniform,
   counted-row-map-live-out, counted-row-slot-budget,
   counted-row-rename-interference, counted-row-rename-constraint,
   counted-row-lane-state, counted-row-bridge-clobber.  */

// LREG index domain helpers: xtt_effect_set masks are over L0..L15; the
// allocatable SFPU hard registers are L0..L7.
static inline uint32_t
crf_reg_bit (unsigned regno)
{
  gcc_checking_assert (SFPU_REG_P (regno));
  return 1u << (regno - SFPU_REG_FIRST);
}

// A member admissible for exclusion from a parameterized record: a
// single-slot immediate materialization -- every non-register operand a
// compile-time constant, its only SFPU dataflow the write (and
// read-modify-write) of its single destination register, and its audited
// effect set free of CC, Dst, RWC, and configuration effects.  Derived
// from the effect audit and the cross-clone invariance proof; never from
// instruction identity.

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
      || !fixed_replay_rtx_p (pattern))
    return false;

  // cc_read (the audited model of a lane-gated write) is admissible: the
  // movement window is proven free of CC writes, so the lane state at the
  // new position is the state at the old one.  A CC write is not.
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

// Classified SFPU register mentions of an rtx, by pattern position.

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
      // Address registers are uses even under a store destination.
      crf_scan_rtx (XEXP (x, 0), false, defs, uses, unhandled);
      return;

    case SUBREG:
    case STRICT_LOW_PART:
    case ZERO_EXTRACT:
      // Partial or indirect register access: not a whole-value def/use.
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

// One whole-register value in the linear in-block dataflow model.

struct crf_value
{
  int def_pos = -1;		 // -1: live into the block
  rtx_insn *def_insn = nullptr;
  unsigned reg = 0;		 // original hard register
  std::vector<unsigned> use_positions;
  int last_pos = -1;
  bool live_out = false;	 // consumed on some path after the block
  bool fixed = false;		 // multi-definition or hidden-effect def
  bool poisoned = false;	 // range crosses an opaque event or shadow
  int renamed_to = -1;		 // planned final hard register (-1 = keep)
};

// Per-position facts for the whole block.

struct crf_position
{
  rtx_insn *insn;
  uint32_t defs = 0, uses = 0;	 // SFPU reg masks (pattern + audited extra)
  bool eligible = false;	 // may be a family member
  bool excludable = false;	 // admissible for invariant-violation exclusion
  bool barrier = false;		 // breaks family runs
  bool empty = false;		 // zero-length marker: transparent
  bool cc_write = false;
  bool opaque = false;
  uint32_t marker_mask = 0;	 // zero-length LREG interface marker
  unsigned phash = 0;		 // parameterized structural hash
  int value_of_def[8];		 // value index defined per reg, or -1
  int value_of_use[8];		 // value index consumed per reg, or -1
  crf_position () { memset (value_of_def, -1, sizeof (value_of_def));
		    memset (value_of_use, -1, sizeof (value_of_use)); }
};

struct crf_block
{
  basic_block bb;
  std::vector<crf_position> pos;
  std::vector<crf_value> values;
};

// Parameterized structural hash: instruction code and full structure with
// SFPU register numbers abstracted, and with constant operands abstracted
// only for exclusion-admissible instructions (their constants never enter
// the record; every other constant is part of the recorded word).

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
	// FALLTHROUGH
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

// Build the linear value model and per-position facts for BB.  Returns
// false when the block cannot be modeled (variable user capture).

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
	// Live into the block.
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
		  // Audited architectural effects beyond the pattern's
		  // registers: hidden dataflow, modeled as fixed def+use.
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
	  // Unknown reads and writes: poison every live value, extend
	  // their ranges, and act as a run barrier.
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

      // Uses consume the current values.
      for (int i = 0; i != 8; ++i)
	if (p.uses & (1u << i))
	  p.value_of_use[i] = value_at (i, pos_ix);

      // Definitions begin new values.
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
	  // Values consumed by a user capture payload feed a recorded
	  // program with launch sites this analysis cannot see.
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
	// Transparent to discovery: re-attached to the containing clone
	// at verification, moved to its head or tail by dataflow.
	continue;
      p.eligible = conv_run_insn_p (insn);
      if (p.eligible)
	p.phash = crf_param_hash (insn, false);
      else
	p.barrier = true;
    }

  // Values still live at the block's end that some path consumes.
  for (int i = 0; i != 8; ++i)
    if (cur[i] >= 0)
      blk.values[cur[i]].live_out
	= conv_reg_consumed_after_p (SFPU_REG_FIRST + i, BB_END (bb), bb);

  return !blk.pos.empty ();
}

// One clone of a parameterized family: a span of positions and the
// evolving register value map relating it to the family's first clone.

struct crf_clone
{
  unsigned begin, end;		 // half-open position span
  std::map<unsigned, unsigned> p2r, r2p; // seed reg <-> clone reg
  std::map<unsigned, bool> defined_p, defined_r;
};

struct crf_seq
{
  unsigned parent = 0;
  unsigned hash = 0;
  unsigned length = 0;		 // members (excludable/empty not counted)
  std::vector<crf_clone> clones; // clones[0] is the seed
};

// Structural lockstep match of one seed/clone member pair under the
// evolving map.  Extends the launch-conversion matcher in exactly one
// way: a live-in register pair may differ, recorded in the map as a
// canonicalization requirement rather than failing.  Everything else --
// codes, modes, structure, constants, run-local value correspondence --
// must agree.

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
	  // Run-local value: must be the corresponding definition.
	  return map.defined_p.count (pa) && map.defined_r.count (rb)
	    && map.p2r.count (pa) && map.p2r[pa] == rb
	    && map.r2p.count (rb) && map.r2p[rb] == pa;
	// Live-in use: record the (possibly differing) correspondence.
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
      // FALLTHROUGH
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

// Transparent positions never become members: zero-length markers and
// exclusion-admissible materializations (the latter are re-attached to
// the clone that contains them at verification).

static inline bool
crf_transparent_p (crf_position const &p)
{
  return p.empty || p.excludable;
}

// Grow parameterized sequences over the block, mirroring build_sequences'
// grow-by-one architecture with the lockstep matcher in place of word
// equality.

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
      // A joining clone matches all members from scratch against the
      // sequence's seed.
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
  // Seed self-match establishes the identity map and def sets.
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

static void
crf_build_sequences (std::vector<crf_seq> &list, crf_block &blk,
		     unsigned max_residual)
{
  list.clear ();
  list.push_back (crf_seq ()); // null
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

// A selected family, fully verified: the rename plan, bridges, and
// member movements ready to apply.

struct crf_plan
{
  unsigned residual = 0;		   // recorded slot words (= members)
  int saving = 0;			   // modeled issued-slot saving
  std::vector<crf_clone> clones;	   // surviving, disjoint
  // per clone: member positions (block indices), lockstep with the seed's
  std::vector<std::vector<unsigned>> members;
  // value index -> final hard reg
  std::map<unsigned, unsigned> renames;
  // value index -> clone whose lockstep walk required the rename
  // (bystander cascade swaps carry -1)
  std::map<unsigned, int> rename_source;
  // per clone: bridge moves (dest_reg <- src_reg) inserted at clone head
  std::vector<std::vector<std::pair<unsigned, unsigned>>> bridges;
  // per clone: excludable positions inside the span moving to the head
  // (its consumer is a member) or the tail (consumers all later)
  std::vector<std::vector<unsigned>> moves_head;
  std::vector<std::vector<unsigned>> moves_tail;
};

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

// The final register of a value under the plan.

static inline unsigned
crf_final_reg (crf_block const &blk, crf_plan const &plan, int vix)
{
  auto it = plan.renames.find (vix);
  return it != plan.renames.end () ? it->second : blk.values[vix].reg;
}

// Final-assignment def/use register masks of the instruction at POS.

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

// Can the excluded materialization at POS move to the clone HEAD (before
// ANCHOR) or TAIL (after LAST), under the FINAL register assignment?
// Ordinary dependence check against every crossed instruction; a crossed
// CC write would change the member's lane gating.  Positions in SKIP move
// with it (order preserved) and are transparent.

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

// True read-modify-write tie of INSN's definition of REG: a source
// operand carries a matching constraint naming the destination operand,
// so both share one encoded register field and a rename must carry the
// register's previous value along.  A source that merely happens to name
// the same register in an independently encoded field is not a tie.

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

// Verify one candidate family and build its plan.  Returns true with PLAN
// filled on success; every refusal dumps its taxonomy name.

static bool
crf_verify_family (crf_block &blk, crf_seq &seq, unsigned budget,
		   bool sticky, crf_plan &plan, unsigned ref)
{
  unsigned length = seq.length;

  // Overlap triage: clones ascending by begin; keep a maximal disjoint set.
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
      if (dump_file)
	fprintf (dump_file, "Refusing counted-row family [%u,%u):"
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

  // Residual soundness under possibly-enabled shadow coupling, and the
  // v1 multi-result restriction (companion-group boundary semantics stay
  // with the word-exact machinery).
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    for (unsigned m = 0; m != length; ++m)
      {
	crf_position const &mp = blk.pos[plan.members[c][m]];
	rtx_insn *insn = mp.insn;
	xtt_effect_set e = rvtt_insn_effects (insn);
	xtt_multiresult_group group;
	if (rvtt_multiresult_group (insn, e, &group))
	  {
	    if (dump_file)
	      fprintf (dump_file, "Refusing counted-row family [%u,%u):"
		       " counted-row-residual-not-uniform: member %u is a"
		       " multi-result instruction\n",
		       plan.clones.front ().begin,
		       plan.clones.front ().end, m);
	    return false;
	  }
	// Rename planning (seed_def_reg below and the clone definition
	// roles) requires a single canonical definition register per
	// member.  A member defining several registers (a two-register
	// SFPSWAP, or audited hidden dataflow modeled as fixed def+use)
	// has no single seed register: refuse by name rather than
	// asserting.
	if (popcount_hwi (mp.defs) > 1)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Refusing counted-row family [%u,%u):"
		       " counted-row-multidef-member: member %u defines"
		       " multiple registers\n",
		       plan.clones.front ().begin,
		       plan.clones.front ().end, m);
	    return false;
	  }
	if (sticky && (e.lreg_write & 0xF))
	  {
	    if (dump_file)
	      fprintf (dump_file, "Refusing counted-row family [%u,%u):"
		       " shadow-state-unproved: member %u writes the value"
		       " bank under possibly-enabled index tracking\n",
		       plan.clones.front ().begin,
		       plan.clones.front ().end, m);
	    return false;
	  }
      }

  // Rename planning with seed chain-closure.  SEED_REMAP retargets the
  // seed's own definition at member M when a cross-clone value carries a
  // def role and a live-in role that demand different registers.  A
  // refusal specific to one clone drops that clone; only seed-side
  // refusals and non-converging chain closure refuse the whole family.
  plan.bridges.assign (plan.clones.size (), {});
  plan.moves_head.assign (plan.clones.size (), {});
  plan.moves_tail.assign (plan.clones.size (), {});
  std::map<unsigned, unsigned> seed_remap; // member index -> target reg

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
	  if (dump_file)
	    fprintf (dump_file, "Refusing counted-row family [%u,%u):"
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
      // Which member's definition role planned a value's rename, for
      // chain closure from the live-in side.
      std::map<unsigned, unsigned> plan_def_member;

      uint32_t seed_writes = 0;
      for (unsigned m = 0; m != length; ++m)
	{
	  int r = seed_def_reg (m);
	  if (r >= 0)
	    seed_writes |= crf_reg_bit (r);
	}

      // Plan one value's rename with read-modify-write chain closure:
      // when a renamed value's definition also consumes the register's
      // previous value (an RMW half), that value must move to the same
      // register or the single encoded register field would tear.
      // Returns 1 on success, 0 on an in-plan conflict (caller decides),
      // -1 on an unrenameable value.
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
	    // RMW chain: the definition consumes the register's previous
	    // value through a TIED operand (one encoded field).
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
		  if (dump_file)
		    fprintf (dump_file, "Dropping counted-row clone"
			     " [%u,%u): counted-row-residual-not-uniform:"
			     " diverges at member %u\n",
			     plan.clones[c].begin, plan.clones[c].end, m);
		  break;
		}

	      crf_position const &cp = blk.pos[cpos];

	      // New live-in correspondences discovered at this member.
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
			  // Def role vs live-in role conflict: close the
			  // chain by retargeting the seed definition the
			  // def role mirrors, then replan.
			  auto dm = plan_def_member.find (vu);
			  if (dm != plan_def_member.end ())
			    {
			      seed_remap[dm->second] = s;
			      remapped = true;
			      break;
			    }
			}
		      drop_clone = int (c);
		      if (dump_file)
			fprintf (dump_file, "Dropping counted-row clone"
				 " [%u,%u): counted-row-map-live-out:"
				 " live-in value in r%u cannot move to"
				 " r%u\n", plan.clones[c].begin,
				 plan.clones[c].end, t, s);
		      break;
		    }
		  // Unrenameable live-in: bridge with one all-lanes move,
		  // unless the recorded program would clobber the source
		  // still needed later.
		  bool used_later = false;
		  for (unsigned up : v.use_positions)
		    if (up >= plan.clones[c].end)
		      used_later = true;
		  if (v.live_out)
		    used_later = true;
		  if (used_later && (seed_writes & crf_reg_bit (t)))
		    {
		      drop_clone = int (c);
		      if (dump_file)
			fprintf (dump_file, "Dropping counted-row clone"
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

	      // Definition roles.
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
		      // The value already carries a live-in role demanding
		      // a different register: close the seed chain at this
		      // member and replan.
		      seed_remap[m] = it->second;
		      remapped = true;
		      break;
		    }
		  if (int (v.reg) != want || it != plan.renames.end ())
		    {
		      if (v.fixed || v.poisoned || v.live_out)
			{
			  drop_clone = int (c);
			  if (dump_file)
			    fprintf (dump_file, "Dropping counted-row"
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
			  if (dump_file)
			    fprintf (dump_file, "Dropping counted-row"
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

      // Seed remap entries are renames of the seed's own def values.
      planning_clone = -1;
      if (!remapped && drop_clone < 0 && !refused)
	for (auto const &sr : seed_remap)
	  {
	    crf_position const &p = blk.pos[plan.members[ref][sr.first]];
	    int vd = p.value_of_def[exact_log2 (p.defs)];
	    gcc_assert (vd >= 0);
	    if (plan_rename (vd, sr.second) != 1)
	      {
		if (dump_file)
		  fprintf (dump_file, "Refusing counted-row family"
			   " [%u,%u): counted-row-map-live-out: seed chain"
			   " closure needs an unrenameable value\n",
			   plan.clones.front ().begin,
			   plan.clones.front ().end);
		return false;
	      }
	  }

      if (!remapped && drop_clone < 0 && !refused)
	{
	  // Excludable materializations: every one whose value feeds a
	  // clone member relocates to that clone's head (canonical-register
	  // recips serialize between launches, the hand's own delivery
	  // discipline); one inside a span but feeding nothing in the
	  // family moves out past the tail.  Movement legality is
	  // VALUE-based: uses must follow the new position, no CC write or
	  // opaque instruction may be crossed (lane-state constancy), and
	  // the whole-block occupancy simulation of the final assignment
	  // is the authoritative gate.
	  for (auto &mh : plan.moves_head)
	    mh.clear ();
	  for (auto &mt : plan.moves_tail)
	    mt.clear ();

	  // Clone of the first member-use of an excludable's value,
	  // following chains through other excludables (the RMW pair).
	  std::map<unsigned, int> consumer_clone; // position -> clone or -1
	  std::vector<unsigned> excl_all;
	  for (unsigned pos = 0; pos != blk.pos.size (); ++pos)
	    if (blk.pos[pos].excludable)
	      excl_all.push_back (pos);
	  std::map<unsigned, unsigned> member_clone; // member pos -> clone
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

	  // Lane-state/opacity constancy over a movement range.
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
		      // Already directly ahead of the clone (only
		      // transparent positions between): leave it.
		      bool clean = true;
		      for (unsigned ix = pos + 1; ix != anchor; ++ix)
			if (!crf_transparent_p (blk.pos[ix]))
			  clean = false;
		      if (clean)
			continue;
		    }
		  // Uses must all follow the new position; a use by a
		  // fellow excludable relocating to the same head keeps
		  // its original order there.
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
			  if (dump_file)
			    fprintf (dump_file, "Refusing counted-row"
				     " family [%u,%u):"
				     " counted-row-excluded-member-"
				     "unmovable: insn %d cannot reach its"
				     " consumer clone\n",
				     plan.clones.front ().begin,
				     plan.clones.front ().end,
				     INSN_UID (blk.pos[pos].insn));
			  return false;
			}
		      if (dump_file)
			fprintf (dump_file, "Dropping counted-row clone"
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
	      // No consumer in the family: if inside a clone span, move
	      // out past the tail.
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
			if (dump_file)
			  fprintf (dump_file, "Dropping counted-row clone"
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

      // Occupancy of the final assignment, with the bystander cascade:
      // a conflict against an untouched renameable value swaps it into
      // the evacuated register; a non-cascadable conflict drops the
      // clone whose lockstep walk required the conflicting rename.
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
	      // Not cascadable: drop the responsible clone.
	      int src = -1;
	      for (int v : { a, b })
		if (v >= 0 && plan.rename_source.count (v)
		    && plan.rename_source[v] >= 0)
		  src = plan.rename_source[v];
	      if (src >= 0 && unsigned (src) != ref)
		{
		  if (dump_file)
		    fprintf (dump_file, "Dropping counted-row clone"
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

  // Modeled saving: every non-seed clone's residual collapses to one
  // launch; bridges are bought slots; the capture word is one slot.
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

  // A plan that changes nothing is the word-exact machinery's territory.
  {
    bool any_change = !plan.renames.empty ();
    for (unsigned c = 0; c != plan.clones.size () && !any_change; ++c)
      any_change = !plan.moves_head[c].empty ()
	|| !plan.moves_tail[c].empty () || !plan.bridges[c].empty ();
    if (!any_change)
      return false;
  }

  return true;
}

// Whole-block occupancy verification of the plan's final register
// assignment, over the stream in its FINAL order (excluded members moved,
// bridges inserted), plus the lane-state window proof: no CC write may
// fall inside the span affected by any rewritten value (state-constancy
// makes the rewrites lane-exact for any entry lane state).

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

  // Bridged values: their uses inside the bridging clone move to the
  // bridge read.
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

  // Bridge destination values: from the bridge write to the end of the
  // clone's span (conservative).
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
      // Abutment (def at 2t+1 after uses at 2t) is already encoded in the
      // timestamps; any remaining overlap is a conflict.
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
		  fprintf (dump_file, "Refusing counted-row family"
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

  // Lane-state window: the rewrites are lane-exact only while the lane
  // state is constant over every affected value's span.
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
	  if (dump_file)
	    fprintf (dump_file, "Refusing counted-row family [%u,%u):"
		     " counted-row-lane-state: CC write (insn %d) inside"
		     " the rewritten window\n", plan.clones.front ().begin,
		     plan.clones.front ().end,
		     INSN_UID (blk.pos[pos].insn));
	  return false;
	}

  return true;
}

// Apply a verified plan: queue every register replacement in one change
// group (recog and constraints re-verify each rewritten instruction),
// then fix the dead-note registers, move the excluded members, and issue
// the bridge moves.

static bool
crf_apply (crf_block &blk, crf_plan &plan)
{
  // Replace registers per value, ROLE-AWARE: a definition rename touches
  // only definition positions (SET_DEST outside a MEM), a use rename only
  // use positions.  One instruction can carry two same-numbered registers
  // belonging to different values with different targets (the abutting
  // accumulator chain).  Renames can chain (L5->L1 while L1->L3), so all
  // locations are collected against the ORIGINAL patterns first.
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
      if (dump_file)
	fprintf (dump_file, "Refusing counted-row family [%u,%u):"
		 " counted-row-rename-constraint: a rewritten instruction"
		 " failed re-recognition\n", plan.clones.front ().begin,
		 plan.clones.front ().end);
      return false;
    }

  // Dead/unused notes riding the rewritten instructions.
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

  // Move the excluded members and issue the bridges.
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      rtx_insn *anchor = blk.pos[plan.members[c].front ()].insn;
      for (unsigned mpos : plan.moves_head[c])
	{
	  rtx_insn *insn = blk.pos[mpos].insn;
	  reorder_insns (insn, insn, PREV_INSN (anchor));
	}
      for (auto const &br : plan.bridges[c])
	{
	  rtx mv = gen_rtx_SET (gen_rtx_REG (XTT32SImode, br.first),
				gen_rtx_REG (XTT32SImode, br.second));
	  rtx_insn *mvi = emit_insn_before (mv, anchor);
	  gcc_assert (recog_memoized (mvi) >= 0);
	}
      rtx_insn *tail_after = blk.pos[plan.members[c].back ()].insn;
      for (unsigned mpos : plan.moves_tail[c])
	{
	  rtx_insn *insn = blk.pos[mpos].insn;
	  reorder_insns (insn, insn, tail_after);
	  tail_after = insn;
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


// Form the record and launches for an applied plan, mirroring
// replace_sequence: the first clone hosts the capture (executing while
// recording where the target allows), every other clone collapses to a
// launch.  The consumed slots are marked persistent so the word-exact
// machinery below never reallocates them.

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

// Driver: canonicalize parameterized counted-row families so the ordinary
// word-exact discovery below records one parameterized row program per
// family.  Budget honesty: candidates are ranked by modeled slot saving
// with shorter residuals winning ties, and the local budget model shrinks
// by each applied family's residual.

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

      // Iterate: apply the best verifiable family, rescan, repeat.
      // Instructions of an applied family are frozen: a later family may
      // not rewrite values they define or use, or it would break the
      // earlier family's canonical form.
      std::set<rtx_insn *> frozen;
      for (unsigned round = 0; round != 8 && budget >= MIN_SEQUENCE;
	   ++round)
	{
	  crf_block blk;
	  if (!crf_scan_block (bb, blk))
	    break;

	  std::vector<crf_seq> list;
	  crf_build_sequences (list, blk, budget);

	  // Rank candidates: modeled saving descending, residual
	  // ascending (budget honesty), position ascending.
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
	      // Identity families (no divergence, nothing excludable in
	      // any span) are the word-exact machinery's own territory.
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
	  /* Deterministic-rank truncation: 64 is an uncited engineering
	     search budget (DG2 ref-cap class, FH audit FHO-8), byte-stable
	     because the ranking above is total.  A candidate dropped here is
	     silently not formed -- widening or a cost.md derivation is the
	     counted-row owner's follow-up.  */
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

	  // Verify every ranked candidate and apply the best VERIFIED
	  // plan: a high-bound family that lost most of its clones must
	  // not shadow a smaller family that survived whole.
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
		  if (dump_file)
		    fprintf (dump_file, "Refusing counted-row family"
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
	      // Best-fit slot span (smallest that holds the record).
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
	      gcc_assert (found); // budget model guaranteed a fit
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

// The replay pass looks for sequences of instructions that repeat and replaces
// the repeated portions w/ a REPLAY instruction

/* Audited mod-write classification for the no-exec record placement
   obligation (lane FL, FH-1; rvtt-cost.md AUDITED COMPOSITION FACT
   "no-exec record composition").  The silicon-refuted composition is a
   no-exec recording window opening while a mod-write's positional-state
   retirement (the audited W_drain window) can still be in flight; the
   guard in rtl-rvtt-dst-autoincr.cc prices it only for that pass's OWN
   groups, so records this pass places must audit the same fact against
   the stream's audited mod-write classes:

     - the typed SETC16 address-modifier programming word (the user
       builtin and every pass-owned emission reach the same
       rvtt_ttsetc16_int pattern), and
     - a typed Dst access through a non-no-increment address modifier
       (the mod0-6 store/load class; the effect vocabulary's ADDR_MODE
       leg answers NONE exactly for the audited no-increment mode, so
       any other answer is a positional mutator or unaudited -- both
       refuse).

   Issue-time RWC writers (TTINCRWC, the SET/FACE separator class, raw
   pure-Dst/RWC words) are NOT in the class: the crossing model records
   them as re-anchoring issue-time words, and the celu/eqz-class wrapper
   adjacency (records behind raw STALLWAIT-class words) is
   silicon-witnessed good across many pins -- treating undecoded words
   as hazards would refuse the witnessed-good class.  Undecodable words
   (calls, non-raw asm, opaque typed words) instead earn ZERO cover in
   the distance walk: they can never manufacture separation.

   Returns true when INSN is in the audited mod-write class; *WORDS is
   INSN's frontend issue-word cover credit (the dst-autoincr counting:
   typed Tensix words at their machine-description length, raw
   pure-RWC words at the one-word extraction contract, scalar words at
   their issue length, jumps at the one-word conservative floor).  */

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
	*words = 1;		/* audited issue-time separator class */
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
    return false;		/* zero cover, outside the audited class */
  *words = get_attr_length (insn) / 4;
  if ((e.dst_mem_write || e.dst_mem_read)
      && e.rwc.kind != xtt_rwc_effect_t::NONE)
    return true;
  return false;
}

/* Whether an audited mod-write lies within WINDOW frontend issue-slot
   words upstream of CAP over ANY CFG path (the minimum-distance
   direction of the dst-autoincr guard, walked backward).  The distance
   is the issue-word cover strictly between the mod-write and CAP; a
   path that accumulates >= WINDOW cover, or ends at the function entry,
   is proven separated.  *DIST reports the refuting distance.  */

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

  /* Backward min-distance walk over predecessors, pruned at WINDOW.  */
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

/* Fail-closed no-exec re-record sweep (lane FJ; rvtt-cost.md "no-exec
   record composition", delivery-boundary paragraph).  A pass-hoisted
   NO-EXEC capture whose placement block lies inside a loop re-ingests
   its payload every iteration; when that payload carries a Dst store,
   the re-ingestion follows the previous iteration's launch-delivered
   stores at runtime pacing no static model prices.  The silicon
   witnesses: lcm-fresh ON+record-hoist (hang, lane ES 2x2) and
   sparse_k_filter ON-25 at runtime trip 32 (hang, lane FE F1) -- and
   lane FJ's device datum that the same shape hangs WITH EXPLICIT
   TTINCRWC rows too, so the composition is the re-record x launches
   x Dst-store-payload, not the mod-write alone.  The witnessed-good
   exec-while-record conversion (fleet silicon: minmax, sdpa, where,
   typecast, lcm ON-set) is the intended deliverer of these shapes;
   when it has NOT fired by the end of the pass, this sweep un-hoists
   the capture by name: every launch of the span is replaced by an
   inline copy of the payload (a launch executes exactly the payload,
   so this is the identity the capture was formed from), and the record
   and its never-executed shadow are deleted.  Storeless payloads
   (celu/eqz-class wrapper records, silicon-good across many pins) and
   loop-free placements (xielu preamble, single-loop preheaders) are
   untouched.  Only captures this pass formed are swept: user-authored
   records are the user's own contract.

   Second placement obligation (lane FL, FH-1; same sweep, same
   identity-restoring action): a still-no-exec formed capture whose
   recording window can open within the audited W_drain issue-word
   window of an audited mod-write (placement_modwrite_hazard_p; the
   rvtt-cost.md AUDITED COMPOSITION FACT's distance boundary, priced
   with the same exported constant the dst-autoincr group guard uses,
   rvtt_modwrite_drained_frontend_window) is un-hoisted by name -- the
   compiler must never FORM the silicon-refuted wedge adjacency it
   refuses to compose elsewhere.  Loop-free placements are audited
   too: the wedge condition is the record's ingestion inside the
   retirement window; once is enough.  Captures proven separated
   (>= W_drain issue words on every path, or no audited mod-write
   upstream at all) keep their bytes -- the xielu/gcd/lcm preamble and
   init placements are this proven class.  */

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
	continue; /* Converted to exec-while-record: witnessed class.  */
      basic_block bb = BLOCK_FOR_INSN (cap);
      if (!bb)
	continue;
      class loop *loop = bb->loop_father;
      /* Rule 1 (lane FJ) applies to in-loop placements only (a
	 loop-free record's payload executes once, the witnessed
	 class); rule 2 (lane FL) audits every placement.  */
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

      /* Pre-collect every launch of this exact span (block-level) so the
	 dominance-based rule 3 can inspect them before we decide to fire.  */
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

      /* Rule 1 (lane FJ): in-loop re-record with a Dst-store payload.
	 Rule 2 (lane FL, FH-1): recording window opens inside the
	 audited W_drain window of an audited mod-write.  Rule 3 (lane FS,
	 FP-3): a still-no-exec Dst-store capture whose record does NOT
	 dominate every launch of its span (or has no in-function launch
	 at all) is live-at-exit relative to that launch -- the recorded
	 store can be delivered from a launch the record never executed
	 before, on a sibling CFG path or, since the per-thread Replay
	 Expander buffer PERSISTS across the soft-reset kernel-invocation
	 boundary (lane FS silicon model, laneFS-evidence-20260822: EXP-1
	 cross-invocation + EXP-2 within-launch cross-function delivery),
	 from a caller-loop re-entry or an entirely later kernel.  The
	 intra-function reach walks (FJ dst-autoincr, FL W_drain) cannot
	 see that consumer; forming the adjacency is the same silicon-
	 refuted wedge (ES 2x2 / FE-F1 / FJ HANG-3), so fail closed.  The
	 dominating-preamble class (xielu/gcd/lcm init record dominates its
	 in-loop launches, all device PASS) is preserved: a dominating
	 record executes before every launch in the same invocation.  All
	 three rules share the identity-restoring un-hoist below.  */
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
	    fprintf (dump_file, "Replay refusal: noexec-rerecord-dststore-"
		     "composition-unaudited (capture bb %d in loop %d "
		     "un-hoisted, %u launches inlined)\n",
		     bb->index, loop->num, launches);
	  else if (modwrite_rule)
	    fprintf (dump_file, "Replay refusal: noexec-record-modwrite-"
		     "window-unaudited (capture bb %d %u issue words after "
		     "an audited mod-write, window %u; un-hoisted, "
		     "%u launches inlined)\n",
		     bb->index, modwrite_dist, window, launches);
	  else
	    fprintf (dump_file, "Replay refusal: noexec-record-dststore-"
		     "nondominating-launch-persist-unaudited (capture bb %d "
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

static void
transform (function *cfn, unsigned buffer_size)
{
  basic_block bb;
  std::vector<replay_span> replay_spans;
  formed_noexec_captures.clear ();
  formed_playback_launches.clear ();

  /* Fail-closed raw-capture census.  The allocator's only view of
     already-claimed slots is the typed rvtt_ttreplay_int stream: an
     opaque asm never reaches is_replay_insn (the TYPE_TENSIX filter
     below skips it before classification), so a hand-authored raw
     ".ttinsn" word carrying the architectural REPLAY opcode would
     leave replay_spans untouched and the formation below would
     silently allocate -- and launch over -- slots the raw word already
     owns.  The raw word's slot range is not derivable here, and
     capture cannot be distinguished from launch by the opcode byte
     alone, so any such word refuses ALL replay allocation in the
     function by name.  Typed owners still declare their ranges and are
     excluded exactly by the span subtraction below; raw words that do
     not carry the REPLAY opcode remain ordinary sequence boundaries;
     a non-constant raw word remains an opaque boundary as before (it
     cannot be decoded, and its slots cannot be proven claimed --
     unchanged first-increment behavior, recorded as a limitation).  */
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *census_insn;
      FOR_BB_INSNS (bb, census_insn)
	{
	  uint32_t raw_word;
	  if (NONDEBUG_INSN_P (census_insn)
	      && asm_noperands (PATTERN (census_insn)) >= 0
	      && rvtt_raw_ttinsn_word (census_insn, &raw_word)
	      && rvtt_raw_replay_owner_word_p (raw_word))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "replay-raw-capture-present: raw .ttinsn word "
			 "0x%08x (insn %d) carries the REPLAY opcode; "
			 "refusing all replay allocation in this function\n",
			 raw_word, INSN_UID (census_insn));
	      return;
	    }
	}
    }

  /* Recording-epoch scoping (ownership-epoch model, TOP3-2 layer 2).

     A user fixed capture opens a recording epoch over the replay
     buffer's recording state.  When the typed instruction stream proves
     the epoch closed (rvtt_replay_epoch_close: the payload's typed
     lengths account for exactly the declared words), formation may
     proceed around it -- in particular a payload of typed multi-result
     instructions (indexed SFPSWAP, eight-definition SFPTRANSP) is
     retained in the capture model rather than refusing the whole
     function.  When the epoch is unprovable (opaque asm payload, or the
     payload crosses the block), only the region where recording state
     may still be open is refused: from the capture up to the next
     explicit replay-owner instruction on each path.  An explicit typed
     owner operation is an epoch boundary by the owner's contract -- a
     TTREPLAY word issued while recording were still active would be
     swallowed by the recording rather than executed, so a well-formed
     owner protocol proves all prior recording closed.  Blocks whose
     possibly-recording region contains any slot-occupying word (or
     opaque asm) are excluded from formation and from the launch-family
     rewrites below.

     This scoping is gated on the replay-hoist optimization: in the
     default configuration the legacy whole-function refusal is preserved
     byte-identically.  */
  bool scoped = riscv_tt_opt_replay_hoist > 0;
  auto_bitmap dirty_bbs;      // excluded from formation/rewrites
  auto_bitmap open_exit_bbs;  // recording state possibly open at exit

  // Determine replay_spans ranges
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
	      // Inside a typed-closed user capture payload.
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
		    // A slot-occupying word in a possibly-recording
		    // region.
		    bitmap_set_bit (dirty_bbs, bb->index);
		}
	      else if (shadow && get_attr_length (insn))
		shadow--;
	      continue;
	    }
	  if (scoped)
	    // Owner epoch boundary: possibly-open recording state is
	    // proven closed at an explicit owner operation.
	    open_unprovable = false;
	  else if (shadow)
	    {
	      if (dump_file)
		fprintf (dump_file, "User capturing or replaying during capture\n");
	      return;
	    }

	  if (type == REPLAY_variable_capture)
	    // Using remainder of the buffer.
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
				     "User capturing or replaying during capture\n");
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

	  // Cut out [from,to) from replay_spans.
	  for (auto pos = replay_spans.begin (), end = replay_spans.end (); pos != end;)
	    if (pos->end <= span.begin)
	      ++pos; // not reached, continue
	    else if (pos->begin >= span.end)
	      break; // gone past, we're done
	    else if (pos->begin >= span.begin && pos->end <= span.end)
	      replay_spans.erase (pos), --end; // entirely consumed
	    else if (pos->begin >= span.begin)
	      {
		pos->begin = span.end; // snip front
		break;
	      }
	    else if (pos->end <= span.end)
	      pos->end = span.begin, ++pos; // snip back
	    else
	      {
		// punch hole, and we're done
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

  /* Propagate possibly-open recording state over the CFG until each path
     reaches an explicit replay-owner instruction (the epoch-boundary
     axiom above).  Blocks whose possibly-recording prefix contains any
     slot-occupying word or opaque asm are excluded from formation.  */
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

  // Convert replay_spans to be [start, +length)
  for (auto &slot : replay_spans)
    slot.end -= slot.begin;
  // Sort in decreasing length
  std::sort (replay_spans.begin (), replay_spans.end (),
	     [] (replay_span const a, replay_span const b)
	     {
	       return a.end > b.end
		 || (a.end == b.end && a.begin < b.begin);
	     });
  // Remove spans that are too short
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

  // Shadow-coupling possibility gates the companion-pairing refusals in
  // span_companion_sound_p; computed once per function.
  bool sticky = rvtt_shadow_coupling_possible (cfn);

  std::vector<bool> persistent_slots (buffer_size, false);
  if (riscv_tt_opt_replay_hoist > 0)
    hoist_counted_loops (cfn, replay_spans, persistent_slots, dirty_bbs,
			 sticky);

  // Counted-row parameterized formation: canonicalize eligible clone
  // families so the word-exact discovery below records one parameterized
  // row program per family (docs/COUNTED_ROW_FORMATION.md).
  if (riscv_tt_opt_counted_row > 0)
    canonicalize_counted_rows (cfn, replay_spans, persistent_slots,
			       dirty_bbs, sticky);

  replay_block info; // insn info
  replay_list list; // list of sequences
  replay_map map; // map of sequences
  replay_active active; // pointers to active (under-consideration) sequences
  FOR_EACH_BB_FN (bb, cfn)
    {
      if (bitmap_bit_p (dirty_bbs, bb->index))
	// Recording state may be open here (unprovable user epoch).
	continue;
      if (!scan_insns (info, bb))
	continue;

      // This is N^2
      unsigned lwm = build_sequences (map, list, info, replay_spans.front ().end);

      active_triage (info, active, list, lwm);

      // This is the knapsack problem :(
      auto spans = available_replay_spans (replay_spans, persistent_slots);
      if (spans.empty ())
	continue;

      while (!active.empty ())
	{
	  auto *seq = pick_replay (active, spans.front ().end, info, sticky);
	  if (!seq)
	    break;

	  auto slot = spans.begin ();
	  // Is there a better fit?
	  // FIXME: should we only accept exact fit?
	  for (auto probe = slot;
	       ++probe != spans.end () && probe->end >= seq->length;)
	    slot = probe;

	  /* The record-hoist flag admits the preheader hoist attempt on
	     its own (its pricing branch owns the re-record class); with
	     only the plain hoist flag the attempt behaves exactly as
	     before.  */
	  basic_block preheader
	    = (riscv_tt_opt_replay_hoist > 0
	       || riscv_tt_opt_replay_record_hoist > 0)
	    ? hoist_preheader (*seq, info, dirty_bbs) : nullptr;
	  unsigned len = preheader
	    ? replace_hoisted_sequence (*seq, info, slot->begin, preheader)
	    : replace_sequence (*seq, info, slot->begin);
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

	  // Remove unuseable sequences
	  active_invalidate (active, seq, spans.front ().end);
	}
    }

  // The launch-loop unroll and the launch conversion of isomorphic runs
  // are part of the replay-hoist mechanism family: the shapes they target
  // are produced by the replay-aware complete unroll and the hoist above,
  // and the flag keeps the default configuration byte-identical.
  if (riscv_tt_opt_replay_hoist > 0)
    {
      unroll_launch_loops (cfn, dirty_bbs);
      convert_isomorphic_runs (cfn, dirty_bbs);
    }

  /* Fail-closed: any pass-hoisted no-exec capture the exec-while-record
     conversion did not reach, placed on a loop with a Dst-store payload,
     is the silicon-refuted re-record composition -- un-hoist it.  */
  unhoist_hazard_rerecords (cfn);
}

namespace {

const pass_data pass_data_rvtt_replay =
{
  RTL_PASS, /* type */
  "rvtt_replay", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
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
    return TARGET_XTT_TENSIX && riscv_tt_opt_replay > 0;
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
}; // class pass_rvtt_replay

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_replay (gcc::context *ctxt)
{
  return new pass_rvtt_replay (ctxt);
}
