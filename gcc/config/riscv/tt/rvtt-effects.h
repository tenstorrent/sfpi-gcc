/* Typed architectural effect sets for Tensix instructions.
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

/* For rvtt_insn_data and its insn_id enum.  rvtt.h does not include
   this header.  */
#include "rvtt.h"

/* Architectural effect sets of Tensix instructions.  Passes that need
   to know what an instruction touches use these interfaces instead of
   matching recog codes; recog codes are only used to reach the
   operands of an instruction already classified.  The values come
   from the effect attributes in rvtt-cost.md, whose defaults are all
   conservative, refined by the instruction's operands.  */

enum xtt_subunit_t { XTT_SU_NONE, XTT_SU_SIMPLE, XTT_SU_MAD, XTT_SU_ROUND,
		     XTT_SU_LOAD, XTT_SU_STORE, XTT_SU_CFG, XTT_SU_SYNC };

struct xtt_rwc_effect_t {
  enum kind_t { NONE, INC, SET, FACE, UNKNOWN } kind;
  int dst_delta;		/* meaningful for INC; FACE == one face step */
  int cr_delta;			/* counter-register semantics, typed operands */
  unsigned set_mask;		/* for SET (TTSETRWC) */
};

struct xtt_effect_set {
  uint32_t lreg_read, lreg_write; /* hard-reg mask over L0..L15/LREG16 */
  bool	   cc_read, cc_write;
  bool	   cc_write_all_lanes;	  /* cc_write is known to enable all
				     lanes (the exact all-lanes SFPENCC
				     encoding); false for any other CC
				     write */
  uint32_t config_dests_written;  /* bitmask over SFPCONFIG dests 0..15 */
  uint32_t config_dests_read;	  /* bitmask over SFPCONFIG dests 0..15 */
  bool	   addr_mod_slot_write;	  /* SETC16 into an address-mod slot reg */
  xtt_rwc_effect_t rwc;
  bool	   dst_mem_read, dst_mem_write;	  /* from MEM operands */
  int	   result_latency;	  /* from xtt_result_latency */
  bool	   next_slot_stall;	  /* architectural next-slot acceptance
				     stall (xtt_next_slot_stall) */
  bool	   prng_advance;	  /* architectural per-lane PRNG state advance */
  xtt_subunit_t subunit;
  bool	   opaque;		  /* CALL_P, unclassified asm, unknown */
};

/* Return the effect set of an insn, from its attributes; opaque by
   default.  */
extern xtt_effect_set rvtt_insn_effects (rtx_insn *);
/* As rvtt_insn_effects, but PRNG-advancing words are typed rather than
   opaque.  Only macro formation, which preserves PRNG order, uses
   this.  */
extern xtt_effect_set rvtt_macro_insn_effects (rtx_insn *);

/* The target's no-increment load/store address mode, or -1 if
   unknown.  */
extern int rvtt_no_increment_address_mode ();

/* Return the subunit of the late RTL pattern a builtin resolves to,
   or XTT_SU_NONE for builtins not listed.  */
struct rvtt_insn_data;
extern xtt_subunit_t rvtt_builtin_subunit (const rvtt_insn_data *);

/* Return the result latency of the late RTL pattern a builtin resolves
   to: the `xtt_result_latency' attribute minus its +1 bias, so 0 means
   same-slot chaining and -1 means unknown (also returned for builtins
   not listed).  Only patterns with an operand-independent latency are
   listed.  The timing model using this value is in rvtt-timing.h.  */
extern int rvtt_builtin_result_latency (const rvtt_insn_data *);

/* Builtin-call classification shared by the GIMPLE passes.  */

/* Read argument ARGNO of CALL into *OUT when it is an INTEGER_CST that
   fits an unsigned HWI.  Returns false, leaving *OUT untouched, for a
   missing or non-constant argument.  */
extern bool rvtt_call_const_uarg (const gcall *call, unsigned argno,
				  unsigned *out);

/* True for an sfpencc whose (mod1, imm12) operands encode exactly the
   all-lanes-enable word.  The check uses the macro table's encoding
   rather than the builtin's name.  */
extern bool rvtt_encc_all_lanes_call_p (const gcall *call,
					const rvtt_insn_data *insnd);

/* True for builtin ids that write the CC / lane-enable state.  An
   all-lanes sfpencc must be recognized separately, before this
   predicate is applied.  */
extern bool rvtt_cc_writer_id_p (rvtt_insn_data::insn_id id);

/* True for builtins with no Dst-memory access, no RWC effect, no
   configuration write and no implicit fixed-LREG operand.  Such a
   builtin may still write CC through its mod operand; callers that
   care check insnd->sets_cc on the call.  */
extern bool rvtt_dst_inert_compute_id_p (rvtt_insn_data::insn_id id);

/* True for builtins that neither write the lane-enable CC state nor
   have hidden raw effects, but may touch Dst memory or move data
   between lanes: a superset of rvtt_dst_inert_compute_id_p.  Use this
   for CC-window and lane-state scans; use rvtt_dst_inert_compute_id_p
   when Dst contents must also be left untouched.  */
extern bool rvtt_cc_quiet_compute_id_p (rvtt_insn_data::insn_id id);

/* LaneEnabled depends independently on column flags and LaneConfig.  An
   SFPENCC reset establishes only FLAGS_ALL; ROW_MASK_CLEAR needs an exact
   zero LaneConfig write while every column flag is enabled.  Neither fact
   is part of the function-entry ABI.  Keep this proof shared by GIMPLE
   consumers rather than treating the name "all_lanes" as a hardware reset.
   Facts are must-facts: CFG joins intersect, opaque effects drop them, and
   replay/runtime issuance cannot establish them.  Rebuild after mutation.  */
enum rvtt_lane_fact
{
  RVTT_LANE_FLAGS_ALL = 1,
  RVTT_LANE_ROW_MASK_CLEAR = 2,
  RVTT_LANE_ALL = 3
};

class rvtt_gimple_lane_facts
{
public:
  /* Nonnegative contracts are for conditional body-summary construction
     only, never a function-entry ABI assertion at an optimization site.  */
  explicit rvtt_gimple_lane_facts (function *, int = -1, int = -1);
  unsigned before (gimple *) const;
  unsigned after (gimple *) const;
  unsigned block_in (basic_block) const;
  unsigned block_out (basic_block bb) const
  {
    return bb == ENTRY_BLOCK_PTR_FOR_FN (m_fn) ? m_entry : m_out[bb->index];
  }
private:
  unsigned transfer (gimple *, unsigned) const;
  function *m_fn;
  unsigned m_entry;
  bool m_direct_issue;
  bool m_mop_initialized;
  auto_vec<unsigned> m_out;
};

extern bool rvtt_gimple_all_lanes_after_p (gimple *);
/* Retain a parameter-independent no-recorder body digest before expansion;
   unlike lane facts it says nothing about the callee's incoming/outgoing
   flags or ROW_MASK.  Opaque/recursive bodies never supply a positive.  */
extern bool rvtt_gimple_direct_issue_p (function *);
extern void rvtt_gimple_direct_issue_invalidate (function *);
/* Retained callee-body contracts for RTL.  No new source scan is started;
   a still-mutable GIMPLE body cannot supply this frozen contract.  FULL
   selects the conditional FULL-to-FULL summary, not an incoming ABI.  */
extern bool rvtt_callee_direct_issue_p (tree);
extern bool rvtt_callee_preserves_lanes_p (tree, bool);
/* Existing complete TU template audit, consulted without starting a scan.
   True only for an audited closure member whose MOP templates have no
   lane-state writes, nested launch, or recorder/playback instruction.  */
extern bool rvtt_gimple_mop_preserves_lanes_p (function *);
/* Current-body MUST proof only: every actual launch has all nine safe
   template slots initialized on every incoming path.  No MAY hook is
   consulted, so complete-TU producers can validate each audited body
   without recursively granting their own still-incomplete permission.  */
extern bool rvtt_gimple_mop_initialized_p (function *);
/* Narrow current-template value contract, shared by GIMPLE and RTL.  */
extern bool rvtt_mop_lane_slot_word_p (unsigned, uint32_t);
/* Registered by the later TU-template analysis.  Before it exists or
   computes a complete audit, the common substrate must fail closed.  */
extern bool (*rvtt_mop_lane_proof_hook) (function *);
/* Separate complete-TU proofs: absence of any recorder, and preservation
   by opaque delivery channels.  Neither supplies an incoming lane ABI.  */
extern bool (*rvtt_tu_no_recorder_hook) (function *);
extern bool (*rvtt_tu_opaque_lane_hook) (function *);
/* Current-position record audit: -1 unknown, 0 swallowed without
   execution, 1 delivered outside all fully owned capture windows.  */
extern int (*rvtt_tu_stmt_issue_hook) (function *, gimple *);
/* Conservative unsigned 32-bit bounds for a delivered instruction word.
   Unknown operands span all bits; arithmetic overflow is never assumed
   absent merely because the source resembles a TT_OP macro.  */
extern bool rvtt_gimple_word_bounds (tree, uint32_t *, uint32_t *, unsigned = 0);

/* Argument N of CALL as a host integer, or -1 when it is not a literal
   INTEGER_CST.  */
extern long rvtt_call_int_arg (const gcall *call, unsigned n);

/* STMT as a gcall when it is the rvtt builtin call with insn identity
   ID, null otherwise.  */
extern gcall *rvtt_call_with_id (gimple *stmt, rvtt_insn_data::insn_id id);

/* True if INSN's pattern has no unspec_volatile, memory store, call or
   asm, so it cannot reach Dst, the RWC counters, configuration state
   or CC.  */
extern bool rvtt_pattern_transparent_p (rtx_insn *insn);

/* True when VAL is an all-zero vector: a read of the constant-zero
   register, or an immediate load of 0.  */
extern bool rvtt_zero_vector_p (tree val);

/* The ABI symbol for the Tensix instruction FIFO.  Recognize it
   through the predicates below.  */
#define RVTT_INSTRN_BUFFER_SYMBOL "__instrn_buffer"

/* Whether NAME (an assembler or source symbol name, possibly null) is
   the instruction-FIFO symbol.  */
extern bool rvtt_instrn_buffer_name_p (const char *name);

/* Whether ADDR is a canonical instruction-buffer operand of the SFPU
   builtins: either a literal zero (used by direct builtin tests; the
   operand has no SFPLOADI semantics) or the address of the external
   __instrn_buffer declaration.  Any other buffer is rejected.  */
extern bool rvtt_canonical_buffer_arg_p (tree addr);

/* Annotate FILE with INSN's effect set (under -mtt-tensix-dump-effects).  */
extern void rvtt_dump_insn_effects (FILE *, rtx_insn *);

/* Return true if INSN is an SFPU LREG value operation with no
   Dst-memory, RWC-counter or configuration effect (the
   xtt_lane_local/xtt_cc_write attributes in rvtt.md, per insn code
   regardless of mod).  *CC_WRITES is then set if the operation's mod
   field can set CC, whatever the actual mod value.  Unannotated
   patterns return false.  Kept separate from rvtt_insn_effects.  */
extern bool rvtt_lane_local_effects (rtx_insn *, bool *cc_writes);

/* Return true if INSN's LREG/Dst writes are lane-gated and its dataflow
   is lane-local (the xtt_lane_gated attribute).  The CC-gated
   predicated-assign copy is not covered; consumers recognize it
   structurally.  */
extern bool rvtt_lane_gated_consumer_p (rtx_insn *);

/* For a Dst load/store (EFFECTS has dst_mem_read or dst_mem_write),
   return its address, data-mode and address-mode operands.  Returns
   false for insns whose Dst operand layout is not known.  */
extern bool rvtt_dst_access_operands (rtx_insn *, const xtt_effect_set &,
				      rtx *address, rtx *mode,
				      rtx *addr_mode);

/* Multi-result instructions define, in one PARALLEL, both a value
   result set (L0-L3) and a companion result set (L4-L7): the indexed
   SFPSWAP (value pair plus companion pair at value+4, per SFPSWAP.md's
   ENABLE_DEST_INDEX case) and the eight-result SFPTRANSP (both
   Transpose4 banks).  The masks are hard-LREG masks as in
   xtt_effect_set::lreg_write.  EFFECTS must be non-opaque.  Returns
   false for instructions without a companion result set.  */

struct xtt_multiresult_group {
  uint32_t value_write_mask;
  uint32_t companion_write_mask;
};

extern bool rvtt_multiresult_group (rtx_insn *, const xtt_effect_set &,
				    xtt_multiresult_group *);

/* If INSN is a zero-length LREG interface marker (the typed
   variable-LREG read/write patterns), set *LREG_MASK to its hard LREG
   and return true.  The markers emit no instruction word but carry the
   dataflow of fixed-LREG protocols, so a capture boundary must not
   separate them from the multi-result instruction they describe.  */
extern bool rvtt_lreg_marker (rtx_insn *, uint32_t *lreg_mask);

/* Replay-buffer recording epochs.  A user fixed capture of
   PAYLOAD_WORDS words opens an epoch, which closes when the
   instruction lengths of the following slot-occupying Tensix
   instructions add up to exactly that many words.  Otherwise the
   status says why:

     OPAQUE_PAYLOAD        - opaque asm, a call, an unrecognized insn, or
			     a declared length that would split one
			     instruction's words (blocker = the insn);
     CROSSES_BLOCK         - the block ends while the epoch is open;
     OWNER_DURING_CAPTURE  - an explicit REPLAY instruction inside
			     the open epoch (broken user code).

   On CLOSED, close_at is the last payload instruction, extended over
   any immediately following zero-length LREG interface markers, and
   multiresult_members counts the payload's multi-result
   instructions.  */

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
   written through SFPCONFIG destination 15) may be enabled anywhere in
   FN: true when FN contains a typed indexed SFPSWAP or a typed
   configuration write that can reach destination 15, except the exact
   immediate zero write (which cannot enable coupling).  That exception
   does not clear a possible enable elsewhere in FN.  SFPTRANSP does
   not depend on the coupling and does not count.  The reset state is
   disabled; an enable through a raw TTI word is not visible here.
   There is no disabling transition, so one enable covers the whole
   function.  */
extern bool rvtt_shadow_coupling_possible (function *);

/* Effects of SFPLOADMACRO launches emitted by the macro planner.

   A launch's effects depend on its macro descriptor, so
   rvtt_insn_effects treats every SFPLOADMACRO as opaque.  When the
   planner emits a launch it knows the descriptor, and records the
   launch's effects here, keyed by the insn.  The records are used by
   exec_interlocked_slots (rtl-rvtt-replay.cc) for pricing only;
   rvtt_insn_effects itself is unchanged.  All facts come from the
   descriptor:

     - lreg_read = 0: SFPLOADMACRO issue never waits on operands.
       SFPLOADMACRO.md schedules each sub-unit event for a fixed later
       cycle (issue + 1 + delay) with no issue-time register rule.
       Every register an event reads is the launch's own VD, a
       resident constant set up by the planner, or a schedule member
       whose readiness the schedule's delays already guarantee.
     - lreg_write = launch VD | the descriptor's template writes: the
       complete LREG write set of the launch.
     - result_latency = the launch's settle distance: the latest event
       completion after the launch's own slot, from the descriptor's
       SequenceBits delays and the subunit result latencies, so a
       consumer of a launch-written register waits for the macro to
       finish.
     - Writes cover all lanes: records exist only for schedules with
       no CC writes, which run with all lanes enabled, so a later full
       overwrite of a launch-written register is not a dependence.
       The pricing code may drop the write-side dependence edge.

   No record is made for CC-writing schedules or undecodable sequence
   bytes.  Lookup fails unless the insn is a recognized SFPLOADMACRO
   pattern whose INSN_UID, function, launch word and VD all match the
   record.  User-written launches (raw `.ttinsn' words) are asm and
   never have records.  */

extern void rvtt_planner_launch_effects_reset ();
extern void rvtt_planner_launch_effects_record (rtx_insn *, uint64_t word,
						unsigned vd_regno,
						const xtt_effect_set &);
extern bool rvtt_planner_launch_effects (rtx_insn *, xtt_effect_set *);

/* CC lane-state lattice shared by the Dst dataflow passes: all lanes
   enabled, some other known state, or unknown, with a bounded stack of
   saved states for SFPPUSHC/SFPPOPC.  Deeper nesting, or a POPC
   without a visible matching PUSHC, makes the state unknown.  */
enum rvtt_cc_val : uint8_t { RVTT_CC_ALL, RVTT_CC_OTHER, RVTT_CC_UNPROVED };

constexpr unsigned RVTT_CC_STACK_MAX = 16;

struct rvtt_cc_lattice
{
  rvtt_cc_val cc;
  uint8_t cc_depth;
  uint8_t cc_stack[RVTT_CC_STACK_MAX];

  bool cc_equal (const rvtt_cc_lattice &o) const
  {
    if (cc != o.cc || cc_depth != o.cc_depth)
      return false;
    for (unsigned i = 0; i < cc_depth && i < RVTT_CC_STACK_MAX; i++)
      if (cc_stack[i] != o.cc_stack[i])
	return false;
    return true;
  }

  void poison_cc () { cc = RVTT_CC_UNPROVED; cc_depth = 0; }

  void cc_push ()
  {
    if (cc == RVTT_CC_UNPROVED)
      return;
    if (cc_depth >= RVTT_CC_STACK_MAX)
      {
	poison_cc ();
	return;
      }
    cc_stack[cc_depth++] = cc;
  }

  void cc_pop ()
  {
    if (cc == RVTT_CC_UNPROVED)
      return;
    if (cc_depth == 0)
      {
	poison_cc ();
	return;
      }
    cc = (rvtt_cc_val) cc_stack[--cc_depth];
  }
};

#endif /* GCC_RVTT_EFFECTS_H */
