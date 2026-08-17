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
extern bool rvtt_macro_build_expectations
  (const macro_region &region, const macro_schedule &schedule,
   rvtt_macro_verify::expectations *out);

#endif /* GCC_RVTT_MACRO_DESC_H */
