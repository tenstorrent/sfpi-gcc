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
  uint32_t planned_lregs;	/* planner-owned physical LREG mask    */
  const char *refusal;		/* stable name; null = synthesized     */
};

/* Stable refusal names (append-only dump API).  */
extern const char *macro_desc_refusal_program_unproven;
extern const char *macro_desc_refusal_encoding_failed;
extern const char *macro_desc_refusal_verification_failed;

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
