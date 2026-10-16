/* Tensix CC-region tree (pushc/popc frame structure).
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

/* GIMPLE-side analysis of the structured CC (lane-enable) frames: the
   tree of sfppushc (0) / sfppopc (0) frames, computed once per function so
   that passes reasoning about frame shape can query it rather than rescan.

   A consumer keeps its own shape check and also requires the tree to agree.
   Under flag_checking a disagreement asserts; otherwise the consumer
   refuses, so the tree can only make a consumer refuse, never accept a
   shape it would otherwise reject.

   Each region is one static pushc frame, recording its parent, depth,
   entry pushc, exit popcs, the mask-refining statements inside it and
   poison facts (SFPENCC, CC writers outside the refinement vocabulary,
   raw asm or unknown calls).  The root region is the ambient frame
   (depth 0, no entry).

   The tree is built by a linear walk of each basic block plus CFG
   stitching over the canonical forms of the structured lowering.  The one
   accepted non-linear form is the v_endif counted-pop diamond: a join J
   whose incoming frames differ by exactly one region R, where every
   shorter-stack edge comes from a drain block P (single pred and succ J,
   exactly one sfppopc (0) and no other CC-relevant statement) and J has no
   CC-relevant statement.  That popc is recorded as R's exit and J's
   statements stay unmapped.  Any other unstructured shape (nonzero
   pushc/popc operand, a pop of the ambient frame, depth beyond the
   hardware limit of 8, an unresolvable join) marks the enclosing frames
   unstructured and leaves the affected statements unmapped, so queries
   on them refuse.  */

/* Per-region facts (the region itself only; queries fold in ancestors or
   descendants where needed).  */
enum rvtt_cc_region_flag
{
  /* The frame's structure is not proven.  */
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
  /* An all-lanes SFPENCC (rvtt_all_lanes_encc_p) executed inside the
     frame.  */
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
  /* Union of the poison bits of every descendant frame.  */
  unsigned subtree_flags;
  /* The join block when an exit was recognized through the counted-pop
     diamond.  */
  basic_block drain_join;
};

class rvtt_cc_region_tree
{
public:
  /* Build the tree for FN's current IL.  */
  rvtt_cc_region_tree (function *fn);
  ~rvtt_cc_region_tree ();

  /* Rebuild after inserting or removing statements the tree must answer
     for.  Deleting whole frames or touching CC-inert statements does not
     require a rebuild.  */
  void rebuild ();

  /* The frame STMT executes under, or null when unknown (not in the
     analyzed IL, in an unstructured or unreachable block, or in a drain
     join).  An sfppushc maps to the frame it opens, an sfppopc to the
     frame it closes.  */
  rvtt_cc_region *region_of (gimple *stmt) const;

  /* The frame opened by the sfppushc (0) PUSHC, or null.  */
  rvtt_cc_region *region_opened_by (gimple *pushc) const;

  /* True if A and B are known to execute under the same frame.  */
  bool same_frame_p (gimple *a, gimple *b) const;

  /* True if OUTER's frame is the parent of INNER's.  */
  bool parent_frame_p (gimple *outer, gimple *inner) const;

  /* True if POPC is a recorded exit of R.  */
  bool closes_frame_p (gimple *popc, const rvtt_cc_region *r) const;

  /* True if R and all its ancestors are structured.  */
  bool structured_p (const rvtt_cc_region *r) const;

  /* True if R is structured and every CC-relevant statement recorded in
     R itself is a vocabulary mask refinement.  */
  bool refinements_pure_p (const rvtt_cc_region *r) const;

  /* True if the mask inside R may exceed the enclosing mask or is not
     modeled (SFPENCC, vocabulary-external or opaque statement in R or a
     descendant).  */
  bool poisoned_p (const rvtt_cc_region *r) const;

  /* True if an all-lanes SFPENCC executed in R itself.  */
  bool region_all_lanes_p (const rvtt_cc_region *r) const;

  /* The refinement chain of R (see rvtt_cc_region::refinements).  */
  const vec<gimple *> &refinement_chain (const rvtt_cc_region *r) const;

  /* The root (ambient) region.  */
  rvtt_cc_region *root () const { return m_root; }

  /* True if the function preserves the all-lanes ambient lane-enable
     state: every reachable block is structured, no frame is unstructured
     or opaque, and the root frame itself has no refinement, SFPENCC or
     vocabulary-external write.  Inner frames may do anything, since their
     popc restores the saved state.  Used by rvtt-ipa-summary for dumps
     only.  */
  bool ambient_preserving_fold_p () const;

  /* True if LOOP preserves the lane-enable state it was entered with and
     can only narrow it inside: every CC-relevant statement in the body is
     mapped to a structured frame opened inside the loop, there is no
     SFPENCC (it can widen beyond the entry state) and no opaque statement
     other than CC-inert raw .ttinsn words (rvtt-raw-boundary.cc), and the
     root frame has no refinement or vocabulary-external write in the loop.
     In-frame refinements, SFPCOMPC and other writers only narrow relative
     to their frame entry, and the frame's popc restores the saved state.  */
  bool loop_cc_ambient_preserving_p (class loop *loop) const;

  /* True if the lane-enable state on edge E is known to be all lanes:
     every backward path from E reaches the function entry or a block whose
     last CC-relevant event is a typed all-lanes SFPENCC.  Calls and raw
     words that may change the CC state refuse.  Under this fact any in-loop enable state is a subset of E's,
     whatever CC activity is crossed.  */
  bool edge_entry_all_lanes_p (edge e) const;

private:
  /* Backward-walk core of edge_entry_all_lanes_p.  */
  bool block_entry_all_lanes_p (basic_block bb) const;
  void build ();
  void clear ();

  /* Every reachable block's walk completed structurally.  */
  bool m_complete;

  function *m_fn;
  rvtt_cc_region *m_root;
  /* Owned region nodes.  */
  auto_vec<rvtt_cc_region *> m_regions;
  /* Statement -> region.  */
  hash_map<const gimple *, rvtt_cc_region *> *m_map;
  /* pushc stmt -> region it opens.  */
  hash_map<const gimple *, rvtt_cc_region *> *m_opened;
};

/* True if STMT is a CC event for a code-motion window walk: a raw asm, a
   call that is not a typed rvtt builtin, a CC-writing rvtt call, or the
   typed all-lanes SFPENCC (which has no CC flag).  Needs no tree, so it
   answers correctly for newly inserted statements.  */
extern bool rvtt_cc_window_cc_event_p (gimple *stmt);

/* Build a tree for FN and return ambient_preserving_fold_p.  False if FN
   has no GIMPLE body.  */
extern bool rvtt_cc_region_fn_ambient_preserving_p (function *fn);

/* RTL-side view, implemented in rvtt-cc-region-rtl.cc.

   This is derived from the post-RA insn stream rather than carried over
   from the GIMPLE tree: by then the statements the tree is keyed on are
   gone, and every CC event is an explicit typed instruction whose effects
   the typed-effect table describes.

   The question is span-scoped: for the insns strictly between two points
   of one basic block, how does the lane-enable state relate to the state M
   at the span entry?  The classifier walks the span once, tracking the
   pushc/popc depth relative to the span entry:

     - sfppushc (0) opens a frame and sfppopc (0) closes one; a pop at
       relative depth 0 or a nonzero operand refuses;
     - SFPSETCC, the SET_CC SFPGT/SFPLE compares and the lane-local
       conditional CC writers only narrow the mask (they change flags
       only on enabled lanes), and a frame's popc restores its entry
       state, so M bounds every point of the span;
     - SFPCOMPC narrows relative to the enclosing save, so it is admitted
       only inside an in-span frame;
     - the all-lanes SFPENCC can widen beyond M, so it requires M to be
       all lanes; any other SFPENCC refuses;
     - every other CC writer and every opaque instruction refuses.

   The all-lanes entry proof is the backward walk of
   edge_entry_all_lanes_p redone on RTL.  Raw .ttinsn words never end the
   walk as a kill, since a raw word can sit inside a replay record window
   and not execute (rvtt-raw-boundary.h); for the same reason a REPLAY
   instruction poisons the rest of its block.  */

/* Span classification results.  The admitting results are ordered:
   NO_EVENT means the span has no CC event; BALANCED means all CC activity
   is in balanced in-span frames with narrowing-only writers (every mask
   in the span is a subset of M and the end state is M); ALL_LANES means
   depth-0 mask activity proven against an all-lanes entry.  The REFUSE
   results name the reason.  */
enum rvtt_cc_rtl_span_verdict
{
  RVTT_CC_RTL_SPAN_NO_EVENT,
  RVTT_CC_RTL_SPAN_BALANCED,
  RVTT_CC_RTL_SPAN_ALL_LANES,
  /* An sfppopc (0) at relative depth 0, popping a save taken outside
     the span.  */
  RVTT_CC_RTL_SPAN_REFUSE_OUTSIDE_POP,
  /* An in-span frame still open at a position whose mask must equal
     the entry mask.  */
  RVTT_CC_RTL_SPAN_REFUSE_UNBALANCED,
  /* The end state must equal the entry mask but a depth-0 event
     narrowed it with no word-exact restore.  */
  RVTT_CC_RTL_SPAN_REFUSE_END_MASK,
  /* Depth-0 (or widening) mask activity whose admission needs the
     all-lanes entry proof, which failed.  */
  RVTT_CC_RTL_SPAN_REFUSE_ENTRY_UNPROVEN,
  /* An SFPENCC that is not the word-exact all-lanes form.  */
  RVTT_CC_RTL_SPAN_REFUSE_ENCC,
  /* An sfppushc/sfppopc with a nonzero operand (unmodeled shape).  */
  RVTT_CC_RTL_SPAN_REFUSE_NONZERO_MOD,
  /* A CC writer outside the audited vocabulary.  */
  RVTT_CC_RTL_SPAN_REFUSE_VOCAB,
  /* An opaque instruction (call, asm, raw word, unaudited pattern)
     in the interior.  */
  RVTT_CC_RTL_SPAN_REFUSE_OPAQUE,
  /* A replay instruction at or before the span end: CC events in the
     span may be recorded rather than executed (rvtt-raw-boundary.h), so
     only NO_EVENT can be trusted.  */
  RVTT_CC_RTL_SPAN_REFUSE_REPLAY,
};

/* Classify the nondebug insns of BB strictly after AFTER and strictly
   before UNTIL (NULL means the end of BB).  If REQUIRE_ENTRY_MASK_AT_END,
   the lane-enable state at the span end must be exactly the entry state M
   (a killing write must cover exactly the lanes the definition wrote);
   otherwise it may be any subset of M.  Both insns must be in BB.  */
extern rvtt_cc_rtl_span_verdict
rvtt_cc_rtl_classify_span (basic_block bb, rtx_insn *after,
			   rtx_insn *until, bool require_entry_mask_at_end);

/* The dump name of V.  */
extern const char *rvtt_cc_rtl_span_verdict_name (rvtt_cc_rtl_span_verdict v);

/* True if V admits the span.  */
inline bool
rvtt_cc_rtl_span_admissible_p (rvtt_cc_rtl_span_verdict v)
{
  return v == RVTT_CC_RTL_SPAN_NO_EVENT
	 || v == RVTT_CC_RTL_SPAN_BALANCED
	 || v == RVTT_CC_RTL_SPAN_ALL_LANES;
}

/* True if the lane-enable state on entry to AT is known to be all lanes
   (the RTL counterpart of edge_entry_all_lanes_p).  */
extern bool rvtt_cc_rtl_entry_all_lanes_p (rtx_insn *at);

#endif /* GCC_RVTT_CC_REGION_H */
