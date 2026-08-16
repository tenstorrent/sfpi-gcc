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

#endif /* GCC_RVTT_EFFECTS_H */
