/* Typed architectural effect sets for Tensix instructions (Layer 1).
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

#ifndef GCC_RVTT_EFFECTS_H
#define GCC_RVTT_EFFECTS_H

/* This is the ONLY vocabulary macro-planner layers may use to classify
   instructions.  Values are derived from the generated effect attribute
   family in rvtt-cost.md (all of whose defaults are refusing), resolved
   against instruction operands after the attribute class has admitted the
   instruction.  recog-code comparisons are permitted solely to reach an
   admitted instruction's operands -- never as region identity.  */

enum xtt_subunit_t { XTT_SU_NONE, XTT_SU_SIMPLE, XTT_SU_MAD, XTT_SU_ROUND,
		     XTT_SU_LOAD, XTT_SU_STORE, XTT_SU_CFG, XTT_SU_SYNC };

struct xtt_rwc_effect_t {
  enum kind_t { NONE, INC, SET, FACE, UNKNOWN } kind;
  int dst_delta;		/* meaningful for INC; FACE == one face step   */
  int cr_delta;			/* counter-register semantics, typed operands  */
  unsigned set_mask;		/* for SET (TTSETRWC)                          */
};

struct xtt_effect_set {
  uint32_t lreg_read, lreg_write; /* hard-reg mask over L0..L15/LREG16	       */
  bool	   cc_read, cc_write;
  bool	   cc_write_all_lanes;	  /* cc_write provably writes the all-lanes
				     enabled state (word-exact against the
				     capability table's architectural
				     all-lanes SFPENCC encoding); refusing
				     default false -- any other CC write is
				     lane-state-unproved		       */
  uint32_t config_dests_written;  /* bitmask over SFPCONFIG dests 0..15	       */
  uint32_t config_dests_read;	  /* bitmask over SFPCONFIG dests 0..15	       */
  bool	   addr_mod_slot_write;	  /* SETC16 into an address-mod slot reg       */
  xtt_rwc_effect_t rwc;
  bool	   dst_mem_read, dst_mem_write;	  /* from MEM operands (existing)      */
  int	   result_latency;	  /* from xtt_result_latency		       */
  xtt_subunit_t subunit;
  bool	   opaque;		  /* CALL_P, unclassified asm, unknown	       */
};

/* Attribute-driven; opaque=true default.  */
extern xtt_effect_set rvtt_insn_effects (rtx_insn *);

/* Gimple-level access for builtin calls: the subunit of the late RTL
   pattern a builtin resolves to.  Returns XTT_SU_NONE (the refusing
   default) for unaudited builtins.  */
struct rvtt_insn_data;
extern xtt_subunit_t rvtt_builtin_subunit (const rvtt_insn_data *);

/* Annotate FILE with INSN's effect set (under -mtt-tensix-dump-effects).  */
extern void rvtt_dump_insn_effects (FILE *, rtx_insn *);

/* Post-admission operand access for a Dst load/store (EFFECTS must have
   dst_mem_read or dst_mem_write): the typed address, data-mode, and
   address-mode operands.  Returns false for insns whose Dst operand
   layout is not on record.  */
extern bool rvtt_dst_access_operands (rtx_insn *, const xtt_effect_set &,
				      rtx *address, rtx *mode,
				      rtx *addr_mode);

/* ---- Multi-result / shadow-coupled effect structure (TOP3-2 layer 1/2).

   A multi-result instruction is one whose single architectural event
   defines both a value-bank result set (L0-L3) and a companion result
   set (L4-L7) in one PARALLEL: the indexed SFPSWAP (value pair plus
   companion pair at value+4, per SFPSWAP.md's ENABLE_DEST_INDEX leg) and
   the eight-definition SFPTRANSP (both Transpose4 banks).  The masks are
   hard-LREG masks in the same domain as xtt_effect_set::lreg_write.
   Post-admission operand access: EFFECTS must be non-opaque.  Returns
   false for instructions with no companion structure on record.  */

struct xtt_multiresult_group {
  uint32_t value_write_mask;
  uint32_t companion_write_mask;
};

extern bool rvtt_multiresult_group (rtx_insn *, const xtt_effect_set &,
				    xtt_multiresult_group *);

/* Zero-length architectural LREG interface marker (the typed
   variable-LREG read/write patterns): sets *LREG_MASK to the pinned hard
   LREG.  These markers deliver no instruction word but carry the
   dataflow connection of fixed-LREG protocols; a capture boundary must
   not separate them from the multi-result instruction they describe.  */
extern bool rvtt_lreg_marker (rtx_insn *, uint32_t *lreg_mask);

/* Recording-epoch closure proof (ownership-epoch model for the replay
   buffer's recording state).  A user fixed capture of PAYLOAD_WORDS
   words opens an epoch; the epoch closes when the typed instruction
   lengths of the following slot-occupying Tensix instructions account
   for exactly the declared words.  Anything the typed stream cannot
   account for refuses, by name:

     OPAQUE_PAYLOAD        - opaque asm, a call, an unrecognized insn, or
			     a declared length that would split one
			     instruction's words (blocker = the insn);
     CROSSES_BLOCK         - the block ends while the epoch is open;
     OWNER_DURING_CAPTURE  - an explicit replay-owner instruction inside
			     the open epoch (broken user code).

   On CLOSED, close_at is the last payload instruction extended across
   any immediately following zero-length LREG interface markers (the
   companion-group protocol above), and multiresult_members counts the
   payload's multi-result instructions -- the capture retains them.  */

struct xtt_replay_epoch {
  enum status_t { CLOSED, OPAQUE_PAYLOAD, CROSSES_BLOCK,
		  OWNER_DURING_CAPTURE } status;
  rtx_insn *close_at;
  rtx_insn *blocker;
  unsigned multiresult_members;
};

extern xtt_replay_epoch rvtt_replay_epoch_close (rtx_insn *capture,
						 unsigned payload_words);

/* Whether index-tracking shadow coupling (LaneConfig.ENABLE_DEST_INDEX,
   written through SFPCONFIG destination 15) is possibly enabled anywhere
   in FN: true when the function contains a typed multi-result
   instruction (its semantics are the coupling) or any typed
   configuration write that can reach destination 15.  The programming
   model's default state is disabled; a raw-TTI enable is invisible to
   the typed stream and remains a documented layer-2 gap (a later
   increment types those words).  Lossy by design: there is no proven-off
   transition, so one enable is function-sticky.  */
extern bool rvtt_shadow_coupling_possible (function *);

#endif /* GCC_RVTT_EFFECTS_H */
