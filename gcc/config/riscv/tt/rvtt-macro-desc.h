/* Macro-planner descriptor synthesis.
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
   and per-row launch tuples, selected from the capability tables' proven
   whole-word programs keyed by the derived event structure, with
   template fields packed from admitted source operands where the field
   derivation is known.  Synthesis never emits RTL: its output is
   consumed by the analysis dump, the verifier, and emission.  */

struct macro_launch_spec
{
  unsigned macro_index;
  unsigned vd;			/* planned launch VD (even-row value)  */
  bool vd_alternates;		/* odd rows use vd ^ 1		       */
  /* Store-only carrier: the launch VD is sacrificial (written, never
     read), so a fixed VD is harmless across back-to-back rows.  A value
     carrier's fixed VD is not -- its hosted consumers pend past the next
     row's launch, so formation must separate such rows with the
     inter-row drain.  */
  bool is_store_only;
  unsigned mode;
  unsigned addr_mode;
  unsigned address;
  uint32_t word;		/* encoded launch word (even-row)      */
  uint32_t word_alt;		/* odd-row word when vd_alternates     */
};

/* CC-template model: the descriptor's representation of a proven
   CC-writing calendar.  The row's predicate definition is a launched
   template event; its CC result becomes visible to later issue slots
   after the deferred-CC lag (cc_visibility_lag in the capability
   tables).  The payload load issued before that slot executes under the
   ambient all-lanes mask, the one issued at or after it under the
   definition.  The scheduled store's lane predicate is the live CC state
   at the store's execution cycle (store_lane_mask_live_at_execution; the
   launch never latches it), so the row-end all-lanes restore must retire
   strictly before the store executes: restore_visible_slot <=
   store_exec_slot (the restore's CC write, visible to issues from
   restore_exec + lag on, is visible to an event executing at cycle E
   exactly when restore_exec < E; with lag = 1 the two forms coincide).
   A violating schedule refuses cc-restore-store-race.  The store must
   also retire before the next row's predicate definition executes, and
   the restore's visibility slot must not exceed the row initiation
   interval so the next row opens under the restored mask.  Every slot
   below is derived from the matched program's proven delays; synthesis
   refuses when any condition fails.  */
struct macro_cc_model
{
  bool active;
  bool complement;		/* template predicate sense is the
				   architectural complement of the
				   source's (the post-visibility load
				   carries the merge's live operand)   */
  int def_visible_slot;		/* first issue slot seeing the def     */
  int pre_load_slot;		/* payload load under the ambient mask */
  int post_load_slot;		/* payload load under the definition   */
  int store_exec_slot;		/* store execution cycle (reads the
				   live lane mask at that cycle)       */
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

/* Refusal names, used in dumps.  */
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
   predicate mask (store_lane_mask_live_at_execution).  The 4-slot
   select calendar that keeps the separator violates this.  */
extern const char *macro_desc_refusal_cc_restore_store_race;

/* Synthesize REGION/SCHEDULE into OUT.  Returns false when synthesis
   could not begin (no capability table); OUT->refusal names any other
   failed obligation.  Never mutates the function.  */
extern bool rvtt_macro_synthesize (const macro_region &region,
				   const macro_schedule &schedule,
				   macro_descriptor *out, FILE *dump);
extern void rvtt_macro_descriptor_release (macro_descriptor *);

/* Verify a synthesized descriptor against the region's explicit facts
   (rvtt-macro-verify.cc).  Runs under -mtt-tensix-macro-planner-verify
   and always when checking is enabled.  Returns null on success, else
   the failing component's tag; a non-null return is a descriptor
   refusal and must prevent formation.  */
extern const char *rvtt_macro_verify_descriptor
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, FILE *dump);

/* Build the verifier's expectations from the region's explicit facts
   (implemented beside synthesis; consumed by rvtt-macro-verify.cc).  */
namespace rvtt_macro_verify { struct expectations; }
/* Scheduler-facing derivation helpers (rvtt-macro-desc.cc).  */
extern int rvtt_macro_hosted_subunit (rtx_insn *);
extern unsigned rvtt_macro_store_only_sacrificial_vd (uint32_t internal_lregs);
extern bool rvtt_macro_derived_template_probe (rtx_insn *, int launch_vd,
					       uint8_t *opcode, uint8_t *mod1,
					       uint8_t *src_c, uint16_t *imm12);

extern bool rvtt_macro_build_expectations
  (const macro_region &region, const macro_schedule &schedule,
   rvtt_macro_verify::expectations *out);

/* Drain-aware boundary placement (rtl-rvtt-schedule.cc, used by the
   planner's emission under -mtt-tensix-optimize-drain-schedule): prove
   that the derived drain of the run ending before row END may be elided
   at the boundary into the next run [END, NEXT_END), because every
   in-flight macro event's writeback provably precedes the first
   conflicting follower access.  All distances derive from the
   descriptor's own SequenceBits delays; refusals are named to DUMP and
   keep the full derived drain.  */
extern bool rvtt_macro_drain_boundary_elidable
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, unsigned begin, unsigned end,
   unsigned next_end, FILE *dump);
/* Loop-backedge drain elision: prove that a loop-body region's final
   run may elide its in-body drain because the backedge follower stream
   -- the in-body tail, the loop-head prefix, and the region's own first
   run [0, FIRST_RUN_END) in the next iteration -- orders every access
   after every pending writeback.  The caller must emit the full derived
   drain on the loop's exit path, so the drain moves from once per trip
   to once per exit.  When EXIT_DRAIN is non-null it receives the decoded
   pending horizon less the slot credit of the in-body tail, which the
   exit path also executes: the fewest SFPNOPs the exit needs.  */
extern bool rvtt_macro_drain_backedge_elidable
  (const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, unsigned first_run_end, FILE *dump,
   int *exit_drain = nullptr);
/* Window-pairing inter-row drain tuning (rtl-rvtt-schedule.cc, used by
   the planner's emission under -mtt-tensix-optimize-window-pairing): the
   minimal proven inter-row drain for a run of uniform rows with a
   fixed-VD value carrier, from the exact pending-event versus
   follower-access model (register-, Dst- and sub-unit-aware) instead of
   the full derived drain.  Returns a value in [0, desc.drain_slots], or
   desc.drain_slots on any refusal, named to DUMP.  A result below the
   full drain names its binding conflict through *BOUND_NAME.  */
extern int rvtt_macro_interrow_drain_tuned
  (function *fn, const macro_region &region, const macro_schedule &schedule,
   const macro_descriptor &desc, FILE *dump, const char **bound_name);
/* ------------------------------------------------------------------ */
/* Descriptor-program residency (-mtt-tensix-macro-planner-residency,
   default off), after the dictionary compression of C. Lefurgy, P. Bird,
   I.-C. Chen and T. Mudge, "Improving code density using compression
   techniques", MICRO-30, 1997.  Derived descriptor words are keyed by
   content (the exact template/sequence/misc words) and become resident
   kernel-wide when the dominance and owned-state-invariance proofs hold,
   so identical descriptor programs are pushed once per kernel instead of
   once per region or per enclosing-loop trip.  Selection is
   first-formed-wins on content equality with no eviction: identical
   content never contends for the descriptor registers.  The delivery
   model (rvtt-cost.md, RISC_PUSH_X100) only prices the dump
   diagnostics.  The per-region ambient enable and owned SETC16 program
   are never elided.  Every refusal leaves the placement unchanged.  */

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
     retained prefixes, calendars); the residency walks treat them as
     benign.  */
  hash_set<rtx_insn *> emitted;
};

/* Refusal names, used in dumps.  */
extern const char *macro_resid_refusal_skip_path;  /* function not
						      owned-state clean  */
extern const char *macro_resid_refusal_span;	   /* dedupe span dirty  */
extern const char *macro_resid_refusal_dominance;  /* no dominating
						      resident program   */

/* Outward residency extension: repeat the configuration-epoch proof
   through successively enclosing loops from the already-proven placement
   in *HOIST_PREHEADER / *HOIST_EDGE, provided the whole function keeps
   the owned state invariant (paths that reach the resident program but
   not the region observe nothing: the enable re-asserts the all-lanes
   state and the owned-dest words are unread outside the region).  On
   success update the placement to the outermost proven level and return
   true; otherwise leave it untouched and return false.  Never mutates
   the function.  */
extern bool rvtt_macro_residency_extend (function *fn,
					 const macro_region &region,
					 const macro_descriptor &desc,
					 const rvtt_macro::caps *c,
					 macro_residency_state *state,
					 basic_block *hoist_preheader,
					 edge *hoist_edge,
					 unsigned *levels, FILE *dump);

/* Content-equality de-duplication: true when DESC's descriptor words
   equal an already-programmed entry whose placement dominates REGION's
   launch block and the whole function keeps the owned state invariant;
   the region may then omit its descriptor-word programming (the
   enable/SETC16 prefix stays).  Never mutates the function.  */
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
