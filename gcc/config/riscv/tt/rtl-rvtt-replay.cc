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
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
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
// loop body executes) on success; any structural mismatch refuses.
static bool
provable_constant_trips (class loop *loop, basic_block preheader,
			 uint64_t *trips)
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
   real work, so removing it saves only the RISC-delivery premium over
   replay reissue, while the added preheader record-only pass is bought at
   full delivery price and executes nothing.  Model, in centislots
   (hundredths of one issue slot), with every constant taken from the
   target cost table (rvtt-cost.md):

     deliver = (1 + length) * XTT_REPLAY_COST_RISC_PUSH_X100
     execute = length * XTT_REPLAY_COST_REPLAY_SLOT_X100
     before  = deliver                        ; per-trip in-loop recording,
                                              ; execution overlapped under
                                              ; the dominant delivery cost,
                                              ; UNLESS hidden (below)
     after   = max (RISC_PUSH_X100, execute)  ; per-trip launch + replay
                                              ; reissue of the payload
     benefit = trips * (before - after)       ; per-trip delivery saved
               - deliver                      ; added record-only preheader
                                              ; pass (delivers, no work)

   Execution-saturation context term (the LAUNCH_RUN parameter).  The
   `before = deliver' pricing assumes the loop body is delivery-bound, so
   removing the record pass's delivered words shortens the trip.  That is
   false when the body's sibling launches of the SAME capture buffer are
   contiguous in the final instruction stream: a contiguous run of R
   launches occupies the issue plane for R * execute centislots while
   delivering only R * RISC_PUSH_X100, and the record pass's delivery
   streams into that execution shadow instead of extending the trip.
   When the run's execution surplus covers the record pass's delivery,

     launch_run * (execute - RISC_PUSH_X100) >= deliver

   the per-trip relief is ~zero: before = after, so benefit degenerates to
   -deliver (the preheader pass is pure cost) and the hoist refuses.  A
   single launch can never satisfy this (length * SLOT - PUSH <
   (1 + length) * PUSH for every length), so counted-loop hoists -- whose
   per-trip launch is always separated from the next trip's by the loop
   control words -- and all single-instance shapes are unaffected and pass
   LAUNCH_RUN = 1.  Silicon: the unary-max/min (trips 4, length 4, 8
   contiguous sibling launches per trip) shape measured +2.06% when
   hoisted under the delivery-only model (which priced it +245); the
   Reduce-SDPA winner (trips 4, length 8, 8 sibling launches per trip,
   each separated by a surviving typed Dst increment, +121) and the
   SDPA-exp counted loop (8, 24, +2325) are delivery-bound and keep their
   unchanged benefits.  Full derivation in rvtt-cost.md.

   Hoist only when benefit >= the minimum-benefit threshold
   (XTT_REPLAY_HOIST_MIN_BENEFIT, overridable in the same centislot units
   through -mtt-tensix-replay-hoist-min-benefit=; the saturation term is
   part of the modeled benefit, not of the threshold, so an override
   cannot force a hoist whose record delivery is hidden).  The calibration
   derivation lives with the constants in rvtt-cost.md: the Blackhole
   same-source silicon A/Bs place every measured losing shape at negative
   modeled benefit (max -158: 4-trip captures of 17..31 slots, +1.8%..+2.3%
   regressions; -615: the execution-saturated 4-trip 4-slot 8-sibling
   shape, +2.06%) and every measured winning shape at positive benefit
   (minimum +121: the 4-trip 8-slot preheader-capture pair worth 21.5
   cycles/body; +2325: the 8-trip 24-slot counted payload, -9.83%), so the
   default threshold 60 refuses the entire measured losing class while
   accepting the measured winners with ~2x headroom under the nearest one.

   TRIPS must be provable (see provable_constant_trips above).  An unknown
   or merely estimated trip count refuses the hoist, which keeps the
   emitted code byte-identical to the unhoisted form.  The decision inputs
   are exactly the provable trip count, the capture length, the longest
   contiguous sibling-launch run, and the cost-table constants.  */

static bool
hoist_profitable_p (class loop *loop, basic_block preheader, unsigned length,
		    unsigned launch_run)
{
  uint64_t niter;
  if (!provable_constant_trips (loop, preheader, &niter))
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: loop %d trip count is not provably"
		 " constant\n", loop->num);
      return false;
    }

  HOST_WIDE_INT trips = (HOST_WIDE_INT) niter;
  if (trips < 2)
    {
      if (dump_file)
	fprintf (dump_file, "Not hoisting: loop %d runs %ld time(s)\n",
		 loop->num, (long) trips);
      return false;
    }

  HOST_WIDE_INT deliver = ((1 + (HOST_WIDE_INT) length)
			   * XTT_REPLAY_COST_RISC_PUSH_X100);
  HOST_WIDE_INT execute = ((HOST_WIDE_INT) length
			   * XTT_REPLAY_COST_REPLAY_SLOT_X100);
  HOST_WIDE_INT after = MAX ((HOST_WIDE_INT) XTT_REPLAY_COST_RISC_PUSH_X100,
			     execute);
  // Execution-saturation context: when the body's contiguous run of
  // sibling launches of this same buffer has enough execution surplus to
  // hide the record pass's delivery, hoisting relieves nothing per trip.
  HOST_WIDE_INT surplus = ((HOST_WIDE_INT) launch_run
			   * (execute - XTT_REPLAY_COST_RISC_PUSH_X100));
  bool hidden = surplus >= deliver;
  HOST_WIDE_INT before = hidden ? after : deliver;
  HOST_WIDE_INT benefit = trips * (before - after) - deliver;
  if (hidden && dump_file)
    fprintf (dump_file,
	     "Record delivery hidden: contiguous launch run %u x length %u"
	     " exec surplus %ld >= record delivery %ld\n",
	     launch_run, length, (long) surplus, (long) deliver);
  HOST_WIDE_INT min_benefit = (riscv_tt_replay_hoist_min_benefit >= 0
			       ? (HOST_WIDE_INT)
				 riscv_tt_replay_hoist_min_benefit
			       : XTT_REPLAY_HOIST_MIN_BENEFIT);

  if (benefit < min_benefit)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: modeled benefit %ld < %ld"
		 " (trips %ld, length %u)\n",
		 (long) benefit, (long) min_benefit, (long) trips, length);
      return false;
    }

  if (dump_file)
    fprintf (dump_file,
	     "Hoist profitable: modeled benefit %ld >= %ld"
	     " (trips %ld, length %u)\n",
	     (long) benefit, (long) min_benefit, (long) trips, length);
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

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    {
      if (dump_file)
	fprintf (dump_file, "Not hoisting: loop has no dedicated preheader\n");
      return nullptr;
    }

  if (!hoist_profitable_p (loop, preheader, seq.length,
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
      if (!preheader)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Not hoisting: loop has no dedicated preheader\n");
	  continue;
	}
      // The counted-loop payload is its own single clone; across trips the
      // launch is always separated from the next by the loop-control
      // delivery, so the contiguous launch run is 1.
      if (!hoist_profitable_p (loop, preheader, seq.length, 1))
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
convert_isomorphic_runs (function *cfn)
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

  // Launch conversion of isomorphic runs is part of the replay-hoist
  // mechanism family: the runs it targets are produced by the replay-aware
  // complete unroll, and the flag keeps the default configuration
  // byte-identical.
  if (riscv_tt_opt_replay_hoist > 0)
    convert_isomorphic_runs (cfn);
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
