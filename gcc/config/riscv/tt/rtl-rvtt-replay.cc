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
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt-protos.h"

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

// Minimum acceptable sequence length
// FIXME: We should experiment, 3 might also work. 2 is unlikely to be a win
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

static replay_sequence *
pick_replay (replay_active &active, unsigned limit)
{
  replay_sequence *result = nullptr;
  unsigned best = 0;

  for (auto *seq : active)
    {
      gcc_assert (seq->clones.size () > 1);
      if (seq->length > limit)
	break;
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

// A raw asm or an unknown callee can own or overwrite replay state without
// exposing that fact to this function's RTL.  Typed barriers are harmless
// here: they remain outside the payload and do not change the selected replay
// slots.  A typed owner is conservatively a boundary for this first hoisting
// implementation even though global slot accounting has already excluded its
// declared range.
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
		      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)))
	    return false;
      }
  return true;
}

static basic_block
hoist_preheader (replay_sequence const &seq, replay_block const &block)
{
  basic_block bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return nullptr;
  if (loop->num_nodes != 1 || loop->header != bb)
    {
      if (dump_file)
	fprintf (dump_file, "Not hoisting: candidate bb %d is not a single-bb loop header\n",
		 bb->index);
      return nullptr;
    }
  if (!loop_preserves_replay_p (loop))
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: loop contains call, opaque asm, or replay owner\n");
    return nullptr;
    }

  gcov_type backedges = expected_loop_iterations_unbounded (loop);
  if (backedges < 1
      || backedges * (seq.length + 1) <= backedges + 1)
    {
      if (dump_file)
	fprintf (dump_file, "Not hoisting: loop backedge estimate is %ld\n",
		 (long) backedges);
      return nullptr;
    }

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

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader && dump_file)
    fprintf (dump_file, "Not hoisting: loop has no dedicated preheader\n");
  return preheader;
}

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

  for (auto const &clone : seq.clones)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
      emit_insn_after (replay, block[clone.end - 1].insn);
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
   loop-control instructions may surround it, but ordinary memory, calls,
   opaque asm, configuration/counter operations, dynamic instruction words,
   and explicit replay ownership make the whole loop ineligible.  */
static bool
counted_loop_payload (class loop *loop, replay_block &info,
		      replay_sequence &seq)
{
  if (loop->num_nodes != 1 || loop->header != loop->latch
      || !loop_preserves_replay_p (loop))
    return false;

  rtx_insn *insn;
  FOR_BB_INSNS (loop->header, insn)
    if (NONDEBUG_INSN_P (insn))
      {
	if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0
	    || contains_mem_rtx_p (PATTERN (insn)))
	  return false;
	if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	    && get_attr_type (insn) == TYPE_TENSIX
	    && (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
		|| !fixed_replay_rtx_p (PATTERN (insn))))
	  return false;
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

  gcov_type iterations = expected_loop_iterations_unbounded (loop) + 1;
  if (iterations < 2
      || iterations * (length - 1) <= length + 1)
    return false;

  seq = replay_sequence (0, 0, length);
  seq.clones.emplace_back (0, info.size ());
  return true;
}

static void
hoist_counted_loops (function *cfn,
		     std::vector<replay_span> const &replay_spans,
		     std::vector<bool> &persistent_slots)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;

      replay_block info;
      replay_sequence seq;
      if (!counted_loop_payload (loop, info, seq))
	continue;

      basic_block preheader = dedicated_loop_preheader (loop);
      auto spans = available_replay_spans (replay_spans, persistent_slots);
      auto slot = std::find_if (spans.begin (), spans.end (),
				[&seq] (replay_span span)
				{ return span.end >= seq.length; });
      if (!preheader || slot == spans.end ())
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

// The replay pass looks for sequences of instructions that repeat and replaces
// the repeated portions w/ a REPLAY instruction

static void
transform (function *cfn, unsigned buffer_size)
{
  basic_block bb;
  std::vector<replay_span> replay_spans;

  // Determine replay_spans ranges
  replay_spans.emplace_back (0, buffer_size);
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *insn;
      unsigned shadow = 0;
      FOR_BB_INSNS (bb, insn)
	{
	  if (GET_CODE (insn) != INSN)
	    continue;
	  rtx pattern = PATTERN (insn);

	  if (GET_CODE (pattern) == USE)
	    continue;
	  if (GET_CODE (pattern) == CLOBBER)
	    continue;

	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;

	  replay_span span;
	  auto type = is_replay_insn (span, insn);
	  if (type == REPLAY_none)
	    {
	      if (shadow && get_attr_length (insn))
		shadow--;
	      continue;
	    }
	  if (shadow)
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
		shadow = span.end;

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
      if (shadow)
	{
	  if (dump_file)
	    fprintf (dump_file, "User capturing across basic block\n");
	  return;
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

  std::vector<bool> persistent_slots (buffer_size, false);
  if (riscv_tt_opt_replay_hoist > 0)
    hoist_counted_loops (cfn, replay_spans, persistent_slots);

  replay_block info; // insn info
  replay_list list; // list of sequences
  replay_map map; // map of sequences
  replay_active active; // pointers to active (under-consideration) sequences
  FOR_EACH_BB_FN (bb, cfn)
    {
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
	  auto *seq = pick_replay (active, spans.front ().end);
	  if (!seq)
	    break;

	  auto slot = spans.begin ();
	  // Is there a better fit?
	  // FIXME: should we only accept exact fit?
	  for (auto probe = slot;
	       ++probe != spans.end () && probe->end >= seq->length;)
	    slot = probe;

	  basic_block preheader = riscv_tt_opt_replay_hoist > 0
	    ? hoist_preheader (*seq, info) : nullptr;
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
    if (riscv_tt_opt_replay_hoist > 0)
      loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    transform (fn, riscv_tt_replay_size);
    if (riscv_tt_opt_replay_hoist > 0)
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
