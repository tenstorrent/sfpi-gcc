/* Tensix per-function IPA summaries (whole-body rescans, stage A).
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

#ifndef GCC_RVTT_IPA_SUMMARY_H
#define GCC_RVTT_IPA_SUMMARY_H

/* Interprocedural summaries, carry-only stage: facts computed
   once per function body and consulted across call boundaries, so that
   no tt pass re-walks another function's statements per consumer.

   The summary is a per-cgraph-node record of classified-statement
   DIGESTS: each face's classifier runs once over the body (in the
   consumer file that owns the classifier -- the classification code is
   not forked here) and records a parameter-independent event stream;
   consumers replay the stream against their own query parameters
   (owned rows, contract decls, the forming function) with the exact
   legacy fold.  Replaying the digest is verdict-identical to
   re-walking the body BY CONSTRUCTION for every parameterization,
   because the recorded events carry everything the legacy per-stmt
   classification depended on beyond the parameters.

   Faces (each computed lazily, on first consult):

     - init/epoch face (gimple-rvtt-crosscall.cc): the init-hoist and
       ADDR_MOD-hoist chain-hop whole-body scans (init_scan_stmt);

     - MOP cover face (rtl-rvtt-mop-form.cc): the outward-ownership
       must-dataflow's per-statement classification plus the CFG
       skeleton it runs over;

     - CC carry (rvtt-cc-region): the function-level all-lanes-ambient
       preservation fact, folded from the CC-region tree.  This stage
       only CARRIES the fact (dump + API); no consumer admission widens
       on it (a later widening stage owns that, by name).

   VALIDITY.  A digest answers for one snapshot of the subject's gimple
   body.  Every tt pass that mutates ANOTHER function's body (the
   crosscall/init/ADDR_MOD contract commits) invalidates the subject's
   record explicitly (rvtt_ipa_summary_invalidate); cgraph removal
   drops records and clone duplication never copies them.  Consults are
   legal only while the subject body is still gimple -- consumers keep
   their legacy fail-closed checks (body released / past gimple) with
   their standing refusal names.  Two belts back the explicit wiring:
   an O(1) block/edge-count signature checked on every consult (a
   mismatch fires the registered `ipa-summary-stale' name and recomputes
   fail-closed -- recomputing from the current body is exactly the
   legacy read-at-consult behavior), and under flag_checking a full
   statement-count signature.  (The consumer files' one-pin shadow
   legacy walks hard-asserted verdict identity through the corpus
   -fchecking leg and were deleted at pin 53.)  */

/* One classified statement event.  The union of the faces' event
   vocabularies; each face uses its own subset.  */

struct rvtt_ipa_event
{
  enum kind_t : unsigned char
  {
    /* A delivered instruction word whose constant image (or constant
       opcode base, WORD_EXACT false) is WORD; the face's word
       classifier re-runs on WORD against the consumer's parameters at
       replay time.  Init face only.  */
    EV_DELIVER,
    /* A typed CC-writing rvtt call (init face).  */
    EV_CC_WRITE,
    /* A statement the face refuses unconditionally (parameter-free);
       WHAT is the registered refusal name.  Init face only.  */
    EV_REFUSE,
    /* A call whose admission is consumer-side: DECL is the spelled
       call target (NULL_TREE for an unresolvable target the init face
       still orders by identity).  On the MOP face, COMPOSABLE records
       whether the callee had an analyzable in-TU body at digest time
       (the compose-vs-extern-benign split).  */
    EV_CALL,
    /* MOP face: a launch requiring BITS of template state; WHAT is the
       classification detail for the hazard dump.  */
    EV_LAUNCH,
    /* MOP face: a write covering BITS of template state.  */
    EV_COVER,
  };
  kind_t kind;
  bool word_exact;
  bool composable;
  uint32_t word;
  unsigned bits;
  /* Provenance for refusal dumps; valid exactly as long as the digest
     is (the body-mutation invalidation contract above).  */
  gimple *stmt;
  tree decl;
  const char *what;
};

/* MOP face: one basic block's digest -- the classified events plus the
   CFG skeleton the outward-ownership dataflow iterates over.  */

struct rvtt_ipa_mop_block
{
  int index;
  /* Some successor edge reaches the function's exit block.  */
  bool exit_succ;
  /* Predecessor block indices; -1 encodes the entry block.  */
  vec<int> preds;
  vec<rvtt_ipa_event> events;
};

/* MOP face: the whole digest is the block list in FOR_EACH_BB_FN
   order plus the block-index space bound.  */

/* The per-function record.  Face payloads are owned here and released
   on invalidation / node removal.  */

struct rvtt_ipa_fn_summary
{
  /* O(1) staleness proxy (production belt).  */
  int sig_blocks = -1;
  int sig_edges = -1;
  /* Full statement-count signature (flag_checking belt).  */
  unsigned sig_stmts = 0;

  bool init_computed = false;
  vec<rvtt_ipa_event> init_events = vNULL;

  bool mop_computed = false;
  int mop_nblocks = 0;
  vec<rvtt_ipa_mop_block> mop_blocks = vNULL;

  bool cc_computed = false;
  bool cc_ambient_preserving = false;

  void release_faces ();
};

/* The record for NODE, creating it empty on first consult.  Returns
   null when the node has no walkable gimple body (consumers fail
   closed with their standing names).  A record whose body signature no
   longer matches is dropped and recreated empty (see VALIDITY).  */
extern rvtt_ipa_fn_summary *rvtt_ipa_summary_get (cgraph_node *node);

/* Explicit invalidation: FN's gimple body was just mutated by a tt
   pass acting from outside FN's own pipeline (contract commits).  */
extern void rvtt_ipa_summary_invalidate (function *fn);

/* CC carry: NODE's body provably preserves the all-lanes ambient
   lane-enable state across its execution (rvtt-cc-region fold; fail
   closed).  Stage A carrier -- no consumer admission widens on it.  */
extern bool rvtt_ipa_cc_ambient_preserving_p (cgraph_node *node);

/* TU anchor facts: the decl/symtab-level entry-root enumeration behind
   the kernel-single-TU and crt0-benign axioms, recorded once as a
   checked, dumpable property (gimple-rvtt-prgm-const.cc's tu-facts
   dump/verify surface).  Body-free by construction, so computing it
   never perturbs any body-reading census's snapshot point.  */

struct rvtt_ipa_tu_anchor_facts
{
  bool computed = false;
  /* The link model's entry anchor (gimple-rvtt-crosscall.cc,
     compute_executable_closure): `_start' when the TU defines it, else
     a public `main'.  */
  bool has_start = false;
  bool has_main = false;
  /* Enumerable external entries under the model: the anchor, or every
     externally-visible non-comdat definition when no anchor pins the
     surface; asm-callable forced definitions always.  */
  unsigned n_entry_roots = 0;
  /* Some root exists (entries, static ctors/dtors, address-taken
     definitions): the executable closure is anchorable.  A TU with
     defined bodies and no root at all is the fail-closed
     census-unrooted shape.  */
  bool rooted = false;
};

extern const rvtt_ipa_tu_anchor_facts &rvtt_ipa_tu_anchors ();

#endif /* GCC_RVTT_IPA_SUMMARY_H */
