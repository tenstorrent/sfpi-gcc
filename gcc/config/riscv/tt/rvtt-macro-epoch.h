/* Cross-tile configuration-epoch proof for the macro planner (WP11).
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

#ifndef GCC_RVTT_MACRO_EPOCH_H
#define GCC_RVTT_MACRO_EPOCH_H

#include "rvtt-macro-region.h"
#include "rvtt-macro-tables.h"

/* The cross-tile configuration epoch: a formed CC calendar's descriptor
   words (SFPCONFIG destinations) are re-programmed by the configuration
   prefix once per _calculate call -- once per tile -- although the
   programmed state provably survives from the previous tile of the SAME
   kernel.  When the region's configuration preheader itself sits inside
   an enclosing issue loop (the tile loop), and every instruction of
   that loop outside the region is proven a NON-OWNER of the planner's
   SFPCONFIG destinations, the descriptor-word part of the prefix may be
   hoisted to the enclosing loop's structural preheader: the epoch
   spanning every trip is configuration-clean, so tile N+1's launches
   read exactly the words tile 1 programmed.

   The proof discipline (refusing where invalidated):

   - every typed instruction's effect set must not read or write an
     owned SFPCONFIG destination, and must carry no foreign SFPU
     dataflow (LREG or CC effects outside the region);
   - every raw `.ttinsn' word (constant single-input asm, the LLK
     boilerplate form) must not be an SFPCONFIG to an owned destination
     (opcode and field layout from the capability tables);
   - every volatile store is treated as a potential RISC instruction
     push: its stored value must resolve -- constants, lui/addi chains,
     scc/shift/add compositions, and monotone self-loop inductions with
     a resolvable equality bound -- to a 32-bit interval whose opcode
     byte provably is not the SFPCONFIG opcode (or whose destination
     field provably is not owned); volatile loads deliver no words;
   - calls, unrecognized assembly, opaque Tensix issues, and
     unresolvable stored words refuse by name.

   Only the descriptor words hoist.  The ambient all-lanes enable and
   the owned SETC16 address-modifier program stay per-tile: the enable's
   lane state is re-established under the same materialization license,
   and the SETC16-visible address-modifier registers remain reachable
   from data-plane MMIO the value proof cannot bound.  */

/* Stable refusal names (append-only dump API).  */
extern const char *macro_epoch_refusal_invalidated;  /* intervening owner */
extern const char *macro_epoch_refusal_unproven;     /* unresolvable word */
extern const char *macro_epoch_refusal_preheader;    /* no structural
							outer preheader  */

/* Try to prove the cross-tile configuration epoch for REGION, whose
   configuration prefix would be placed in CONFIG_PREHEADER.  On success
   returns true and sets exactly one of:

   - *HOIST_PREHEADER: the enclosing loop's structural preheader (unique
     external entry whose source has a single successor) -- the
     descriptor-word insertion block;

   - *HOIST_EDGE: the enclosing loop's unique external entry edge, when
     its source block is shared with other control flow (the guarded
     tile loop).  The caller splits this edge AT COMMIT TIME ONLY (after
     every proof has passed), so refusal paths never mutate; the split
     block executes exactly when the loop is entered, which discharges
     the zero-trip obligation by construction.

   On a named refusal returns false with *REFUSAL set.  When no
   enclosing loop exists (nothing to elide) returns false with *REFUSAL
   null.  Never mutates the function.  */
extern bool rvtt_macro_prefix_epoch_hoist (function *fn,
					   const macro_region &region,
					   basic_block config_preheader,
					   const rvtt_macro::caps *c,
					   basic_block *hoist_preheader,
					   edge *hoist_edge,
					   const char **refusal,
					   rtx_insn **refusal_insn);

/* Residency-mode whole-function invariance walk (WP13; consumed by the
   descriptor-residency solver in rvtt-macro-desc.cc).  Proves that no
   instruction of FN outside BENIGN can change the values held by the
   planner's owned SFPCONFIG destinations or deliver an unresolvable /
   opaque word: foreign owned-dest WRITES, unresolvable raw or
   volatile-stored words, opaque Tensix issues, and calls refuse by the
   epoch names; foreign LREG/CC dataflow and foreign owned-dest READS
   are admitted (a residency placement change or content-equal elision
   is unobservable through them -- rationale at resid_insn_check).
   Returns null when clean, else the stable refusal name with
   *REFUSAL_INSN set.  Never mutates the function.  */
extern const char *
rvtt_macro_epoch_owned_state_invariant_p (function *fn,
					  hash_set<rtx_insn *> &benign,
					  const rvtt_macro::caps *c,
					  rtx_insn **refusal_insn);

#endif /* GCC_RVTT_MACRO_EPOCH_H */
