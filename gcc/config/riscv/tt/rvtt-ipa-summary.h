/* Tensix per-function IPA summaries (whole-body rescans).
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

/* Interprocedural summaries: facts computed once per function body and
   consulted across call boundaries, so that passes do not re-walk another
   function's statements for every query.

   The summary of a cgraph node is a set of lazily computed "faces".  Each
   face's classifier (which lives in the pass that owns it) runs once over
   the body and records a parameter-independent stream of events.  Consumers
   replay the stream against their own query parameters with the same fold
   they would apply while walking the body; since the events carry
   everything the classification depends on beyond those parameters, the
   result is the same as re-walking the body.

   Faces:

     - init/epoch face: the whole-body scans used by the cross-call
       init-hoist and ADDR_MOD-hoist;

     - MOP face: the per-statement classification used by the
       outward-ownership dataflow of MOP formation, plus the CFG skeleton
       it runs over;

     - CC face: whether the function preserves the all-lanes-enabled CC
       state, from the CC-region tree.  Used only in dumps.

   Validity: a summary describes one snapshot of the gimple body.  A pass
   that modifies another function's body (the cross-call contract commits)
   must call rvtt_ipa_summary_invalidate; removing a cgraph node drops its
   record and cloning never copies one.  Summaries may only be consulted
   while the body is still gimple.  As a backstop, each consult compares a
   block/edge-count signature (and, with flag_checking, a statement count)
   and recomputes the summary on mismatch, dumping `ipa-summary-stale'.  */

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
  /* Source of the event, for dumps; valid as long as the summary is.  */
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

/* The per-function record.  Face payloads are owned here and released
   on invalidation or node removal.  The MOP face is the block list in
   FOR_EACH_BB_FN order plus the number of block indices.  */

struct rvtt_ipa_fn_summary
{
  /* Cheap staleness signature, checked on every consult.  */
  int sig_blocks = -1;
  int sig_edges = -1;
  /* Full statement-count signature (flag_checking only).  */
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

/* Return the record for NODE, creating it empty on first use.  Return
   null when NODE has no gimple body.  A record whose body signature no
   longer matches is dropped and recreated empty.  */
extern rvtt_ipa_fn_summary *rvtt_ipa_summary_get (cgraph_node *node);

/* Invalidate the record of FN, whose gimple body was just modified by a
   pass running on another function.  */
extern void rvtt_ipa_summary_invalidate (function *fn);

/* Return true if NODE's body provably preserves the all-lanes-enabled
   state across its execution.  Used only in dumps.  */
extern bool rvtt_ipa_cc_ambient_preserving_p (cgraph_node *node);

/* Live entry-point diagnostics enumerated from decls and the symbol table.
   No function body is read, so refreshing this snapshot is safe after
   lowering too.  It does not authorize an unscanned body or supply an
   incoming lane/template ABI.  */

struct rvtt_ipa_tu_anchor_facts
{
  bool computed = false;
  /* The link model's entry anchor: `_start' when the TU defines it, else a
     public `main'.  */
  bool has_start = false;
  bool has_main = false;
  /* Public definitions, including COMDAT, remain possible external
     entries even with main/_start; forced entries count as well.  */
  unsigned n_entry_roots = 0;
  /* Some root exists (entries, static ctors/dtors, address-taken
     definitions), so the reachable code can be enumerated.  A TU with
     defined bodies and no root at all is treated conservatively.  */
  bool rooted = false;
};

extern const rvtt_ipa_tu_anchor_facts &rvtt_ipa_tu_anchors ();

#endif /* GCC_RVTT_IPA_SUMMARY_H */
