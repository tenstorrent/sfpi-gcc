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

/* ALGORITHM ESSAY

   Hardware and problem.  Each Tensix thread owns a REPLAY buffer of
   instruction slots (riscv_tt_replay_size of them).  One TTREPLAY word
   with load=1 (a "capture" or "record") stores the next LEN delivered
   words into slots [IDX, IDX+LEN), either executing them as they
   record (exec=1) or swallowing them un-executed (exec=0); one
   TTREPLAY word with load=0 (a "launch" or "playback") re-emits the
   stored words through the normal downstream pipeline at its own
   stream position.  Replacing an N-word repeat with a 1-word launch
   thus saves N-1 delivered words per site, bought at one recording
   pass and N buffer slots.  Profit is word/issue-slot economics
   throughout: every priced decision goes through the shared
   delivery-cost engine (rvtt-delivery-cost.h; constants and their
   derivations in rvtt-cost.md) and, where reissue stalls matter, the
   shared interlock timing engine (rvtt-timing.h).

   Pass placement (tt/rvtt-passes.def).  This file registers two
   passes over the same transform ().  pass_rvtt_replay runs after
   reload -- on the final explicit or macro issue stream, with hard
   registers and recognizable patterns -- and BEFORE both Dst
   auto-increment ownership (which absorbs typed TTINCRWC separators
   around the launches this pass emits) and MOP loop-delivery
   formation (which re-rolls runs of those launches).
   pass_rvtt_replay_reform is the alternative placement selected by
   -mtt-tensix-optimize-post-autoincr-window: the first invocation
   gates itself off and the SAME transform () runs once, after the Dst
   auto-increment fold, with the file-static reform_mode flag raised.
   The fold absorbs the per-row separator words that break repeated
   row bodies, so those bodies become word-uniform only after it, and
   a single post-fold allocation prices every candidate against the
   one buffer.  reform_mode adds launch-arithmetic audits for
   "carried" Dst accesses (fold-retargeted positional-walk accesses;
   see the reform_mode block comment below).  Both passes keep the CFG
   intact except for the complete unroll of launch loops, which
   removes a backedge and requests loop fixup.

   Data structures.  scan_insns digests one basic block into a
   replay_block: a vector of replay_info entries carrying the insn, a
   structural hash, a register-age GENERATION (see invariants below),
   a MUST_END barrier flag, and an EMPTY flag for zero-length words.
   replay_span is a half-open [begin, end) interval of block indices.
   replay_sequence is a candidate window: length, hash, and the
   ascending clone spans that deliver the same words; replay_list,
   replay_map and replay_active are the sequence store, the hash index
   used during growth, and the picker's surviving-candidate set.
   Around these: replay_sa::state/automaton/view (the shadow suffix
   automaton), peel_plan and hoist_lift_plan (the hoist variants),
   conv_capture/conv_launch under a conv_map register value map (the
   launch conversion), and crf_block/crf_position/crf_value with
   crf_seq/crf_clone families committed as a verified crf_plan (the
   counted-row canonicalization).

   Algorithm, in transform () order:

   1. Slot census.  Typed user captures subtract their declared slot
      ranges from the free spans; a variable-length user capture
      claims the rest of the buffer; a raw asm word carrying the
      REPLAY opcode refuses all formation in the function (its slot
      range is unknowable).  Under the hoist flag, recording-epoch
      scoping excludes only the blocks where a user recording may
      still be open (up to the next explicit replay owner on each
      path) instead of refusing the whole function.

   2. Counted-loop hoisting (hoist_counted_loops).  A single-block
      counted loop whose body is one uninterrupted fixed-encoding SFPU
      run becomes a single-clone payload: a no-exec capture of the
      body in the dedicated preheader and one launch per trip, when
      the trip count is provable (rvtt-trips.h) and the priced benefit
      clears the audited minimum.  A benefit-refused candidate may
      still admit as an exec-while-record first-trip peel.

   3. Counted-row canonicalization (canonicalize_counted_rows, the
      crf_* machinery; docs/COUNTED_ROW_FORMATION.md).  Row families
      that repeat modulo per-row immediate materializations and
      register rotation are REWRITTEN into word-exact form -- excluded
      members moved to clone heads or tails, registers renamed under a
      verified occupancy simulation, live-in divergence bridged with
      all-lanes moves -- so the ordinary discovery below records one
      parameterized row program per family.

   4. Discovery (build_sequences), one basic block at a time.
      Candidates grow one instruction per round from length-1 seeds;
      equal (hash, generation, word) prefixes merge, so the clone
      lists of every repeated run emerge together.  This is O(N^2) by
      construction.  active_triage drops overlapping clones and
      single-instance sequences.  A suffix-automaton shadow
      (replay_sa) enumerates every maximal repeat of the same symbol
      string as a census; under checking builds it asserts that each
      legacy candidate is enumerable with the identical clone set, and
      it never feeds the picker.

   5. Allocation and replacement.  The knapsack aspect is greedy:
      pick_replay selects the candidate with the greatest modeled word
      saving that fits the largest free span, the tightest-fitting
      span hosts it, and active_invalidate retires candidates
      overlapping the consumed positions before the next pick.
      In-block commits (replace_sequence) record the first clone
      exec-while-record and launch the rest; when the full hoist
      admission holds (hoist_preheader) the record moves to the
      preheader as a no-exec capture (replace_hoisted_sequence),
      possibly peeled, lifted to an outer preheader, or re-sized to a
      wider same-anchor window plus one partial prefix launch (the
      window_sizing_* helpers).

   6. Delivery cleanups on the formed stream.  unroll_launch_loops
      completely unrolls a proven-trip loop whose body is nothing but
      playback launches, typed Dst steps and its own control, removing
      two loop-control words per trip.  convert_isomorphic_runs turns
      a run that matches a recorded payload under a register value map
      (conv_match_insn) into one more launch, provided every register
      whose final contents differ is dead after the run and the
      trailing Dst-advance context matches the payload's other sites.

   7. Fail-closed sweep (unhoist_hazard_rerecords).  A pass-hoisted
      no-exec capture left in a hazardous placement -- inside a loop
      with a Dst-store payload, within the audited drained-frontend
      window of a mod-write, or not dominating every launch of its
      span -- is un-hoisted by name: its launches become inline
      payload copies again (the identity the capture was formed from).

   Invariants and refusal discipline.  Clones must be word-exact under
   the one comparator (rvtt_dcost_replay_word_equal_p) and of the same
   GENERATION: the hash folds a per-GPR write counter, so two
   textually equal synthesized-word insns whose scalar inputs may
   differ never merge.  Sequences never cross a MUST_END word (asm,
   non-Tensix, replay owner, variable capture) and never span a replay
   owner's recorded shadow.  User-reserved slots are never allocated;
   slots consumed by hoisted or canonicalized records are marked
   persistent against later formation.  On targets that cannot execute
   while recording, the capture swallows its payload and the first
   clone launches too.  Every transformation either fires with its
   stated proof or refuses BY NAME through the refusal registry
   (rvtt-refuse.h), leaving the emitted bytes identical to the
   untransformed stream -- an unpriceable or unproven candidate is a
   named refusal, never a guess.  */

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

/* Look for repeated sequences of Tensix insns, and use REPLAy/ instruction for
   them.  Finding the sequences is O(N^2), and allocating them to the replay
   buffer is the knapsack problem.  We aim for 'good enough' */

/* 1) Only consider single BBs.  This works well for unrolled loops anyway.
      Looking accross BBs would require considering the dominator graph, and
      better live value computation for synthesized insns
   2) If sequence A's occurrences are all before sequence B's, B could reuse
      the replay buffer locations.  We do not consider this.
   3) If the user has explicitly used replay, we use the parts of the replay
      buffer that have not used (anywhere in the function).
   4) We use all of a discovered sequence (or none of it).  We could of course
      use the first N insns, if that is profitable and no room for the whole
      sequence.  */

/* FIXME: PR 36496 We terminate sequences if they meet a non TENSIX insn.
   This isn't always necessary.  The non-Tensix insn could be hoisted
   upwards, provided it doesn't affect the generation of any insn hoisted
   past. This may improve synthesized insns where opcode or address
   computation is in the middle of a sequence.  */


/* Post-auto-increment window RE-FORMATION mode.  Under
   -mtt-tensix-optimize-post-autoincr-window the formation DEFERS
   wholesale past pass_rvtt_dst_autoincr: the pre-fold pass_rvtt_replay
   invocation gates itself off and pass_rvtt_replay_reform (bottom of
   this file) runs transform () as the function's ONLY formation, over
   the folded stream.  Why: the fold absorbs the per-row typed TTINCRWC
   separators -- window-excluded barrier words (xtt_replay barrier) --
   into an owned address-modifier program, so a carried row body is
   word-uniform only AFTER the fold; and a pre-fold run would consume
   replay-buffer slots on the small pre-fold-visible windows, starving
   the fold's (much larger) windows -- the single post-fold allocation
   prices every candidate against the one buffer.  Deferral loses no
   opportunity: the fold only removes barrier words and retargets
   modifier operands of the rows those barriers separated, and no
   pre-fold-capturable run contains such a row, so every
   pre-fold-capturable run is post-fold-capturable verbatim.

   Under this mode:

   - Formation is the reviewed machinery unchanged, counted-row
     canonicalization included (it runs exactly once, here, with all
     its own audits -- lockstep, occupancy, delay-shadow contract,
     final lockstep -- over the folded stream; its exclusion vocabulary
     refuses every Dst/RWC-effecting word, so carried accesses are
     never moved, and its register-map rewrites never touch a modifier
     operand).  The word-exact replacement paths' soundness theorem is
     STREAM IDENTITY: an in-block capture inserts one exec-while-record
     word before the first clone, and every other clone is replaced by
     one launch that re-emits exactly the clone's words at the clone's
     position (the Replay Expander pushes stored words through the same
     downstream pipeline, WormholeB0 REPLAY.md functional model;
     pinned-sim replay_expander); a hoisted no-exec capture's preheader
     payload is INGESTED, never executed (Load=1/Exec=0 swallowed
     words, the delivery-vision expander model), and every clone becomes one
     launch in place.  The delivered instruction stream is therefore an
     insertion-only extension of the (canonicalized) folded stream:
     per-execution-cumulative RWC and ADDR_MOD walk arithmetic (the
     replay-soundness model) and every positionally discharged
     delay-shadow contract (gaps only grow under insertion)
     are preserved verbatim.

   - For payloads containing a CARRIED access (a Dst access the
     auto-increment pass retargeted to the compiler-owned scratch
     modifier, rvtt_dst_autoincr_carried_access_p): the fold's
     payload-coverage discipline extends to the window's launch
     arithmetic -- the window's delivered payload executions must equal
     the replaced row sites, or the walk skews silently.  The
     replacement paths preserve that equality by construction (one
     delivery per replaced clone); reform_carried_launch_arithmetic_ok
     re-verifies it structurally (clone non-overlap and word-exactness
     are exactly the premises), refusing by name
     post-autoincr-window-launch-arithmetic-skew.  The two
     stream-RESTRUCTURING sub-mechanisms whose delivered words are not
     the replaced site's words refuse carried members by name: the
     isomorphic-run launch conversion (register-renamed delivery of a
     positional-state access is unaudited) and the exec-while-record
     first-trip peel (it RELOCATES one trip's carried executions into
     the preheader, across the owned configuration program's placement
     point -- unproven walk-order in this increment).

   - Every fail-closed belt of the formation runs here over the
     post-fold layout: the raw-REPLAY census, the recording-epoch
     scoping, the un-hoist sweep rules 1-3, and the slot-span
     subtraction (records take only slots no prior owner -- user or LLK
     envelope -- declared).  The downstream dest-index window checker and
     MOP formation see the formed stream as their pass ordering already
     requires.  */
bool reform_mode = false;

/* The replay pass looks for sequences of instructions that repeat and replaces
   the repeated portions w/ a REPLAY instruction */

/* Audited mod-write classification for the no-exec record placement
   obligation (rvtt-cost.md AUDITED COMPOSITION FACT
   "no-exec record composition").  The hardware-refuted composition is a
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
   hardware-witnessed good across many toolchain snapshots -- treating undecoded words
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

/* Fail-closed no-exec re-record sweep (rvtt-cost.md "no-exec
   record composition", delivery-boundary paragraph).  A pass-hoisted
   NO-EXEC capture whose placement block lies inside a loop re-ingests
   its payload every iteration; when that payload carries a Dst store,
   the re-ingestion follows the previous iteration's launch-delivered
   stores at runtime pacing no static model prices.  The hardware
   witnesses: the lcm-fresh kernel under record-hoist (hang) and
   sparse_k_filter at runtime trip 32 (hang) -- and
   a device measurement showing the same shape hangs WITH EXPLICIT
   TTINCRWC rows too, so the composition is the re-record x launches
   x Dst-store-payload, not the mod-write alone.  The witnessed-good
   exec-while-record conversion (proven on device across minmax, sdpa,
   where, typecast, and lcm kernels) is the intended deliverer of these shapes;
   when it has NOT fired by the end of the pass, this sweep un-hoists
   the capture by name: every launch of the span is replaced by an
   inline copy of the payload (a launch executes exactly the payload,
   so this is the identity the capture was formed from), and the record
   and its never-executed shadow are deleted.  Storeless payloads
   (celu/eqz-class wrapper records, hardware-witnessed good) and
   loop-free placements (xielu preamble, single-loop preheaders) are
   untouched.  Only captures this pass formed are swept: user-authored
   records are the user's own contract.

   Second placement obligation (same sweep, same
   identity-restoring action): a still-no-exec formed capture whose
   recording window can open within the audited W_drain issue-word
   window of an audited mod-write (placement_modwrite_hazard_p; the
   rvtt-cost.md AUDITED COMPOSITION FACT's distance boundary, priced
   with the same exported constant the dst-autoincr group guard uses,
   rvtt_modwrite_drained_frontend_window) is un-hoisted by name -- the
   compiler must never FORM the hardware-refuted wedge adjacency it
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
      /* Rule 1 applies to in-loop placements only (a
	 loop-free record's payload executes once, the witnessed
	 class); rule 2 audits every placement.  */
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

      /* Rule 1: in-loop re-record with a Dst-store payload.
	 Rule 2: recording window opens inside the
	 audited W_drain window of an audited mod-write.  Rule 3:
	 a still-no-exec Dst-store capture whose record does NOT
	 dominate every launch of its span (or has no in-function launch
	 at all) is live-at-exit relative to that launch -- the recorded
	 store can be delivered from a launch the record never executed
	 before, on a sibling CFG path or, since the per-thread Replay
	 Expander buffer PERSISTS across the soft-reset kernel-invocation
	 boundary (hardware-established persistence model: cross-invocation
	 and within-launch cross-function delivery both demonstrated),
	 from a caller-loop re-entry or an entirely later kernel.  The
	 intra-function reach walks (dst-autoincr, W_drain) cannot
	 see that consumer; forming the adjacency is the same hardware-
	 refuted wedge class, so fail closed.  The
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

/* The formation driver for one function (both pass invocations end up
   here; reform_mode distinguishes them).  BUFFER_SIZE is the replay
   buffer's slot count.  In order: the raw-capture census (a raw REPLAY
   word refuses the whole function), user-slot subtraction and
   recording-epoch scoping, counted-loop hoisting, counted-row
   canonicalization, the per-block discover/pick/commit loop, the
   launch-loop unroll and isomorphic-run conversion, and the fail-closed
   un-hoist sweep.  See the algorithm essay at the top of the file.  */

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
	      /* Inside a typed-closed user capture payload.  */
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
		    /* A slot-occupying word in a possibly-recording
		       region.  */
		    bitmap_set_bit (dirty_bbs, bb->index);
		}
	      else if (shadow && get_attr_length (insn))
		shadow--;
	      continue;
	    }
	  if (scoped)
	    /* Owner epoch boundary: possibly-open recording state is
	       proven closed at an explicit owner operation.  */
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

  /* Shadow-coupling possibility gates the companion-pairing refusals in
     span_companion_sound_p; computed once per function.  */
  bool sticky = rvtt_shadow_coupling_possible (cfn);

  std::vector<bool> persistent_slots (buffer_size, false);
  if (riscv_tt_opt_replay_hoist > 0)
    hoist_counted_loops (cfn, replay_spans, persistent_slots, dirty_bbs,
			 sticky);

  /* Counted-row parameterized formation: canonicalize eligible clone
     families so the word-exact discovery below records one parameterized
     row program per family (docs/COUNTED_ROW_FORMATION.md).  */
  /* In reform mode this is the FUNCTION'S ONLY formation (the pre-fold
     invocation defers, see pass_rvtt_replay::gate), so the counted-row
     canonicalization runs here exactly once, with all its own audits
     (lockstep, occupancy, delay-shadow contract, final lockstep) over
     the folded stream.  Its member-exclusion vocabulary admits only
     single-slot materializations with no Dst/RWC/CC/configuration
     effect, so carried accesses are never moved by it; its clone
     register-map rewrites never touch a modifier operand.  */
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
	/* Recording state may be open here (unprovable user epoch).  */
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

      /* Item #9 stage A: the suffix automaton runs here as a SHADOW --
	 it enumerates over the same scanned block and reports what the
	 legacy discovery above did not offer the picker, but ACTIVE (the
	 picker's input) is not touched, so the formed windows below are
	 the legacy ones byte-for-byte.  */
      if (riscv_tt_replay_shadow_discovery > 0 || flag_checking)
	shadow_discovery_census (info, active, unsigned (list.size ()),
				 replay_spans.front ().end,
				 spans.front ().end, sticky, bb->index);

      while (!active.empty ())
	{
	  auto *seq = pick_replay (active, spans.front ().end, info, sticky);
	  if (!seq)
	    break;

	  /* Reform-mode carried-payload launch-arithmetic audit (see the
	     reform_mode block comment): a refused candidate is dropped
	     from consideration (companion_ok doubles as the picked-set
	     veto) and formation continues with the next candidate.  */
	  if (reform_mode
	      && payload_contains_carried_p (info, seq->clones.front ())
	      && !reform_carried_launch_arithmetic_ok (info, *seq))
	    {
	      seq->companion_ok = 0;
	      continue;
	    }

	  /* The record-hoist flag admits the preheader hoist attempt on
	     its own (its pricing branch owns the re-record class); with
	     only the plain hoist flag the attempt behaves exactly as
	     before.  */
	  peel_plan peel;
	  hoist_lift_plan lift;
	  basic_block preheader
	    = (riscv_tt_opt_replay_hoist > 0
	       || riscv_tt_opt_replay_record_hoist > 0)
	    ? hoist_preheader (*seq, info, dirty_bbs, &peel, &lift) : nullptr;

	  /* Lane IM: hoisted-record window re-sizing (see the block
	     comment above window_sizing_clones_exact_p).  Only an
	     admitted non-peel hoist changes the delivery economics; the
	     widened candidate must re-prove the WHOLE hoist admission
	     itself, and any refusal keeps the original pick verbatim.  */
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

  /* The launch-loop unroll and the launch conversion of isomorphic runs
     are part of the replay-hoist mechanism family: the shapes they target
     are produced by the replay-aware complete unroll and the hoist above,
     and the flag keeps the default configuration byte-identical.  */
  if (riscv_tt_opt_replay_hoist > 0)
    {
      unroll_launch_loops (cfn, dirty_bbs);
      convert_isomorphic_runs (cfn, dirty_bbs);
    }

  /* Fail-closed: any pass-hoisted no-exec capture the exec-while-record
     conversion did not reach, placed on a loop with a Dst-store payload,
     is the hardware-refuted re-record composition -- un-hoist it.  */
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
    /* Lane IH: under -mtt-tensix-optimize-post-autoincr-window the
       formation DEFERS wholesale to the post-auto-increment invocation
       below (pass_rvtt_replay_reform).  Deferral loses no opportunity:
       the fold only REMOVES window-excluded barrier words (explicit
       TTINCRWC separators) and retargets modifier operands of the rows
       those barriers separated -- runs the pre-fold formation could
       capture contain no such rows, so every pre-fold-capturable run is
       post-fold-capturable verbatim, and the single post-fold
       allocation prices ALL candidates (the folded carried bodies
       included) against the one replay buffer instead of letting the
       pre-fold run starve the fold's windows of slots.  */
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

/* Post-auto-increment window RE-FORMATION: the same formation,
   run a second time after pass_rvtt_dst_autoincr, under reform_mode (see
   the block comment at reform_mode near the top of this file for the
   design and its soundness obligations).  Default off
   (-mtt-tensix-optimize-post-autoincr-window); knob-off keeps the
   pipeline byte-identical (the pass does not run, and every reform_mode
   branch above is unreachable in the first formation).  */

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

/* Instantiate the post-reload replay-formation pass; inserted by
   tt/rvtt-passes.def ahead of Dst auto-increment ownership and MOP
   formation.  */

rtl_opt_pass *
make_pass_rvtt_replay (gcc::context *ctxt)
{
  return new pass_rvtt_replay (ctxt);
}

/* Instantiate the post-auto-increment re-formation pass; inserted by
   tt/rvtt-passes.def after pass_rvtt_dst_autoincr.  Its gate is the
   exact complement of pass_rvtt_replay's, so exactly one of the two
   invocations forms replay windows for a function.  */

rtl_opt_pass *
make_pass_rvtt_replay_reform (gcc::context *ctxt)
{
  return new pass_rvtt_replay_reform (ctxt);
}
