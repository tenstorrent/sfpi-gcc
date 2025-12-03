/* Pass to generate the WH replay insn
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "cfghooks.h"
#include "df.h"
#include "memmodel.h"
#include "tm_p.h"
#include "insn-config.h"
#include "regs.h"
#include "emit-rtl.h"
#include "recog.h"
#include "cgraph.h"
#include "tree-pretty-print.h" /* for dump_function_header */
#include "varasm.h"
#include "insn-attr.h"
#include "conditions.h"
#include "flags.h"
#include "output.h"
#include "except.h"
#include "rtl-error.h"
#include "toplev.h" /* exact_log2, floor_log2 */
#include "reload.h"
#include "intl.h"
#include "cfgrtl.h"
#include "debug.h"
#include "tree-pass.h"
#include "tree-ssa.h"
#include "cfgloop.h"
#include "stringpool.h"
#include "attribs.h"
#include "asan.h"
#include "rtl-iter.h"
#include "print-rtl.h"
#include "function-abi.h"
#include <vector>
#include <unordered_map>
#include "rvtt.h"

//#define ENABLE_DUMP

#ifdef ENABLE_DUMP
#define DUMP(...) ((void)fprintf(stderr, __VA_ARGS__))
#else
#define DUMP(...) ((void)0)
#endif

using namespace std;

typedef unsigned long long int hash_type;

struct insn_info {
  const short code;                 // insn code
  const unsigned short startable;   // set by strategy, can this insn can start a sequence?
  const hash_type hash;             // cache the hash
  bool halt;                        // this insn is followed by a non-sfpu insn

  insn_info() : code(0), startable(0), hash(0), halt(false) {}
  insn_info(short c, unsigned short s, hash_type h) : code(c), startable(s), hash(h), halt(false) {}
};

struct seq_entry {
  const unsigned short start;    // start insn # of this seq (also unique id)
  const unsigned short length;   // # of insns in this seq
  unsigned short votes;          // votes for this seq
  unsigned short insn_available; // prevent a seq from starting again while it is growing
  const hash_type hash;          // hash value for this seq

  seq_entry() :
      start(0), length(1), votes(0), insn_available(0), hash(0) {}
  seq_entry(unsigned short s, unsigned short l, hash_type h) :
      start(s), length(l), votes(0), insn_available(0), hash(h) {}
};

static vector<insn_info> insn_list;
static vector<vector<seq_entry>> sequences;               // All sequences this insn belongs to
static unordered_map<hash_type, seq_entry*> sequence_map; // Unique sequences
static vector<vector<seq_entry *>> insn_sequences;        // Unique sequences for each insn
static unordered_map<hash_type, const seq_entry*>
  final_sequence_map;                                     // Expanded single final sequence

constexpr unsigned int strategy_load        = 0x1;
constexpr unsigned int strategy_loadi       = 0x2;
constexpr unsigned int strategy_first_insn  = 0x4;
constexpr unsigned int strategy_every_insn  = 0x8;

static int replay_max_insns = 32;

constexpr hash_type dst_hash_salt = 0x100000000;
constexpr hash_type reg_hash_salt = 0x200000000;
constexpr hash_type int_hash_salt = 0x400000000;
constexpr hash_type vec_hash_salt = 0x800000000;
constexpr hash_type wrid_hash_salt = 0x1000000000;

static inline hash_type hashit(hash_type x)
{
  x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
  x = x ^ (x >> 31);
  return x;
}

hash_type find_writer(int regno, const rtx_insn *insn)
{
  basic_block bb = BLOCK_FOR_INSN (insn);

  for (const rtx_insn *probe = insn, *limit = BB_HEAD (bb); probe != limit;)
    {
      probe = PREV_INSN (probe);

      if (!NONJUMP_INSN_P (probe))
	continue;

      if (regno != rvtt_get_insn_dst_regno (probe))
	continue;

      rtx pat = PATTERN (probe);
      if (GET_CODE (pat) != SET || !REG_P (SET_SRC (pat)))
	return reinterpret_cast<hash_type> (probe);

      // Chase back through a mov.  Doing this because the first test
      // I wrote hit this case
      regno = REGNO (SET_SRC (pat));
    }

  return -1;
}

// Compute a unqiue hash for this insn signature by hashing the:
//  - insn code
//  - return register
//  - all the operands
//
// For nonimm insns, we need to find the insn that wrote the value to written
// to the insn queue.  That insn ptr is added to the hash like an operand, ie,
// if the same insn is used then we know it is the same value.  If the insn
// that the value is outside of the BB, then, since this is processed within a
// BB, we know it was the same value and can ignore it
static hash_type compute_insn_hash(const rvtt_insn_data *insnd,
				   const rtx_insn *insn)
{
  int code = INSN_CODE(insn);
  hash_type insn_hash = hashit(code);
  hash_type dst = rvtt_get_insn_dst_regno(insn) - SFPU_REG_FIRST;
  insn_hash = hashit(insn_hash ^ (dst | dst_hash_salt));

  int count = rvtt_get_insn_operand_count(insn);
  for (int i = 0; i < count; i++)
    {
      hash_type val;

      if (i == insnd->nonimm_val_arg_pos())
	{
	  // Nonimm insns, find the writer of the operation
	  rtx op = rvtt_get_insn_operand(insnd->nonimm_pos - 1, insn);
	  gcc_assert(GET_CODE(op) == REG);
	  hash_type writer_id = find_writer(REGNO(op), insn);
	  insn_hash = hashit(insn_hash ^ (writer_id | wrid_hash_salt));
	  continue;
	}

      rtx op = rvtt_get_insn_operand(i, insn);
      switch GET_CODE(op)
	{
	default:
	  gcc_unreachable();
	  break;

	  case UNSPEC:
	    gcc_assert (XINT (op, 1) == UNSPEC_SFPCSTLREG);
	    val = INTVAL (XVECEXP (op, 0, 0));
	  case MEM:
	    // FIXME: Find rtl hasher.
	    break;
	    
	  case REG:
	  val = REGNO(op) | reg_hash_salt;
	  break;

	case CONST_INT:
	  val = INTVAL(op) | int_hash_salt;
	  break;

	  case CONST_VECTOR: // FIXME: Remove
	  // The only expected vector constant has a value of all 0s
	  val = vec_hash_salt;
	  break;
	}

      insn_hash = hashit(insn_hash ^ (val << i));
    }

  return insn_hash;
}

static inline hash_type compute_seq_hash(hash_type insn_hash, hash_type seq_hash)
{
  return hashit(insn_hash ^ (seq_hash << 1));
}

// devise_strategy
//
// This code is over-designed.  I was concerned w/ the O(n^2) nature of the
// algorithm and so wanted to reduce the number of insns that could start a
// sequence by, e.g, only starting when a load/loadi was found.  Turns out
// this is actually pretty fast and the 32 insn limit caps the O(n^2)ness.
//
// I'm leaving the code in place for now, despite the fact that is is
// disabled.
//
// Reduce complexity by reducing how many insns get processed:
//   - load/loadi demark possible sequence starts
//   - we expect loops to be unrolled by 8 bracketed by load
//   - if there are lots of insns, only use load to start blocks
//   - only search for and generate sequences for 1/4 of the insns (then check
//     all insns against those sequences).  in theory, this could be 1/8, but
//     there may be setup code as well.  Clearly we only need 1/2 since no
//     sequence can duplicate itself that takes more than half of the total
//     insns (assuming, of course, that this BB is structured)
//   - if we find kernels w/ interesting sequences in the 2nd half of the
//     kernel's insn and not in the first half, well...crap, revisit (maybe
//     check each 1/nth independently)
//
// Creates the insn_list
static void devise_strategy (int *count, int *strategy, basic_block bb)
{
  rtx_insn *insn;

  int first_load = -1;
  int loads = 0;
  int loadis = 0;
  int insn_count = 0;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P(insn))
	// NONDEBUG_INSN_P != !DEBUG_INSN_P, because, of course
	continue;

      const rvtt_insn_data *insnd;
      if (!rvtt_p (&insnd, insn) || insnd->riscv_p ())
	{
	  if (insn_list.size () > 0)
	    {
	      // The nonimm processing should ensure that riscv insns don't
	      // interact with sfpu insns.  However, bail on __asm insns
	      if (INSN_CODE (insn) == -1)
		// These are __asm and maybe other?
		insn_list.back ().halt = true;
	    }
	  continue;
	}
      if (insnd->empty_p ())
	{
	  // Ugly side effect
	  // Empty insns have served their purpose, delete now
	  // This simplifies the update routine somewhat
	  set_insn_deleted (insn);
	  continue;
	}

      int startable = strategy_every_insn;
      if (insn_count == 0)
	startable |= strategy_first_insn;
      if (insnd->id == rvtt_insn_data::sfpload_int)
	{
	  if (first_load == -1)
	    first_load = insn_count;
	  loads++;
	  startable |= strategy_load;
	}
      else if (insnd->id == rvtt_insn_data::sfploadi_int)
	{
	  loadis++;
	  startable |= strategy_loadi;
	}

      hash_type insn_hash = compute_insn_hash(insnd, insn);
      insn_info insni(INSN_CODE(insn), startable, insn_hash);
      insn_list.push_back(insni);
      insn_count++;
    }
  gcc_assert (insn_count < 65536);

  *strategy = strategy_every_insn;
  *count = insn_count;

#if 0  // Determine which insns can start a sequence
  // Typically, there will be 8 loads in a kernel, but these may not be in one
  // BB, if the loop is unrolled (or we have multiple BBs), in which case we
  // should look for more ways to start a sequence
  if (loads >= 8)
    {
      // Assume unrolled loop, only need to process a fraction of it
      *strategy = strategy_load;
      *count = (insn_count - first_load) / 4 + first_load;
    }
  else if (loadis <= 16)
    {
      *strategy = strategy_loadi;
      *count = insn_count;
      if (loadis < 8)
	*strategy |= strategy_first_insn;
    }
  else
    {
      *strategy = strategy_first_insn;
      *count = insn_count;
    }
#endif

  DUMP (" strategy: %d insn_total: %d insns_to_scan: %d (loads: %d, loadis: %d)\n",
	*strategy, insn_count, *count, loads, loadis);
}

static inline void
dump_sequences ()
{
#ifdef ENABLE_DUMP
  int k = 0;
  for (auto iteri : sequences)
    {
      DUMP ("  %d: ", k);
      for (auto iters : iteri)
	DUMP ("(s%2d: l%2d h%llx) ",
	      iters.start, iters.length, iters.hash);
      k++;
      DUMP ("\n");
  }
#endif
}

// This fn scans the count insns and generates sequences, up to count insns
// in length
//
// A sequence starting insn is an insn that matches the flags in strategy.
// Each sequence starting insn starts a sequence, even if it matches a prior
// sequence - since the sequence may later diverge (identical sequences are
// pruned in a later pass).
//
// Note that only count insns (as determined by the strategy) are processed
// (not the full insn list).
//
// Creates the sequences structure
static void
generate_sequences (int count, int strategy)
{
  DUMP ("  generating sequences, scanning %d insns\n", count);

  bool halt = false;
  for (int i = 0; i < count; i++)
    {
      hash_type insn_hash = insn_list[i].hash;
      DUMP ("   #%3d: processing insn (h%llx)\t%s\n", i, insn_hash, insn_data[insn_list[i].code].name);

      vector<seq_entry> dummy;
      sequences.push_back (dummy);

      // Start a new sequence or track an old sequence
      if ((insn_list[i].startable & strategy) != 0)
	{
	  DUMP ("      add new sequence %d\n", i);
	  hash_type seq_hash = compute_seq_hash (insn_hash, 0);
	  seq_entry new_seq (i, 1, seq_hash);
	  sequences.back ().push_back (new_seq);
	}

      // Try to extend existing sequences
      if (halt)
	DUMP ("      halting sequences at insn %d\n", i);
      else if (i != 0)
	{
	  for (auto const& cur_seq : sequences[i - 1])
	    {
	      if (cur_seq.length == replay_max_insns)
		DUMP ("      sequence s:%d l%d reached insn limit %d\n",
		     cur_seq.start, cur_seq.length, replay_max_insns);
	      else
		{
		  // Extend the sequence's hash
		  DUMP ("      extend sequence s:%d l%d\n", cur_seq.start, cur_seq.length);
		  hash_type seq_hash = compute_seq_hash (insn_hash, cur_seq.hash);
		  seq_entry new_seq (cur_seq.start, cur_seq.length + 1, seq_hash);
		  sequences.back ().push_back (new_seq);
		}
	    }
	}

      halt = insn_list[i].halt;
    }

  dump_sequences ();
}

static void
dump_unique_sequences ()
{
#ifdef ENABLE_DUMP
  DUMP ("    ");
  for (auto iter : sequence_map)
    DUMP ("(s%2d: l%2d) ", iter.second->start, iter.second->length);
  DUMP ("\n");
#endif
}

// This fn stores unique sequences into a map keyed on the sequence hash.
// Duplicate sequences are discarded.
//
// When this is done, note that a long sequence may build on a different short
// sequence.  e.g., one sequence could be load+add while another is
// load+mul+inc but both use the "load" as the base then diverage from there.
//
// Creates the sequence_map
static void
map_unique_sequences ()
{
  DUMP ("  mapping unique sequences\n");

  for (auto& insn_iter : sequences)
    for (auto& seq_iter : insn_iter)
      {
	auto entry = sequence_map.find (seq_iter.hash);

	if (entry == sequence_map.end ())
	  {
	    DUMP("    mapping unique s:%2d l%2d h%llx\n",
		 seq_iter.start, seq_iter.length, seq_iter.hash);
	    sequence_map.insert (pair<hash_type, seq_entry *> (seq_iter.hash, &seq_iter));
	  }
      }

  dump_unique_sequences ();
}

static inline void
dump_voted_sequences ()
{
#ifdef ENABLE_DUMP
  DUMP ("  all sequence votes:\n");
  for (auto iter : sequence_map)
    DUMP("    s%2d: l%2d v%d\n", iter.second->start, iter.second->length, iter.second->votes);
#endif
}

// This fn goes through all of the insns again and looks up the unique
// sequence(s) to which the insn sequence belongs and bumps their vote count.
// Since only a fraction of the insns were used to generate the possible
// sequences, some sequences may be missed, however, all insns vote.
//
// This works by:
//  - keep a vector 0..n_insns of sequence entries
//  - a sequence is added for the insn if it is found in the sequence map
//  - each insn traverses the prior insn's sequences and tries to extend them
//  - if extended, they are added to this insn's list of sequences
//  - each time there is a match, the sequence's vote is bumped
//
// Note that this routine does not handle all cases:
//  - a fn w/ non-overlapping sequences could use replay multiple times,
//    this is not handled
//  - this only votes for full sequences.  if a subset of a sequence can
//    be used somewhere, that should add to the benefit of the sequence,
//    however, that is not tracked.  This would be easier to handle if, e.g,
//    the sequence (s2: l2) mapped to the same tally as (s2: l4).  It is
//    unclear to me if this would have much real world benefit
//
// Creates insn_sequences (need a better name)
static void
vote_for_sequences ()
{
  DUMP ("  voting for sequences, %zu insns\n", insn_list.size ());

  for (unsigned int i = 0; i < insn_list.size(); i++)
    {
      hash_type insn_hash = insn_list[i].hash;
      hash_type seq0_hash = compute_seq_hash (insn_hash, 0);

      vector<seq_entry *> dummy;
      insn_sequences.push_back (dummy);
      insn_sequences.back ().reserve (sequences.back ().size ());

      auto entry = sequence_map.find (seq0_hash);
      if (entry != sequence_map.end ())
	{
	  DUMP ("    begin new s:%2d l%2d\n", entry->second->start, entry->second->length);
	  // This insn starts a new sequence
	  insn_sequences.back ().push_back (entry->second);
	}

      if (i == 0)
	continue;

      // Try to extend existing sequences
      for (auto const& cur_seq : insn_sequences[i - 1])
	{
	  hash_type seq_hash = compute_seq_hash (insn_hash, cur_seq->hash);
	  auto entry = sequence_map.find (seq_hash);
	  if (entry == sequence_map.end ())
	    {
	      DUMP ("    no match, ending sequence\n");
	      continue;
	    }

	  int length = cur_seq->length + 1;

	  // When a sequence is being tracked, it cannot be started
	  // again until its full length has been traversed (ie, a
	  // sequence cannot contain itself)
	  if (i > entry->second->insn_available)
	    {
	      DUMP ("    vote for  s%2d: l%2d\n", entry->second->start, entry->second->length);
	      entry->second->votes++;
	      entry->second->insn_available = i + length - 1;
	      insn_sequences.back ().push_back (entry->second);
	    }
	  else
	    DUMP ("   skip overlapped s%2d: l%2d\n",
		  entry->second->start, entry->second->length);
	}
    }

  dump_voted_sequences ();
}

// Pick the sequence that saves the most insns.
// For now, we only get 1 sequence per fn
// In the case of a tie, pick the one that starts earliest.  This helps
// stabilize assembly tests where the result can flip back and forth for
// unknown reasons as well as starting earlier may lead to extra bonus insns
// saved when a partial sequence can be used (since we don't vote for partial
// sequences yet)
static void
pick_sequence (int *start, int *length)
{
  int most_saved = 0;
  int earliest = 0xFFFF;

  for (auto const& iter : sequence_map)
    {
      int saved = iter.second->length * (iter.second->votes - 1) - iter.second->votes;
      DUMP ("    s%2d: l%3d v%2d saved %d\n", iter.second->start, iter.second->length,
	    iter.second->votes, saved);
      if (saved > most_saved
	  || (saved != 0 && saved == most_saved && iter.second->start < earliest))
	{
	  most_saved = saved;
	  earliest = iter.second->start;
	  *start = iter.second->start;
	  *length = iter.second->length;
	}
    }
  DUMP ("  picked s%d: l%d saved %d\n", *start, *length, most_saved);
}

// Per the comment above generate_sequence, the final "sequence" may be
// comprised of multiple sub-sequences (ie, different "start" insns).  To be
// sure we only update the insns of actual matches, populate the
// final_sequence_map w/ just the sequence we care about
//
// This regenerates a lot of info calculated before since uniquify squashed it
//
// Create final_sequence_map
static void
collapse_sequence (int start, int length)
{
  for (int i = 0; i < length; i++)
    for (auto const& iter : sequences[start + i])
      if (iter.start == start)
	{
	  gcc_assert (iter.length == i + 1);
	  DUMP ("    final sequence element s%2d: l%3d\n", iter.start, iter.length);
	  final_sequence_map.insert (pair<hash_type, const seq_entry *> (iter.hash, &iter));
	}
}

// Helper fn for update_insns, inserts/deletes insns for a replay
// IMPORTANTIMPORTANTIMPORTANTIMPORTANTIMPORTANTIMPORTANT
//   See tensix issue #3303 if this code gets updated to issue a TTREPLAY
//   in load only mode
// IMPORTANTIMPORTANTIMPORTANTIMPORTANTIMPORTANTIMPORTANT
static bool
do_update (bool first, int i ATTRIBUTE_UNUSED, int count, int length,
	   rtx_insn *start_insn, rtx_insn *insn)
{
  if (first)
    {
      // First sequence must be full length
      if (count == length)
	{
	  DUMP ("    inserting replay capture at %d\n", i - count);
	  first = false;
	  rtx replay = gen_rvtt_ttreplay_int (GEN_INT (0), GEN_INT (count),
					      GEN_INT (1), GEN_INT (1));
	  emit_insn_before (replay, start_insn);
	}
    }
  else if (count > 1)
    {
      DUMP("    deleting %d insns starting at %d\n", count, i - count);

      for (int j = 0; j < count;)
	{
	  // Replay sequences preclude non-sfpu insns
	  if (NONDEBUG_INSN_P (start_insn))
	    {
	      set_insn_deleted (start_insn);
	      j++;
	    }
	  start_insn = NEXT_INSN (start_insn);
	}
      rtx replay = gen_rvtt_ttreplay_int (GEN_INT (0), GEN_INT (count),
				          GEN_INT (0), GEN_INT (0));
      emit_insn_before (replay, insn);
    }

  return first;
}

// Remove the duplicated insns and emit the REPLAYs
static void
update_insns (basic_block bb, int which, int length)
{
  which = which + 0; // suppress warning, which only used in debug
  DUMP("  updating insn sequence with s:%d l:%d\n", which, length);
  rtx_insn *insn;

  insn = BB_HEAD(bb);
  int i = 0;
  int count = 0;
  bool first = true;
  bool halt = false;
  hash_type seq_hash = 0;
  rtx_insn *start_insn = nullptr;
  rtx_insn *last_rvtt_insn = nullptr;

  FOR_BB_INSNS (bb, insn)
    {
      const rvtt_insn_data *insnd;
      if (NONDEBUG_INSN_P (insn) && rvtt_p (&insnd, insn)
	  && !insnd->riscv_p ())
	{
	  hash_type insn_hash = insn_list[i].hash;
	  hash_type new_hash = compute_seq_hash (insn_hash, seq_hash);
	  auto entry = final_sequence_map.find (new_hash);
	  if (count != 0 && (entry == final_sequence_map.end () || halt))
	    {
	      first = do_update (first, i, count, length, start_insn, last_rvtt_insn);

	      count = 0;
	      seq_hash = 0;
	      new_hash = compute_seq_hash (insn_hash, seq_hash);
	      entry = final_sequence_map.find (new_hash);
	    }
	  if (entry != final_sequence_map.end() && !halt)
	    {
	      // Don't restart if we haven't finished the last sequence
	      if (count == 0)
		{
		  DUMP("    starting seq at %d\n", i);
		  start_insn = insn;
		}
	      count++;
	      seq_hash = new_hash;
	    }

	  halt = insn_list[i].halt;
	  i++;
	  last_rvtt_insn = insn;
	}
    }

  if (count != 0)
    do_update (first, i, count, length, start_insn, last_rvtt_insn);
}

// Finding repeats of generic patterns can be costly in either compute or memory
// (or both).  Fundamentally, the algorithm used is O(n^2).  The "strategy"
// routine limits this by selecting only certain instruction types as eligible
// to start a sequence, making this O(n*m) where m is the number of insns that
// can start a sequence.
//
// Each sequence contains a hash which (uniquely?) identifies it by hashing
// all of its parameters and all the instructions that come before it in the
// sequence.  A new instruction inherits all the sequences from the previous
// instruction and extends them if the new hash is unique.  Many sequences get
// duplicated (we are, after all, looking for duplicate sequences).  The
// duplicate sequences are removed by using a map.  Finally, the insns are
// traversed again and each insn votes for the sequences it belongs to, the
// sequences with the greatest savings (# insns and repeat count) wins.
static void
find_sequence (basic_block bb)
{
  int count, strategy;
  int start = -1;
  int length = -1;

  devise_strategy (&count, &strategy, bb);
  if (count != 0)
    {
      sequences.reserve (count);
      generate_sequences (count, strategy);

      // Break the chain of sequences by inserting a dumming insn
      map_unique_sequences ();
      vote_for_sequences ();
      pick_sequence (&start, &length);
      collapse_sequence (start, length);
      if (start != -1)
	update_insns (bb, start, length);

      sequence_map.clear ();
      final_sequence_map.clear ();
      sequences.resize(0);
      insn_sequences.resize(0);
      insn_list.resize(0);
    }
  else
    DUMP("  no sfpu insns, exiting\n");
}

// The replay pass looks for sequences of instructions that repeat and replaces
// the repeated portions w/ a REPLAY instruction
//
// Note that this implementation looks for sequences within a BB which is much
// easier (I think) than finding loops and replacing them with REPLAY.  TBD.
static void
transform (function *cfn)
{
  DUMP ("Replay pass on: %s\n", function_name (cfn));

  basic_block bb;

  FOR_EACH_BB_FN (bb, cfn)
    {
      DUMP (" begin BB\n");
      find_sequence (bb);
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
    insn_list.reserve(200);
  }

  virtual bool gate (function *cfn) override
  {
    if (!TARGET_XTT_TENSIX_OPT_REPLAY)
      return false;

    // If there are any replay insns, bail here.  Ideally we'd
    // continue if there are gaps in the replay allocations. But
    // that's for another day. Also, perhaps it's cheap or rare enough
    // to check this condition during building the sequence data
    // structure.
    basic_block bb;

    FOR_EACH_BB_FN (bb, cfn)
      {
	rtx_insn *insn;
	FOR_BB_INSNS (bb, insn)
	  {
	    int icode = INSN_CODE (insn);
	    if (icode == CODE_FOR_rvtt_sfpsynth_insn)
	      {
		auto ops = XVECEXP (PATTERN (insn), 0, 0);
		auto code = XVECEXP (ops, 0, SYNTH_icode);

		icode = INTVAL (code);
	      }
	    if (icode == CODE_FOR_rvtt_ttreplay_int)
	      return false;
	  }
      }
    return true;
  } 

  /* opt_pass methods: */
  virtual unsigned execute (function *cfn) override
  {
    replay_max_insns = riscv_tt_replay_size;
    transform (cfn);
    return 0;
  }
}; // class pass_rvtt_replay

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_replay (gcc::context *ctxt)
{
  return new pass_rvtt_replay (ctxt);
}
