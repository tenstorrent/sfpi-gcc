/* Macro-planner descriptor synthesis (Layer 4).
   Copyright (C) 2026 Tenstorrent Inc.

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

#ifndef GCC_RVTT_MACRO_DESC_H
#define GCC_RVTT_MACRO_DESC_H

#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-tables.h"

/* A synthesized descriptor: templates, sequences, misc, SETC16 programs,
   and per-row launch tuples, selected from the capability tables' PROVEN
   whole-word programs keyed by the derived event structure (never by
   shape or operation names), with template fields packed from admitted
   source operands where the field derivation is established.  Synthesis
   is in-memory only at this stage: output goes to the analyze dump and
   the Layer-7 verifier; WP7 owns emission.  */

struct macro_launch_spec
{
  unsigned macro_index;
  unsigned vd;			/* planned launch VD (even-row value)  */
  bool vd_alternates;		/* odd rows use vd ^ 1		       */
  /* Store-only carrier: the launch VD is sacrificial (written, never
     read); a fixed VD is then harmless across back-to-back rows.  A
     VALUE carrier's fixed VD is not -- its hosted consumers pend past
     the next row's launch (see the inter-row drain in form_region,
     lane EV P0 adjudication 2026-08-21).  */
  bool is_store_only;
  unsigned mode;
  unsigned addr_mode;
  unsigned address;
  uint32_t word;		/* encoded launch word (even-row)      */
  uint32_t word_alt;		/* odd-row word when vd_alternates     */
};

/* CC-template model (WP9): the descriptor's representation of a proven
   CC-writing calendar.  The row's predicate DEFINITION is a launched
   template event; its CC result becomes visible to later issue slots
   after the architectural deferred-CC lag (capability tables,
   cc_visibility_lag); the payload load issued before that slot executes
   under the ambient all-lanes mask, the one issued at or after it under
   the definition.  The scheduled store's lane predicate is the LIVE CC
   state at the store's execution cycle
   (store_lane_mask_live_at_execution; silicon adjudication 2026-08-17,
   craq-sim 9f324140 -- the launch never latches it), so the row-end
   all-lanes RESTORE must retire STRICTLY BEFORE the store executes: in
   visible-slot form, restore_visible_slot <= store_exec_slot (the
   restore's CC write, visible to issues from restore_exec + lag on, is
   visible to an event executing at cycle E exactly when restore_exec <
   E; with lag = 1 the two forms coincide).  A violating schedule
   refuses cc-restore-store-race -- the 4-slot separator-kept select
   calendar's silicon failure mode.  The store must also retire before
   the NEXT row's predicate definition executes (same race, other
   edge), and the restore's visibility slot must not exceed the row
   initiation interval so the next row opens under the restored mask.
   Every slot below is derived from the matched program's proven delays
   -- synthesis refuses when any obligation fails.  */
struct macro_cc_model
{
  bool active;
  bool complement;		/* template predicate sense is the
				   architectural complement of the
				   source's (the post-visibility load
				   carries the merge's LIVE operand)   */
  int def_visible_slot;		/* first issue slot seeing the def     */
  int pre_load_slot;		/* payload load under the ambient mask */
  int post_load_slot;		/* payload load under the definition   */
  int store_exec_slot;		/* store execution cycle (reads the
				   LIVE lane mask at that cycle)       */
  int restore_visible_slot;	/* first slot seeing the restore       */
  int row_interval;		/* schedule ii the visibility must meet*/
};

struct macro_descriptor
{
  uint32_t templ[4];
  unsigned n_templates;
  uint32_t seq[4];		/* seq[k] = sequence word of macro k   */
  unsigned n_seq;
  uint32_t misc;
  bool has_misc;
  rvtt_macro::setc16_program setc16[8];
  unsigned n_setc16;
  vec<macro_launch_spec> launches;
  int drain_slots;
  bool needs_all_lanes_prefix;	/* lane-predicated rows need SFPENCC   */
  /* The proven program keeps the row's explicit typed separator in
     place instead of absorbing the stride: its issue slot is the
     restore's visibility slot (see macro_cc_model).  */
  bool keep_separator;
  macro_cc_model cc;
  uint32_t planned_lregs;	/* planner-owned physical LREG mask    */
  const char *refusal;		/* stable name; null = synthesized     */
};

/* Stable refusal names (append-only dump API).  */
extern const char *macro_desc_refusal_program_unproven;
extern const char *macro_desc_refusal_encoding_failed;
extern const char *macro_desc_refusal_verification_failed;
/* A CC-writing row whose dataflow, sense mapping, or visibility timing
   the CC-template model cannot prove.  */
extern const char *macro_desc_refusal_cc_template_unproved;
/* A CC-writing row whose derived calendar races the all-lanes restore
   against the scheduled store: the restore does not retire strictly
   before the store executes (or the store retires at/after the next
   row's predicate definition), so the store would execute under a
   predicate mask via the live lane-enable evaluation
   (store_lane_mask_live_at_execution).  The architectural constraint
   behind the 2026-08-17 silicon adjudication's separator-kept 4-slot
   failure; supersedes the structural stopgap
   cc-separator-kept-silicon-unproven.  */
extern const char *macro_desc_refusal_cc_restore_store_race;

/* Synthesize REGION/SCHEDULE into OUT.  Returns false when synthesis
   could not begin (no capability table); OUT->refusal names any other
   failed obligation.  Never mutates the function.  */
extern bool rvtt_macro_synthesize (const macro_region &region,
				   const macro_schedule &schedule,
				   macro_descriptor *out, FILE *dump);
extern void rvtt_macro_descriptor_release (macro_descriptor *);

/* Layer-7a in-tree verification of a synthesized descriptor against the
   region's explicit facts (rvtt-macro-verify.cc).  Runs under
   -mtt-tensix-macro-planner-verify and always under checking.  Returns
   null on success, else the failing component's stable tag; a non-null
   return is a descriptor refusal and must prevent form_region.  */
extern const char *rvtt_macro_verify_descriptor
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, FILE *dump);

/* Build the verifier's expectations from the region's explicit facts
   (implemented beside synthesis; consumed by rvtt-macro-verify.cc).  */
namespace rvtt_macro_verify { struct expectations; }
/* WP12 scheduler-facing helpers (rvtt-macro-desc.cc).  */
extern int rvtt_macro_hosted_subunit (rtx_insn *);
extern unsigned rvtt_macro_store_only_sacrificial_vd (uint32_t internal_lregs);
extern bool rvtt_macro_derived_template_probe (rtx_insn *, int launch_vd,
					       uint8_t *opcode, uint8_t *mod1,
					       uint8_t *src_c, uint16_t *imm12);

extern bool rvtt_macro_build_expectations
  (const macro_region &region, const macro_schedule &schedule,
   rvtt_macro_verify::expectations *out);

/* Drain-aware boundary placement (rtl-rvtt-schedule.cc, consumed by the
   planner's emission under -mtt-tensix-optimize-drain-schedule): prove
   that the derived drain of the run ending at row END-1 may be elided at
   the boundary into the next run [END, NEXT_END), because every
   in-flight macro event's writeback provably precedes the first
   conflicting follower access.  All distances derive from the
   descriptor's own SequenceBits delays (the derived timing calendars);
   refusals are named to DUMP and keep the full derived drain.  */
extern bool rvtt_macro_drain_boundary_elidable
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, unsigned begin, unsigned end,
   unsigned next_end, FILE *dump);
/* Loop-backedge drain elision (lane CA, the drain-route remainder):
   prove that a loop-body region's FINAL run may elide its in-body
   drain because the backedge follower stream -- the in-body tail, the
   loop-head prefix, and the region's own first run [0, FIRST_RUN_END)
   in the next iteration -- provably orders every access after every
   pending writeback.  The caller must emit the full derived drain on
   the loop's exit path (the architectural exit contract is preserved,
   only its placement moves from once-per-trip to once-per-exit).  */
extern bool rvtt_macro_drain_backedge_elidable
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, unsigned first_run_end, FILE *dump);
/* ------------------------------------------------------------------ */
/* WP13: descriptor-program residency (default-off,
   -mtt-tensix-macro-planner-residency).  Dictionary-selection residency
   of DERIVED descriptor programs (Lefurgy-line adaptation, literature
   scan 2026-08-18 Idea 6): descriptor words are canonicalized by
   CONTENT (the bit-exact derived template/sequence/misc words -- never
   shape or operation identity) and become resident kernel-wide when the
   dominance and owned-state-invariance proofs hold, so identical
   descriptor programs are pushed once per kernel instead of once per
   region or per enclosing-loop trip.  Increment-1 selection policy:
   first-formed-wins, content-equality only, no eviction -- with
   identical content the descriptor register file is never contended, so
   the residency knapsack degenerates; capacity and field layouts are
   capability-table facts; the R2 delivery model (rvtt-cost.md,
   RISC_PUSH_X100) prices the dump diagnostics only.  The per-region
   ambient enable and owned SETC16 program are never elided (AT
   PREFIX-LEDGER rows 1-4 contract discharges are separate follow-ups).
   Every refusal keeps today's placement byte-identically.  */

struct macro_residency_entry
{
  uint32_t templ[4];
  unsigned n_templates;
  uint32_t seq[4];
  unsigned n_seq;
  uint32_t misc;
  bool has_misc;
  basic_block placement;	/* block whose end programs the words   */
};

struct macro_residency_state
{
  auto_vec<macro_residency_entry> programmed;
  /* Every insn emitted by this planner invocation (programming,
     retained prefixes, calendars): benign for the residency walks by
     construction.  */
  hash_set<rtx_insn *> emitted;
};

/* Stable refusal names (append-only dump API).  */
extern const char *macro_resid_refusal_skip_path;  /* function not
						      owned-state clean  */
extern const char *macro_resid_refusal_span;	   /* dedupe span dirty  */
extern const char *macro_resid_refusal_dominance;  /* no dominating
						      resident program   */

/* Outward residency extension: iterate the WP11 configuration-epoch
   proof through successively enclosing loops from the already-proven
   placement in *HOIST_PREHEADER / *HOIST_EDGE, gated by the
   whole-function owned-state invariance walk (the skip-path inertness
   discharge: paths that reach the resident program but not the region
   observe nothing -- the enable re-asserts the outermost-CC all-lanes
   contract under WP11's materialization license, and the owned-dest
   words are unread outside the region).  On success updates the
   placement to the outermost proven level and returns true; on any
   refusal leaves the placement untouched (WP11 behavior) and returns
   false.  Proof-only: never mutates the function.  */
extern bool rvtt_macro_residency_extend (function *fn,
					 const macro_region &region,
					 const macro_descriptor &desc,
					 const rvtt_macro::caps *c,
					 macro_residency_state *state,
					 basic_block *hoist_preheader,
					 edge *hoist_edge,
					 unsigned *levels, FILE *dump);

/* Content-equality de-duplication: true when DESC's descriptor words
   are bit-identical to an already-programmed entry whose placement
   dominates REGION's launch block and the whole-function
   owned-state invariance holds -- the region may then elide its
   descriptor-word programming entirely (retained enable/SETC16 stay).
   Proof-only: never mutates the function.  */
extern bool rvtt_macro_residency_lookup (function *fn,
					 const macro_region &region,
					 const macro_descriptor &desc,
					 const rvtt_macro::caps *c,
					 macro_residency_state *state,
					 FILE *dump);

/* Record DESC's programming placement after a successful emission.  */
extern void rvtt_macro_residency_record (const macro_descriptor &desc,
					 basic_block placement,
					 macro_residency_state *state);

#endif /* GCC_RVTT_MACRO_DESC_H */
