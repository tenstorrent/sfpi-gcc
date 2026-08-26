/* MOP template-effect derivation for the TU-wide PRGM freedom proof.  -*- C++ -*-
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

#ifndef GCC_RVTT_MOP_DERIVE_H
#define GCC_RVTT_MOP_DERIVE_H

/* TU-scope facts the derivation accumulates while the prgm-const pass
   scans every function body.  A MOP word (frontend opcode 0x01) has no
   effects of its own: it expands the instruction words previously
   programmed into the nine MOP template registers.  The derivation
   audits every template-slot WRITE in the TU through the same audited
   raw-word table as directly delivered words; the MOP word is then
   admitted exactly when no template word programmable in this TU can
   touch PRGM/LaneConfig/CC state unaudited (rvtt_mop_derive_finish).
   Facts and provenance: rvtt-mop-tables.h.  */

struct rvtt_mop_derive_state
{
  /* A MOP word is delivered somewhere in the TU (raw `.ttinsn' or a
     classified instruction-FIFO push).  */
  bool mop_pushed = false;
  /* Some mop_cfg instruction-slot write refused the audit; the MOP
     admission above then fails with SLOT_REASON.  Claims from audited
     slot words accumulate regardless.  */
  bool slots_refused = false;
  char slot_reason[192];

  rvtt_mop_derive_state () { slot_reason[0] = 0; }
};

/* The audited raw-word capability table (BH/WH encodings), shared by
   the raw `.ttinsn' census, the instruction-FIFO push census, and the
   template-slot audit.  Returns false for any word whose
   PRGM/LaneConfig/CC effect is not architecturally pinned; a decoded
   SFPCONFIG claims its destination in *CLAIMED.  When ST is non-null,
   MOP words are admitted provisionally (recorded for the finish
   adjudication) and MOP_CFG words unconditionally; with ST null both
   refuse as before.  IN_SLOT selects the template-slot discipline
   (REPLAY and nested MOP/MOP_CFG refuse by name).  */
extern bool rvtt_mop_audited_word_p (uint32_t word, unsigned *claimed,
				     const char **why,
				     rvtt_mop_derive_state *st,
				     bool in_slot = false);

/* ---- Context-bound scanning (lane CF) ----

   The TU walk scans callee bodies on demand for calls whose callee
   will be inlined later (the eager scan runs before the callers'
   inline transforms materialize).  Those generic bodies read their
   parameters and -- in the ckernel_template discipline -- load
   template words back out of a caller-frame aggregate the caller's
   own (already specialized) code stored.  The scan context carries
   the two facts that make such a body classifiable AT ITS CALL SITE:

   - PARM bindings: the actual argument trees of the driving call
     (each with the context it must be read under, so bindings chain
     through nested on-demand scans);
   - the local-aggregate FIELD census of the whole subtree under one
     top-level scanned function: every store to a field of an
     automatic aggregate is recorded with whether its stored value
     classifies through the audited word table; any escape of the
     aggregate's address outside the scanned subtree poisons it.

   A template-slot word loaded from such a field is then admissible
   exactly when the census proves every store to that field audited
   and the object unpoisoned -- the same union-over-programmings
   discipline the slot taxonomy itself uses (flow-insensitive: WHICH
   store reached the load never matters, only that no unaudited one
   exists).  Everything unresolvable keeps the refusing default.  */

struct rvtt_mop_scan_ctx;

struct rvtt_mop_bound_arg
{
  tree value;
  rvtt_mop_scan_ctx *ctx;	/* context the value must be read under */
};

struct rvtt_mop_obj_census
{
  struct entry { tree var; tree field; bool ok; };
  auto_vec<entry, 16> fields;
  hash_set<tree> poisoned;
};

struct rvtt_mop_scan_ctx
{
  rvtt_mop_obj_census *census;			/* shared per subtree */
  hash_map<tree, rvtt_mop_bound_arg> *parms;	/* null at a root scan */
};

/* Poison VAR's census entries: its address escaped the scanned
   subtree, so unaudited stores can no longer be excluded.  */
extern void rvtt_mop_census_poison (rvtt_mop_scan_ctx *ctx, tree var);

/* Resolve VAL through the PARM-binding chain of CTX (bounded); returns
   the final value and updates *CTX_IO to the context it is read
   under.  */
extern tree rvtt_mop_resolve_bound (tree val, rvtt_mop_scan_ctx **ctx_io);

/* Classify one gimple statement that stores to memory.  Returns true
   when the store is proven inert for the TU freedom proof (template
   slot writes are audited into ST; instruction-FIFO pushes classify
   their word); false with *WHY on refusal.  Statements that are not
   stores return true untouched.  CTX (nullable) enables the
   context-bound classification above; field stores into censused
   aggregates are additionally recorded there.  */
extern bool rvtt_mop_derive_store (gimple *stmt, unsigned *claimed,
				   const char **why,
				   rvtt_mop_derive_state *st,
				   rvtt_mop_scan_ctx *ctx = nullptr);

/* Recognize / classify the canonical scalar blocking-store asm idiom
   (store, reload, consume), which stores its value operand at its
   address operand and therefore classifies like any other store.  */
extern bool rvtt_mop_blocking_store_asm_p (const gasm *stmt);
extern bool rvtt_mop_derive_asm_store (const gasm *stmt, unsigned *claimed,
				       const char **why,
				       rvtt_mop_derive_state *st);

/* Prove that the indirect call CALL is the C-runtime init-array walk
   calling only this translation unit's own registered (and therefore
   scanned) static constructors.  See the file comment in
   rvtt-mop-derive.cc for the proof obligations.  */
extern bool rvtt_mop_init_array_call_p (gcall *call);

/* Adjudicate the deferred MOP admission after the whole-TU scan:
   refuses (false, *WHY) when a MOP is pushed while some template slot
   write is unaudited.  */
extern bool rvtt_mop_derive_finish (const rvtt_mop_derive_state *st,
				    const char **why);

/* ---- Raw REPLAY record regions (lane HS;
   -mtt-tensix-optimize-opaque-replay-record) ----

   Parse STMT as the canonical raw `.ttinsn' asm with one constant
   input; on success *WORD receives the 32-bit instruction word.  */
extern bool rvtt_raw_ttinsn_word_p (gasm *stmt, uint32_t *word);

/* Adjudicate one raw REPLAY word (opcode XTT_REPLAY_OPCODE) for the TU
   freedom proof.  On admission returns true; a load_mode=1/exec=0
   record additionally proves its swallowed-word region and adds every
   swallowed raw-word statement to *SUPPRESSED (those statements are
   architecturally never delivered and must be EXCLUDED from the
   executed-word census).  Refuses (false, *WHY) by name for playback
   words, malformed encodings, and every unproven region shape.  The
   theorem and its obligations: rvtt-mop-derive.cc file section.  */
extern bool rvtt_mop_replay_record_admit (gasm *record, uint32_t word,
					  hash_set<gimple *> *suppressed,
					  const char **why);

#endif /* GCC_RVTT_MOP_DERIVE_H */
