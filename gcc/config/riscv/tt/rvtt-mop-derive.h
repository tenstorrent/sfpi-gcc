/* MOP template-effect derivation for the TU-wide PRGM freedom
   proof.  -*- C++ -*-
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

/* Facts accumulated while the TU-wide PRGM freedom scan walks every
   function body.  A MOP word (frontend opcode 0x01) has no effects of
   its own: it expands the words programmed into the nine MOP template
   registers.  Every template-slot write in the TU is checked against
   the same raw-word table as directly delivered words, and a MOP word
   is accepted only when no template word programmable in this TU can
   touch PRGM, LaneConfig or CC state (rvtt_mop_derive_finish).  The
   tables are in rvtt-mop-tables.h.  */

struct rvtt_mop_derive_state
{
  /* A MOP word is delivered somewhere in the TU (raw `.ttinsn' or a
     classified instruction-FIFO push).  */
  bool mop_pushed = false;
  /* Some mop_cfg instruction-slot write was refused; accepting a MOP
     word then fails with SLOT_REASON.  */
  bool slots_refused = false;
  char slot_reason[192];

  /* The TU-wide CC/lane-enable check uses the same scan.  Every
     refusal marks the TU CC-dirty unless the refused statement is a
     canonical raw `.ttinsn' word that preserves the all-lanes-enabled
     state (rvtt_raw_cc_word_ambient_preserving_p); the PRGM check can
     refuse such words, e.g. an SFPLOADI with a non-allocatable
     destination.  In a clean TU the lane-enable analysis may look
     through the opaque statements (stores, MOP expansions, scalar asm)
     the scan accepted.  */
  bool cc_dirty = false;
  char cc_reason[192];

  rvtt_mop_derive_state () { slot_reason[0] = 0; cc_reason[0] = 0; }
};

/* Return true if raw instruction WORD (BH/WH encoding) has a known
   effect on PRGM, LaneConfig and CC state; otherwise set *WHY and
   return false.  A decoded SFPCONFIG records its destination in
   *CLAIMED.  When ST is non-null, MOP words are accepted provisionally
   (checked by rvtt_mop_derive_finish) and MOP_CFG words
   unconditionally; with ST null both are refused.  IN_SLOT applies the
   template-slot rules, which refuse REPLAY and nested MOP/MOP_CFG.
   Defined in rvtt-raw-boundary.cc on top of
   rvtt_word_facts_classify.  */
extern bool rvtt_mop_audited_word_p (uint32_t word, unsigned *claimed,
				     const char **why,
				     rvtt_mop_derive_state *st,
				     bool in_slot = false);

/* Context-bound scanning.  The TU walk scans the bodies of callees
   that will be inlined later, before the inlining has happened.  Such
   bodies read their parameters and may load template words back out
   of an aggregate in the caller's frame.  The scan context records
   what is needed to classify them at the call site:

   - parameter bindings: the actual arguments of the call, each with
     the context it must be read in, so bindings chain through nested
     scans;
   - a field scan of the whole call subtree: every store to a field of
     an automatic aggregate, and whether the stored value is an
     accepted word.  If the aggregate's address escapes the subtree it
     is poisoned.

   A template word loaded from such a field is accepted when every
   store to that field was accepted and the object is not poisoned.
   This is flow-insensitive.  Anything unresolvable is refused.  */

struct rvtt_mop_scan_ctx;

struct rvtt_mop_bound_arg
{
  tree value;
  rvtt_mop_scan_ctx *ctx;	/* context the value must be read under */
};

struct rvtt_mop_obj_scan
{
  struct entry { tree var; tree field; bool ok; };
  auto_vec<entry, 16> fields;
  hash_set<tree> poisoned;
};

struct rvtt_mop_scan_ctx
{
  rvtt_mop_obj_scan *scan;			/* shared per subtree */
  hash_map<tree, rvtt_mop_bound_arg> *parms;	/* null at a root scan */
};

/* Poison VAR's scan entries: its address escaped the scanned
   subtree, so unaudited stores can no longer be excluded.  */
extern void rvtt_mop_scan_poison (rvtt_mop_scan_ctx *ctx, tree var);

/* Resolve VAL through the PARM-binding chain of CTX (bounded); returns
   the final value and updates *CTX_IO to the context it is read
   under.  */
extern tree rvtt_mop_resolve_bound (tree val, rvtt_mop_scan_ctx **ctx_io);

/* Classify STMT if it stores to memory.  Return true when the store
   cannot affect PRGM state (template-slot writes are recorded in ST;
   instruction-FIFO pushes classify their word), false with *WHY
   otherwise.  Non-stores return true.  CTX, if non-null, enables the
   context-bound classification above and records field stores into
   scanned aggregates.  */
extern bool rvtt_mop_derive_store (gimple *stmt, unsigned *claimed,
				   const char **why,
				   rvtt_mop_derive_state *st,
				   rvtt_mop_scan_ctx *ctx = nullptr);

/* Recognize and classify the canonical scalar blocking-store asm
   idiom (store, reload, consume), which classifies like a plain
   store.  */
extern bool rvtt_mop_blocking_store_asm_p (const gasm *stmt);
extern bool rvtt_mop_derive_asm_store (const gasm *stmt, unsigned *claimed,
				       const char **why,
				       rvtt_mop_derive_state *st);

/* Return true if the indirect call CALL is the C-runtime init-array
   walk, calling only this TU's own (scanned) static constructors.
   See rvtt-mop-derive.cc for the conditions checked.  */
extern bool rvtt_mop_init_array_call_p (gcall *call);

/* After the whole-TU scan, return false with *WHY if a MOP word is
   pushed while some template-slot write was refused.  */
extern bool rvtt_mop_derive_finish (const rvtt_mop_derive_state *st,
				    const char **why);

/* Raw REPLAY record regions (-mtt-tensix-optimize-opaque-replay-record).  */

/* Parse STMT as the canonical raw `.ttinsn' asm with one constant
   input; on success store the 32-bit instruction word in *WORD.  */
extern bool rvtt_raw_ttinsn_word_p (gasm *stmt, uint32_t *word);

#endif /* GCC_RVTT_MOP_DERIVE_H */
