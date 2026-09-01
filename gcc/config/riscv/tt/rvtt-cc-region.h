/* Tensix CC-region tree (pushc/popc frame structure, stage A).
   Copyright (C) 2022-2026 Tenstorrent Inc.

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

#ifndef GCC_RVTT_CC_REGION_H
#define GCC_RVTT_CC_REGION_H

/* The single GIMPLE-side analysis of the structured CC (lane-enable)
   frame structure: the tree of sfppushc (0) / sfppopc (0) frames,
   computed once per function, that the frame-shape consumers
   (store-fold, ccmask, lut-select, reassoc's window CC arm) query
   instead of each re-deriving the same facts with a private scan
   (FABLE_GOES_BURR item #14; AUDIT-licensed-folds.md improvement 4).

   Stage A contract (CLASS-I): every consumer keeps its full historical
   shape discipline as a compatibility predicate (each converted
   recognizer accepts EXACTLY its old shape set) and, where that
   predicate admits, additionally requires the tree to agree; under
   flag_checking the agreement is a hard assert (the recompute-assert
   leg -- a divergence between a consumer's local scan and the tree is
   a FINDING).  Generated code cannot change while the tree agrees
   everywhere the old scans fire, and a disagreement fails closed to
   the consumer's standing named refusal.

   Structure.  A region is one static pushc frame: node = {parent,
   depth, entry pushc, exit popc statements, the mask-refinement
   statements recorded inside the frame (the positive x-form/setcc
   vocabulary the shape matchers trust today), poison facts (SFPENCC
   can enable lanes beyond the enclosing mask; CC writers outside the
   vocabulary; raw asm / foreign calls), and the word-exact all-lanes
   SFPENCC fact (rvtt_all_lanes_encc_p)}.  The root region is the
   ambient frame (depth 0, no entry).

   The tree is built by one linear walk per basic block plus CFG
   stitching over the structured lowering's canonical forms.  The one
   blessed non-linear form is the v_endif counted-pop destructor
   diamond: a join J whose in-frames disagree by exactly one region R,
   where every shorter-stack edge comes from a drain block P (single
   pred J, single succ J, exactly one sfppopc (0) and no other
   CC-relevant statement) and J itself carries no CC-relevant
   statement; that popc is recorded as R's exit and J's statements are
   left unmapped (they execute under a varying frame).  Anything else
   unstructured -- an sfppushc/sfppopc with a nonzero operand, a pop of
   the ambient frame, depth beyond the architectural 8, an unresolvable
   join mismatch -- marks the enclosing frames cc-region-unstructured
   and leaves the affected statements unmapped: every query against
   them refuses, fail-closed.  */

/* Poison and structure facts, per region (self only; query helpers
   fold in ancestors where the contract wants subtree semantics).  */
enum rvtt_cc_region_flag
{
  /* The frame's structure is not proven: a breaker occurred while it
     was open, or its subtree contains one.  */
  RVTT_CCR_UNSTRUCTURED = 1 << 0,
  /* An SFPENCC executed inside the frame: the lane-enable state may
     exceed the enclosing mask.  */
  RVTT_CCR_ENCC = 1 << 1,
  /* A CC-writing statement outside the positive refinement vocabulary
     executed inside the frame (mask changed by a word the shape
     matchers do not model).  */
  RVTT_CCR_VOCAB_EXTERNAL = 1 << 2,
  /* A raw asm or a call with unknown body executed inside the frame
     (may contain arbitrary CC words).  */
  RVTT_CCR_OPAQUE = 1 << 3,
  /* A word-exact architectural all-lanes SFPENCC
     (rvtt_all_lanes_encc_p / the typed sfpencc_all_lanes) executed
     inside the frame.  */
  RVTT_CCR_ALL_LANES = 1 << 4,
};

struct rvtt_cc_region
{
  /* Stable index (creation order).  */
  unsigned index;
  /* Enclosing frame; null for the root (ambient) region.  */
  rvtt_cc_region *parent;
  /* Frames deep: root = 0.  */
  unsigned depth;
  /* The opening sfppushc (0); null for the root.  */
  gcall *entry;
  /* The closing sfppopc (0) statements (one per exit path).  */
  auto_vec<gimple *> exits;
  /* Mask-refining statements recorded in this frame, in walk order
     (program order when the frame is single-block, reverse-post-order
     by block otherwise).  */
  auto_vec<gimple *> refinements;
  /* rvtt_cc_region_flag mask (facts recorded in this frame itself).  */
  unsigned flags;
  /* Union of the poison bits of every descendant frame (maintained at
     fact-recording time; poisoned_p folds it in).  */
  unsigned subtree_flags;
  /* When an exit was recognized through the counted-pop destructor
     diamond: the join block.  */
  basic_block drain_join;
};

class rvtt_cc_region_tree
{
public:
  /* Build the tree for FN's current IL.  */
  rvtt_cc_region_tree (function *fn);
  ~rvtt_cc_region_tree ();

  /* Rebuild after an IL mutation that inserted or removed statements
     the tree must answer for (consumers that only delete whole frames
     or touch CC-inert statements need not rebuild).  */
  void rebuild ();

  /* The frame STMT executes under, or null when unknown (statement not
     in the analyzed IL, in a broken/unreachable block, or in a drain
     join).  An sfppushc maps to the frame it opens; an sfppopc to the
     frame it closes.  */
  rvtt_cc_region *region_of (gimple *stmt) const;

  /* The frame opened by the sfppushc (0) PUSHC, or null.  */
  rvtt_cc_region *region_opened_by (gimple *pushc) const;

  /* Both statements proven to execute under the same frame.  */
  bool same_frame_p (gimple *a, gimple *b) const;

  /* OUTER's frame is the parent frame of INNER's.  */
  bool parent_frame_p (gimple *outer, gimple *inner) const;

  /* POPC is a recorded exit of R.  */
  bool closes_frame_p (gimple *popc, const rvtt_cc_region *r) const;

  /* R and all its ancestors are structurally proven.  */
  bool structured_p (const rvtt_cc_region *r) const;

  /* Every CC-relevant statement recorded in R itself is a
     positive-vocabulary mask refinement (no SFPENCC, no
     vocabulary-external CC writer, no opaque statement) and R is
     structured.  */
  bool refinements_pure_p (const rvtt_cc_region *r) const;

  /* The mask state inside R may exceed the enclosing mask or is not
     modeled: SFPENCC / vocabulary-external / opaque, in R or any
     descendant.  */
  bool poisoned_p (const rvtt_cc_region *r) const;

  /* A word-exact all-lanes SFPENCC executed in R itself.  */
  bool region_all_lanes_p (const rvtt_cc_region *r) const;

  /* The refinement chain of R (see rvtt_cc_region::refinements).  */
  const vec<gimple *> &refinement_chain (const rvtt_cc_region *r) const;

  /* The root (ambient) region.  */
  rvtt_cc_region *root () const { return m_root; }

  /* Cross-call carry fold (FABLE_GOES_BURR item #15, stage A): the
     function provably PRESERVES the all-lanes ambient lane-enable
     state across its whole execution -- every reachable block
     structurally proven (no break anywhere, blessed drain joins
     admitted: they carry no CC-relevant statement by construction), no
     frame unstructured or opaque anywhere, and the root (ambient)
     frame itself CC-inert: no refinement, no SFPENCC (the all-lanes
     ENCC included -- its ordering against a second root ENCC is not
     folded here; sharpening that is stage-B precision), no
     vocabulary-external write.  Inner frames may refine or ENCC
     freely: their recorded popc restores the saved state.  Fail-closed
     in every ambiguous direction.  Stage A only CARRIES this fact
     across calls (rvtt-ipa-summary); no consumer admission widens on
     it in this item.  */
  bool ambient_preserving_fold_p () const;

  /* Loop-scoped carry of the same discipline (FABLE_GOES_BURR R2 /
     the crossloop-cc-unproven widening): LOOP's CC activity provably
     PRESERVES the ambient lane-enable state it was entered with and
     can only NARROW the state observed at any point inside -- every
     CC-relevant statement in the loop body is mapped to a
     structurally proven frame; every closed frame was opened inside
     the loop (a popc of an outside save would rewind past the loop
     entry's state); no opaque statement (raw `.ttinsn' constant words
     of the audited CC-INERT class excepted -- rvtt-raw-boundary.cc;
     the ALL_LANES class is a widening here and refuses) and no
     SFPENCC anywhere in the body (ENCC can widen beyond the entry
     ambient -- the all-lanes form included, since the entry ambient
     is not proven all-lanes here); and the ambient (root) frame
     itself carries no refinement or vocabulary-external write inside
     the loop.
     In-frame refinements, COMPC and vocabulary-external writers are
     admitted: relative to their frame entry every one of them narrows
     (pinned-sim for_each_lane discipline), and the frame's recorded
     popc restores the saved state.  Fail-closed in every ambiguous
     direction (unmapped statement, unstructured frame, drain-join or
     broken block carrying CC words).  */
  bool loop_cc_ambient_preserving_p (class loop *loop) const;

  /* The lane-enable state carried into edge E is provably the
     architectural ALL-LANES state: every backward CFG path from E
     reaches the function entry (all-lanes ambient) or a block whose
     LAST CC-relevant event is a word-exact all-lanes SFPENCC (typed
     rvtt_all_lanes_encc_p, or a raw `.ttinsn' word of the audited
     ALL_LANES class) before any other CC-relevant statement.  The
     kill-modeling discipline is gimple-rvtt-prgm-const.cc's
     prepeel_ambient_all_lanes_p, made fail-closed on calls and on
     unaudited raw words (no TU-audit gate is assumed here).  Under
     this fact a placement at E writes EVERY lane, so ANY in-loop
     enable state is a subset of E's -- the containment fact holds for
     arbitrary crossed CC activity (FABLE_GOES_BURR R2; the
     tt/proofs/cc-narrowing-writers/ record carries the argument).  */
  bool edge_entry_all_lanes_p (edge e) const;

private:
  /* Backward-walk core of edge_entry_all_lanes_p.  */
  bool block_entry_all_lanes_p (basic_block bb) const;
  void build ();
  void clear ();

  /* Every reachable block's walk completed structurally (see
     ambient_preserving_fold_p).  */
  bool m_complete;

  function *m_fn;
  rvtt_cc_region *m_root;
  /* Owned region nodes.  */
  auto_vec<rvtt_cc_region *> m_regions;
  /* Statement -> region ('this' pointer keyed; see region_of).  */
  hash_map<const gimple *, rvtt_cc_region *> *m_map;
  /* pushc stmt -> region it opens.  */
  hash_map<const gimple *, rvtt_cc_region *> *m_opened;
};

/* The reassoc window walk's CC-arm vocabulary, centralized: true when
   STMT is a CC event for a code-motion window -- a raw asm, a call
   that is not a typed rvtt builtin, a CC-writing rvtt call
   (rvtt_insn_data::sets_cc), or the typed all-lanes SFPENCC (no CC()
   flag by design).  Exactly the historical
   gimple-rvtt-reassoc.cc window classification's CC subset; a pure
   per-statement fact (needs no tree instance, so freshly inserted
   statements answer correctly).  */
extern bool rvtt_cc_window_cc_event_p (gimple *stmt);

/* Build a tree for FN and fold ambient_preserving_fold_p over it (the
   rvtt-ipa-summary CC-carry entry point; fail-closed on a missing or
   non-gimple body).  */
extern bool rvtt_cc_region_fn_ambient_preserving_p (function *fn);

#endif /* GCC_RVTT_CC_REGION_H */
