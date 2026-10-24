/* Cross-tile configuration-epoch proof for the macro planner.
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

/* Cross-tile configuration epoch.  The configuration prefix of a formed
   CC calendar re-programs its SFPCONFIG descriptor words on every
   _calculate call (once per tile), although the state survives from the
   previous tile of the same kernel.  When the configuration preheader
   sits inside an enclosing loop and every instruction of that loop
   outside the region is proven not to touch the planner's SFPCONFIG
   destinations, the descriptor writes can be hoisted to the enclosing
   loop's preheader.

   An instruction is accepted only if:
   - it is typed, and its effect set neither reads nor writes an owned
     SFPCONFIG destination nor carries LREG or CC effects outside the
     region;
   - it is a raw `.ttinsn' word that is not an SFPCONFIG to an owned
     destination (decoded with the capability tables);
   - it is a volatile store (a possible instruction push) whose value
     resolves to a 32-bit interval whose opcode byte is not SFPCONFIG or
     whose destination field is not owned.  Volatile loads deliver no
     words.
   Calls, unrecognized asm, opaque Tensix issues and unresolvable words
   refuse.

   Only the descriptor words are hoisted.  The all-lanes enable and the
   SETC16 address-modifier program stay per tile, because the
   address-modifier registers are reachable through MMIO that the value
   proof cannot bound.  */

/* Refusal names.  */
extern const char *macro_epoch_refusal_invalidated;  /* intervening owner */
extern const char *macro_epoch_refusal_unproven;     /* unresolvable word */
extern const char *macro_epoch_refusal_preheader;    /* no structural
							outer preheader  */

/* Try to prove the cross-tile configuration epoch for REGION, whose
   configuration prefix would be placed in CONFIG_PREHEADER, using the
   capability tables C.  On success return true and set exactly one of:

   - *HOIST_PREHEADER: the enclosing loop's preheader (its unique
     external entry, whose source has a single successor);

   - *HOIST_EDGE: the enclosing loop's unique entry edge, when its source
     has other successors (a guarded tile loop).  The caller splits it
     only after all proofs pass; the new block runs exactly when the loop
     is entered, so a zero-trip loop is handled.

   On refusal return false with *REFUSAL and *REFUSAL_INSN set.  With no
   enclosing loop return false with *REFUSAL null.  FN is not
   modified.  */
extern bool rvtt_macro_prefix_epoch_hoist (function *fn,
					   const macro_region &region,
					   basic_block config_preheader,
					   const rvtt_macro::caps *c,
					   basic_block *hoist_preheader,
					   edge *hoist_edge,
					   const char **refusal,
					   rtx_insn **refusal_insn);

/* Return null if no instruction of FN outside BENIGN can change the
   values in the planner's owned SFPCONFIG destinations or deliver an
   unresolvable or opaque word; otherwise return the refusal name and
   set *REFUSAL_INSN.  Used by the descriptor-residency solver in
   rvtt-macro-desc.cc.  Unlike the epoch walk, foreign LREG/CC dataflow
   and reads of owned destinations are accepted, since moving or eliding
   a content-equal write is not observable through them (see
   resid_insn_check).  FN is not modified.  */
extern const char *
rvtt_macro_epoch_owned_state_invariant_p (function *fn,
					  hash_set<rtx_insn *> &benign,
					  const rvtt_macro::caps *c,
					  rtx_insn **refusal_insn);

/* Return null if no instruction in the loop body BODY[0..NBBS), with
   header LOOP_HEADER, can modify the Replay Expander buffers between a
   preheader record and a playback launch in any iteration; otherwise
   return a refusal detail and set *REFUSAL_INSN.  Uses the same word
   interval resolution as the epoch walk:

   - TTREPLAY playback launches in PASS_LAUNCHES (formed by the replay
     pass) only read recorded slots and are accepted; other replay
     instructions refuse;
   - MOP dispatches are accepted only if every MopCfg[0..8] word the
     expansion can read (ISA MOPExpander.md) is a constant stored before
     the loop header whose opcode byte is not REPLAY, and FN contains no
     call that could re-arm MopCfg;
   - raw `.ttinsn' words and volatile stores that may hit the
     instruction FIFO must resolve to words that are not REPLAY (constant
     playback launches excepted), and are MOPs only under the rule above;
   - calls, unrecognized asm and unresolvable words refuse.

   FN is not modified.  */
extern const char *
rvtt_macro_epoch_loop_replay_preserved_p (function *fn, basic_block *body,
					  unsigned nbbs,
					  basic_block loop_header,
					  hash_set<rtx_insn *> &pass_launches,
					  rtx_insn **refusal_insn);

#endif /* GCC_RVTT_MACRO_EPOCH_H */
