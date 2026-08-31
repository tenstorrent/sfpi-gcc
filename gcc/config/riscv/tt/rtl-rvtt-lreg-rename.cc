/* Break storage-induced false LREG dependences in capturable rows.
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

/* -mtt-tensix-optimize-lreg-rename (default off).

   After allocation, a row can carry a FALSE recurrence: an
   invariant-input member's destination register is also written by
   other row members purely as storage reuse (the allocator packed
   unrelated short lifetimes into one LREG).  The downstream stall-fill
   mechanisms (interlock fill, capture rotation in rtl-rvtt-schedule.cc)
   then refuse the member as a filler by name -- "writes a register
   another row member also writes" -- although no value ever flows
   between the colliding lifetimes.  This is regrename's classic du-chain
   problem (gcc/regrename.cc) scoped to the capturable-row shape with
   typed-effect proofs instead of constraint queries.

   v1 renames, per row, single-SET invariant-input members of audited
   latency 0 with no CC, configuration, or counter involvement, whose
   destination collides with other row members' writes, onto an LREG
   that is provably untouched: read and written by no row member, not
   live into the row (which excludes every loop-carried value and every
   hoisted invariant), and never a constant register.  The complete
   def-use web moves: the member's SET_DEST and every in-row reader of
   that definition up to the next writer of the old register.  A later
   in-row writer of the old register must exist (it proves the renamed
   value dies inside the row and cannot be live around the backedge).

   Admission requires every row member to carry complete typed effects
   (any opaque member kills the row) and an audited-latency stall to
   exist in the row (a rename without a stall to open has nothing to
   pay for and refuses).  Every proof failure refuses by name and
   changes nothing.

   -mtt-tensix-optimize-lreg-rename-chains (default off) is the
   GENERAL du-chain engine (FABLE_GOES_BURR.md item #7): post-RA
   register renaming over def-use chains in the gcc/regrename.cc
   formulation, restricted to the architectural LREG file, over
   single-basic-block regions of ANY shape (the v1 pass above is the
   self-loop capturable-row special case; it keeps its flag and
   behavior until the parity-census retirement ceremony).  One chain =
   one single-LREG definition plus every true reader of that
   definition up to the chain close (the next writer of the register,
   or the register's death at block exit).  The whole web moves to a
   proven-free LREG; delivered words are unchanged, only register
   fields move (asserted post-commit).

   Differences from v1, per the item-#7 plan:
   - any basic block, not only self-loop rows;
   - writers of any audited latency (v1: latency 0 only);
   - non-invariant inputs admitted (a pure rename moves no
     instruction, so input dependence is irrelevant -- v1's
     invariant-input clause was fill-admission smuggled into rename
     admission);
   - the payoff gate (v1's rename-no-stall-decrease) is DELETED from
     admission: the engine renames whenever a chain is legal and a
     consumer requests it.  Consumers price.  The standalone pass mode
     renames storage-collision chains greedily under a whole-row
     no-worse acceptance through the item-#11 timing engine
     (rvtt_timing::interlock_sim), the laneIJ strict-acceptance
     discipline;
   - the service export: rvtt_lreg_rename_chain (bb, def_insn,
     target) carries the complete legality proof and the post-commit
     re-verification, so the fill/rotation/IMS consumers can request
     renames without duplicating any proof.

   Typed-effect veto (the Tensix half; every fact through
   rvtt_insn_effects -- the item-#4 single table -- and
   rvtt-effects.h's post-admission helpers): a chain refuses on a CC
   write, config-dest write, RWC/Dst counter effect, Dst store
   destination, replay owner class, companion-coupled multi-result
   group, pinned zero-length LREG protocol markers, and any implicit
   (non-operand) register access -- an effect-mask reference to the
   chain register with no explicit REG occurrence in the pattern (the
   LUT implicit-slot class) can not be edited and refuses.

   CC-safety: a chain span (def to close) never crosses a basic-block
   boundary, and ANY CC-state event inside the span (cc_write in the
   typed effect set: SETCC/ENCC/PUSHC/POPC and every other mask
   writer, all-lanes-proven or not) refuses regrename-cc-span.  With
   the mask constant across the span, every reader executes under the
   writer's mask, lanewise operations never read sources on disabled
   lanes, and a kill-close overwrites exactly the written lanes -- the
   renamed register's undefined disabled lanes are unobservable and
   the old register's disabled lanes carry the identical pre-def
   values (the v1 soundness argument, now enforced for every chain).
   NOTE on tt/rvtt-cc-region.{h,cc}: that tree is a GIMPLE-statement
   analysis with no RTL mapping; this pass's fail-closed no-CC-in-span
   veto is strictly stronger than any structured-region query (no
   rename ever crosses a predicated region, structured or not).  A
   later stage that wants cross-region renames must extend the
   cc-region engine with an RTL view first, not fork a local scan.

   Death proof: the close is the next writer of the register inside
   the block (lane-exact under the constant-mask span rule), or --
   only in functions containing NO opaque instruction (calls, asm, raw
   .ttinsn words are invisible to DF hard-register liveness) -- the
   register's absence from the block's DF live-out set.  Anything else
   refuses regrename-chain-open.

   Post-commit structural lockstep re-verification (the laneID
   final-lockstep precedent: renames verified only before commit are
   the known wrong-code shape): after apply_change_group commits, the
   pass re-extracts every edited pattern's typed effects and re-proves
   the chain shape on the ACTUAL stream -- writer defines exactly the
   target, readers read the target and no longer the source, no other
   block instruction references the target, the close still kills,
   and every edited instruction re-recognizes with its delivered word
   count unchanged.  Any divergence reverts the rename, refuses
   regrename-postcommit-divergence (hard assert under -fchecking),
   and changes nothing.

   Pipeline placement: immediately after the v1 pass above -- post
   allocation (rtl-rvtt-lp-alloc), post Dst-ownership, post macro
   formation, and AHEAD of the hazard scheduler's fill passes, so a
   broken storage collision widens the fill candidate sets the same
   compilation (the identical seam rationale as v1; see
   rvtt-passes.def).  Renames neither add upward-exposed uses nor
   extend any live range across a block boundary, so DF liveness
   computed at pass entry stays valid across commits.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "df.h"
#include "tree-pass.h"
#include "cfgrtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "tm_p.h"
#include "print-rtl.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"

/* The one place the LREG-file capacity is read (item #10's engine;
   rvtt-pressure.h itself is a GIMPLE-side header, so the constant is
   reached through its extern).  */
extern unsigned rvtt_pressure_capacity ();

namespace {

static unsigned n_renamed;

static void
refuse (const char *reason, basic_block bb)
{
  rvtt_refuse_by_name (reason, dump_file,
		       "Lreg rename refused: %s in bb %d\n",
		       reason, bb->index);
}

struct row_member
{
  rtx_insn *insn;
  xtt_effect_set fx;
};

/* BB is a self-loop whose payload is SFPU words plus the counter step
   and jump (the capturable-row shape).  Collect the SFPU members with
   their typed effects; refuse on any opaque or scalar-extra content.
   Mirrors the admission the stall-fill passes use, so a rename here is
   visible exactly where the fills look.  */

static bool
rename_row_p (basic_block bb, std::vector<row_member> *members,
	      const char **reason)
{
  *reason = nullptr;
  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;

  bool saw_scalar = false;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    {
	      *reason = "control flow inside the row";
	      return false;
	    }
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	{
	  *reason = "rename-span-opaque";
	  return false;
	}
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	continue;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    continue; /* bookkeeping ghost */
	  row_member m;
	  m.insn = insn;
	  m.fx = rvtt_insn_effects (insn);
	  if (m.fx.opaque)
	    {
	      *reason = "rename-member-effects-unaudited";
	      return false;
	    }
	  members->push_back (m);
	  continue;
	}
      rtx set = single_set (insn);
      if (saw_scalar || !set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || contains_mem_rtx_p (PATTERN (insn)))
	{
	  *reason = "scalar payload beyond the counter";
	  return false;
	}
      saw_scalar = true;
    }
  return members->size () >= 2;
}

/* An audited stall exists: some member with result_latency > 0 is
   immediately followed (in issue order) by a member reading one of its
   destinations.  */

static bool
row_has_audited_stall_p (const std::vector<row_member> &members)
{
  for (size_t i = 0; i + 1 < members.size (); ++i)
    if (members[i].fx.result_latency > 0
	&& (members[i + 1].fx.lreg_read & members[i].fx.lreg_write))
      return true;
  return false;
}

/* Replace every use of hard reg OLDR with NEWR inside *LOC.  Collect
   change requests into the validate_change group.  */

static void
queue_reg_replacements (rtx_insn *insn, rtx *loc, unsigned oldr, rtx newreg)
{
  rtx x = *loc;
  if (!x)
    return;
  if (REG_P (x))
    {
      if (REGNO (x) == oldr)
	validate_change (insn, loc, gen_rtx_REG (GET_MODE (x),
						 REGNO (newreg)), true);
      return;
    }
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	queue_reg_replacements (insn, &XEXP (x, i), oldr, newreg);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  queue_reg_replacements (insn, &XVECEXP (x, i, j), oldr, newreg);
    }
}

/* Try to rename one colliding invariant-input single-writer chain in
   the row.  Returns true when a rename committed.  */

static bool
rename_one_chain (basic_block bb, std::vector<row_member> &members)
{
  /* Row-wide write masks and per-register writer counts.  */
  uint32_t row_writes = 0, row_reads = 0;
  bool row_writes_cc = false;
  for (const row_member &m : members)
    {
      row_writes |= m.fx.lreg_write;
      row_reads |= m.fx.lreg_read;
      row_writes_cc |= m.fx.cc_write;
    }

  for (size_t wi = 0; wi < members.size (); ++wi)
    {
      const row_member &w = members[wi];
      /* Single-destination, audited-latency-0, invariant-input,
	 pure-value member: the admissible filler class.  Renaming does
	 not move the instruction, so a lane-predicated (CC-reading)
	 writer is sound WHEN no row member writes CC: the mask is then
	 constant across the row, every reader executes under the same
	 mask as the writer, and lanewise operations never read their
	 sources on disabled lanes -- the renamed register's undefined
	 disabled lanes are unobservable (the prologue-rotation CC
	 discipline).  A CC-writing member refuses.  */
      if (w.fx.result_latency != 0
	  || w.fx.cc_write
	  || (w.fx.cc_read && row_writes_cc)
	  || w.fx.config_dests_written || w.fx.addr_mod_slot_write
	  || w.fx.rwc.kind != xtt_rwc_effect_t::NONE
	  || w.fx.dst_mem_read || w.fx.dst_mem_write
	  || popcount_hwi (w.fx.lreg_write) != 1)
	continue;
      unsigned oldr_bit_mask = w.fx.lreg_write;
      /* Inputs must be row-invariant: nothing another member writes.
	 The audited read mask includes the tied destination operand
	 position; a MERGING use of the prior destination value (a live
	 lv operand equal to the destination) would make the rename
	 read undefined lanes and refuses below via operand scan.  */
      if ((w.fx.lreg_read & ~w.fx.lreg_write) & row_writes)
	continue;
      /* The wall: another member also writes this register.  */
      uint32_t others_writes = 0;
      for (size_t i = 0; i < members.size (); ++i)
	if (i != wi)
	  others_writes |= members[i].fx.lreg_write;
      if (!(oldr_bit_mask & others_writes))
	continue;
      /* Genuine self-merge check: the destination register appearing as
	 an INPUT operand (a live lv merge) makes the output depend on
	 the destination's prior value; renaming would change it.  */
      {
	extract_insn (w.insn);
	rtx dest_op = recog_data.operand[0];
	bool self_merge = false;
	for (int oi = 1; oi < recog_data.n_operands; ++oi)
	  {
	    rtx op = recog_data.operand[oi];
	    if (REG_P (op) && REG_P (dest_op)
		&& REGNO (op) == REGNO (dest_op)
		&& recog_data.operand_type[oi] == OP_IN
		/* The tied compare/merge source alternative: treat any
		   input occurrence beyond the canonical unspec sources
		   as a merge.  Conservative: the plain forms carry a
		   noval marker there instead of a register.  */
		&& oi == 1)
	      self_merge = true;
	  }
	if (self_merge)
	  {
	    refuse ("rename-dest-self-merge", bb);
	    continue;
	  }
      }

      /* Locate hard regno.  Effect masks are L-indexed from L0.  */
      int lidx = exact_log2 (oldr_bit_mask);
      unsigned oldr = SFPU_REG_FIRST + lidx;

      /* Readers of W's definition: members after W (in row order) whose
	 TRUE reads (audited reads minus own writes -- the audited mask
	 counts the tied destination position) include OLDR, up to the
	 next writer of OLDR.  A next writer must exist (it proves the
	 value dies in-row); a next writer that also truly reads OLDR is
	 a tied merge whose read and write share one operand -- the
	 rename cannot split them and refuses.  */
      std::vector<rtx_insn *> readers;
      bool next_writer = false, tied_consumer = false;
      for (size_t i = wi + 1; i < members.size (); ++i)
	{
	  uint32_t true_reads
	    = members[i].fx.lreg_read & ~members[i].fx.lreg_write;
	  if (members[i].fx.lreg_write & oldr_bit_mask)
	    {
	      /* Audited reads on the writer that are not the tied
		 destination artifact: a genuine merge.  Detect via the
		 operand scan: any input operand register equal to OLDR
		 beyond the destination position.  */
	      extract_insn (members[i].insn);
	      for (int oi = 1; oi < recog_data.n_operands; ++oi)
		{
		  rtx op = recog_data.operand[oi];
		  if (REG_P (op) && REGNO (op) == oldr)
		    tied_consumer = true;
		}
	      next_writer = true;
	      break;
	    }
	  if (true_reads & oldr_bit_mask)
	    readers.push_back (members[i].insn);
	}
      if (!next_writer)
	{
	  refuse ("rename-value-crosses-row-boundary", bb);
	  continue;
	}
      if (tied_consumer)
	{
	  refuse ("rename-consumer-clobbers", bb);
	  continue;
	}

      /* A free architectural LREG: L0..L7, untouched by every member,
	 not live into the row (excludes loop-carried values and hoisted
	 invariants), never a constant register.  */
      int free_l = -1;
      for (int l = 0; l < 8; ++l)
	{
	  uint32_t bit = 1u << l;
	  if ((row_writes | row_reads) & bit)
	    continue;
	  if (REGNO_REG_SET_P (df_get_live_in (bb), SFPU_REG_FIRST + l))
	    continue;
	  free_l = l;
	  break;
	}
      if (free_l < 0)
	{
	  refuse ("rename-no-free-lreg", bb);
	  continue;
	}
      rtx newreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + free_l);

      /* Queue the writer's destination and each reader's uses, then
	 commit atomically.  */
      queue_reg_replacements (w.insn, &PATTERN (w.insn), oldr, newreg);
      for (rtx_insn *r : readers)
	queue_reg_replacements (r, &PATTERN (r), oldr, newreg);
      if (!apply_change_group ())
	{
	  refuse ("rename-constraint", bb);
	  continue;
	}
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Lreg rename: chain L%d -> L%d in bb %d (writer uid=%d,"
		   " %zu readers)\n",
		   lidx, free_l, bb->index, INSN_UID (w.insn),
		   readers.size ());
	}
      n_renamed++;
      return true;
    }
  return false;
}

const pass_data pass_data_rvtt_lreg_rename =
{
  RTL_PASS,
  "rvtt_lreg_rename",
  OPTGROUP_OTHER,
  TV_NONE,
  0,
  0,
  0,
  0,
  0,
};

class pass_rvtt_lreg_rename : public rtl_opt_pass
{
public:
  pass_rvtt_lreg_rename (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lreg_rename, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_lreg_rename > 0;
  }

  unsigned execute (function *) final override
  {
    df_analyze ();
    n_renamed = 0;
    basic_block bb;
    FOR_EACH_BB_FN (bb, cfun)
      {
	std::vector<row_member> members;
	const char *reason;
	if (!rename_row_p (bb, &members, &reason))
	  {
	    if (reason)
	      refuse (reason, bb);
	    continue;
	  }
	if (!row_has_audited_stall_p (members))
	  {
	    refuse ("rename-no-stall-decrease", bb);
	    continue;
	  }
	while (rename_one_chain (bb, members))
	  {
	    /* Effects changed; recollect for further chains.  */
	    members.clear ();
	    if (!rename_row_p (bb, &members, &reason))
	      break;
	  }
      }
    if (dump_file)
      fprintf (dump_file, "Lreg rename: renames=%u\n", n_renamed);
    if (n_renamed)
      df_analyze ();
    return 0;
  }
};

/* ==================================================================
   The general du-chain engine (item #7).  See the file header.  */

/* One scanned block position.  */

struct span_insn
{
  rtx_insn *insn;
  enum kind_t { SI_TENSIX, SI_MARKER, SI_SCALAR, SI_OPAQUE, SI_GHOST } kind;
  xtt_effect_set fx;		/* SI_TENSIX only.  */
  uint32_t touch;		/* L0..L7 bits this position references.  */
};

/* Architectural LREG bits (L0..L7) of every explicit REG occurrence
   in X.  Paranoid vocabulary for non-effect-audited positions.  */

static uint32_t
sfpu_reg_mask (rtx x)
{
  if (!x)
    return 0;
  if (REG_P (x))
    return SFPU_REG_P (REGNO (x)) ? 1u << (REGNO (x) - SFPU_REG_FIRST) : 0;
  uint32_t mask = 0;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	mask |= sfpu_reg_mask (XEXP (x, i));
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  mask |= sfpu_reg_mask (XVECEXP (x, i, j));
    }
  return mask;
}

/* Explicit REG occurrences of hard register REGNO in X.  */

static unsigned
count_reg_occurrences (rtx x, unsigned regno)
{
  if (!x)
    return 0;
  if (REG_P (x))
    return REGNO (x) == regno ? 1 : 0;
  unsigned n = 0;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	n += count_reg_occurrences (XEXP (x, i), regno);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  n += count_reg_occurrences (XVECEXP (x, i, j), regno);
    }
  return n;
}

/* Classify every nondebug insn of BB in order.  */

static void
scan_block (basic_block bb, std::vector<span_insn> *out)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      span_insn si;
      si.insn = insn;
      si.fx = xtt_effect_set ();
      si.touch = 0;
      uint32_t marker_mask = 0;
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	si.kind = span_insn::SI_OPAQUE;
      else if (GET_CODE (PATTERN (insn)) == USE
	       || GET_CODE (PATTERN (insn)) == CLOBBER)
	{
	  /* Bookkeeping; any LREG mention still poisons the bits.  */
	  si.kind = span_insn::SI_SCALAR;
	  si.touch = sfpu_reg_mask (PATTERN (insn));
	}
      else if (rvtt_lreg_marker (insn, &marker_mask))
	{
	  /* Zero-length pinned-LREG protocol marker: the register is
	     part of a fixed-LREG interface and can never move.  */
	  si.kind = span_insn::SI_MARKER;
	  si.touch = marker_mask & 0xFF;
	}
      else if (recog_memoized (insn) >= 0
	       && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    si.kind = span_insn::SI_GHOST;	/* bookkeeping ghost */
	  else
	    {
	      si.kind = span_insn::SI_TENSIX;
	      si.fx = rvtt_insn_effects (insn);
	      si.touch = (si.fx.lreg_read | si.fx.lreg_write) & 0xFF;
	    }
	  /* Fail closed against pattern references the effect audit
	     does not carry (and ghosts' pinned bits).  */
	  si.touch |= sfpu_reg_mask (PATTERN (insn)) & 0xFF;
	}
      else
	{
	  si.kind = span_insn::SI_SCALAR;
	  si.touch = sfpu_reg_mask (PATTERN (insn)) & 0xFF;
	}
      out->push_back (si);
    }
}

/* The function contains an instruction DF hard-register liveness
   cannot see through (call, asm, raw .ttinsn word).  Gates the
   dead-at-exit chain close.  */

static bool
function_has_opaque_insn_p (function *fn)
{
  basic_block bb;
  rtx_insn *insn;
  FOR_EACH_BB_FN (bb, fn)
    FOR_BB_INSNS (bb, insn)
      if (NONDEBUG_INSN_P (insn)
	  && (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0))
	return true;
  return false;
}

/* One analyzed, rename-admissible chain.  */

struct chain_desc
{
  basic_block bb;
  std::vector<span_insn> scan;	/* the block scan the proof used */
  size_t wi;			/* writer index into scan */
  std::vector<size_t> readers;	/* reader indices */
  size_t close;			/* kill-close index, or scan.size () */
  bool close_reads;		/* the close also reads the chain value
				   through clean OP_IN operands */
  int old_l, new_l;		/* L indices */
};

static unsigned n_chain_renamed;

static void
refuse_chain (const char *reason, rtx_insn *at, basic_block bb)
{
  rvtt_refuse_by_name_at (reason, at, dump_file,
			  "Lreg chain rename refused: %s in bb %d\n",
			  reason, bb->index);
}

/* W is an explicit-operand-editable member of a chain on hard reg
   OLDR: every effect-mask reference has a matching explicit REG
   occurrence in the pattern (the LUT implicit-slot / hidden-access
   veto).  */

static bool
explicit_operand_p (const span_insn &si, unsigned oldr)
{
  return count_reg_occurrences (PATTERN (si.insn), oldr) > 0;
}

/* Analyze the chain of the single-LREG definition at SCAN[WI] in BB.
   On success fill *CH (which takes a copy of SCAN) and return true;
   otherwise refuse by name and return false.  FN_HAS_OPAQUE gates the
   dead-at-exit close.  */

static bool
analyze_chain (basic_block bb, const std::vector<span_insn> &scan,
	       size_t wi, bool fn_has_opaque, int target_l,
	       chain_desc *ch)
{
  const span_insn &w = scan[wi];
  gcc_assert (w.kind == span_insn::SI_TENSIX);

  /* Writer vetoes, all through the one typed-effect table.  */
  if (w.fx.opaque)
    {
      refuse_chain ("regrename-span-opaque", w.insn, bb);
      return false;
    }
  if (popcount_hwi (w.fx.lreg_write) != 1
      || (w.fx.lreg_write & ~0xFFu))
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  uint32_t oldbit = w.fx.lreg_write;
  int old_l = exact_log2 (oldbit);
  if (old_l < 0 || old_l >= (int) SFPU_REG_NUM)
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  unsigned oldr = SFPU_REG_FIRST + old_l;
  if (w.fx.cc_write || w.fx.config_dests_written || w.fx.addr_mod_slot_write
      || w.fx.rwc.kind != xtt_rwc_effect_t::NONE || w.fx.dst_mem_write)
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  xtt_multiresult_group grp;
  if (rvtt_multiresult_group (w.insn, w.fx, &grp))
    {
      refuse_chain ("regrename-companion-coupled", w.insn, bb);
      return false;
    }
  if (get_attr_xtt_replay (w.insn) == XTT_REPLAY_OWNER)
    {
      refuse_chain ("regrename-replay-boundary", w.insn, bb);
      return false;
    }
  /* A def with an EXPLICIT input occurrence of its own destination
     register -- the LV merge source, or the destructive families'
     "0"-tied source (the BH XOR shape) -- genuinely reads the prior
     value: the chain is not closed at the top and a whole-pattern
     replacement would rewrite the source read too.  Refuse on ANY
     explicit OP_IN occurrence, at every position (v1's canonical
     operand-1 test was sufficient only under its latency-0 admission,
     which never admits the destructive families).  The audited read
     mask alone does not decide: it carries the tied-destination
     artifact even for the noval (non-merging) alternatives, which
     have no explicit source REG and are safe.  */
  if (w.fx.lreg_read & oldbit)
    {
      extract_insn (w.insn);
      for (int oi = 0; oi < recog_data.n_operands; ++oi)
	{
	  rtx op = recog_data.operand[oi];
	  if (REG_P (op) && REGNO (op) == oldr
	      && recog_data.operand_type[oi] == OP_IN)
	    {
	      refuse_chain ("regrename-self-merge", w.insn, bb);
	      return false;
	    }
	}
    }
  if (!explicit_operand_p (w, oldr))
    {
      refuse_chain ("regrename-implicit-operand", w.insn, bb);
      return false;
    }

  /* Walk the span: collect readers up to the close.  */
  std::vector<size_t> readers;
  size_t close = scan.size ();
  bool close_reads = false;
  for (size_t i = wi + 1; i < scan.size (); ++i)
    {
      const span_insn &si = scan[i];
      switch (si.kind)
	{
	case span_insn::SI_OPAQUE:
	  refuse_chain ("regrename-span-opaque", si.insn, bb);
	  return false;
	case span_insn::SI_MARKER:
	  if (si.touch & oldbit)
	    {
	      refuse_chain ("regrename-pinned-protocol", si.insn, bb);
	      return false;
	    }
	  continue;
	case span_insn::SI_SCALAR:
	case span_insn::SI_GHOST:
	  if (si.touch & oldbit)
	    {
	      refuse_chain ("regrename-pinned-protocol", si.insn, bb);
	      return false;
	    }
	  continue;
	case span_insn::SI_TENSIX:
	  break;
	}
      if (si.fx.opaque)
	{
	  refuse_chain ("regrename-span-opaque", si.insn, bb);
	  return false;
	}
      if (si.fx.lreg_write & oldbit)
	{
	  /* The close.  The allocator's favourite reuse packs a dying
	     source's register into the consumer's destination, so the
	     close often BOTH genuinely reads the chain value AND
	     kills the register.  A clean OP_IN read is splittable:
	     only the input operand locations move to the target and
	     the kill stays.  A read through the destination itself
	     (an OP_INOUT operand) is a tied merge the rename cannot
	     split and refuses.  */
	  extract_insn (si.insn);
	  for (int oi = 0; oi < recog_data.n_operands; ++oi)
	    {
	      rtx op = recog_data.operand[oi];
	      if (REG_P (op) && REGNO (op) == oldr
		  && recog_data.operand_type[oi] == OP_INOUT)
		{
		  refuse_chain ("regrename-tied-close", si.insn, bb);
		  return false;
		}
	      if (REG_P (op) && REGNO (op) == oldr
		  && recog_data.operand_type[oi] == OP_IN)
		close_reads = true;
	    }
	  close = i;
	  break;
	}
      if (si.fx.cc_write)
	{
	  /* A mask event inside the open span: a reader past it may
	     observe lanes the writer never wrote, and a lane-masked
	     close past it kills only the narrowed lanes -- either way
	     the disabled-lane contents of the two worlds diverge.
	     Fail closed on ANY CC event inside the span (see the file
	     header; strictly stronger than every structured-region
	     query).  */
	  refuse_chain ("regrename-cc-span", si.insn, bb);
	  return false;
	}
      if (si.fx.lreg_read & oldbit)
	{
	  if (get_attr_xtt_replay (si.insn) == XTT_REPLAY_OWNER)
	    {
	      refuse_chain ("regrename-replay-boundary", si.insn, bb);
	      return false;
	    }
	  if (!explicit_operand_p (si, oldr))
	    {
	      refuse_chain ("regrename-implicit-operand", si.insn, bb);
	      return false;
	    }
	  readers.push_back (i);
	}
    }
  if (close == scan.size ())
    {
      /* No kill inside the block: admissible only as a dead-at-exit
	 close, and only when DF's hard-register liveness is beyond
	 suspicion (no opaque instruction anywhere in the function --
	 calls, asm, and raw .ttinsn words are invisible to DF
	 hard-register liveness).  The span (and so the no-CC rule
	 above) then extends to the block end.  */
      if (fn_has_opaque
	  || REGNO_REG_SET_P (df_get_live_out (bb), oldr))
	{
	  refuse_chain ("regrename-chain-open", w.insn, bb);
	  return false;
	}
    }

  /* Target selection: a free architectural LREG, deterministic lowest
     index.  Free = untouched by every position in the block (typed
     effects plus the paranoid pattern scan), not live into and not
     live out of the block, never a constant register (the L0..L7
     range is the allocatable file; the capacity is read through the
     item-#10 engine).  */
  uint32_t block_touch = 0;
  for (const span_insn &si : scan)
    block_touch |= si.touch;
  unsigned nlreg = SFPU_REG_NUM;
  if (rvtt_pressure_capacity () < nlreg)
    nlreg = rvtt_pressure_capacity ();
  int new_l = -1;
  for (unsigned l = 0; l < nlreg; ++l)
    {
      if (target_l >= 0 && (int) l != target_l)
	continue;
      uint32_t bit = 1u << l;
      if (block_touch & bit)
	continue;
      if (REGNO_REG_SET_P (df_get_live_in (bb), SFPU_REG_FIRST + l)
	  || REGNO_REG_SET_P (df_get_live_out (bb), SFPU_REG_FIRST + l))
	continue;
      new_l = l;
      break;
    }
  if (new_l < 0)
    {
      refuse_chain ("regrename-no-free-lreg", w.insn, bb);
      return false;
    }

  ch->bb = bb;
  ch->scan = scan;
  ch->wi = wi;
  ch->readers = readers;
  ch->close = close;
  ch->close_reads = close_reads;
  ch->old_l = old_l;
  ch->new_l = new_l;
  return true;
}

/* Whole-row no-worse acceptance for the standalone pass mode: the
   modeled interlocked issue-slot count of the span (writer through
   close inclusive) under the item-#11 scoreboard, before and after
   the register-field edit.  Returns false (refusing by name) when the
   span is unpriceable -- an unaudited producer feeding a span
   consumer; the laneIJ strict-acceptance discipline prices nothing it
   cannot prove.  */

static bool
span_no_worse_p (const chain_desc &ch)
{
  uint32_t oldbit = 1u << ch.old_l;
  uint32_t newbit = 1u << ch.new_l;
  size_t end = ch.close == ch.scan.size () ? ch.scan.size () : ch.close + 1;
  int64_t slots[2] = { 0, 0 };
  for (int world = 0; world < 2; ++world)
    {
      rvtt_timing::interlock_sim sim;
      for (size_t i = ch.wi; i < end; ++i)
	{
	  const span_insn &si = ch.scan[i];
	  if (si.kind != span_insn::SI_TENSIX)
	    continue;
	  uint32_t read = si.fx.lreg_read;
	  uint32_t write = si.fx.lreg_write;
	  if (world == 1)
	    {
	      if (i == ch.wi)
		write = (write & ~oldbit) | newbit;
	      else if (ch.close_reads && i == ch.close)
		read = (read & ~oldbit) | newbit;
	      else
		for (size_t r : ch.readers)
		  if (r == i)
		    read = (read & ~oldbit) | newbit;
	    }
	  rvtt_timing::issue_op op;
	  op.deps = (read | write) & 0xFFFF;
	  op.writes = write & 0xFFFF;
	  op.words = get_attr_length (si.insn) / 4;
	  op.lat = si.fx.result_latency;
	  op.next_slot_stall = si.fx.next_slot_stall;
	  if (!sim.step (op))
	    {
	      refuse_chain ("regrename-row-unpriceable",
			    si.insn, ch.bb);
	      return false;
	    }
	}
      slots[world] = sim.slots ();
    }
  if (slots[1] > slots[0])
    {
      refuse_chain ("regrename-cost-regressed",
		    ch.scan[ch.wi].insn, ch.bb);
      return false;
    }
  return true;
}

/* Commit CH: move the def-use web from OLD_L to NEW_L, then re-prove
   the chain shape on the ACTUAL committed stream (the laneID
   final-lockstep discipline; see the file header).  Any divergence
   reverts, refuses by name, and hard-asserts under -fchecking.
   Returns true iff the rename stands.  */

static bool
commit_chain (const chain_desc &ch)
{
  basic_block bb = ch.bb;
  unsigned oldr = SFPU_REG_FIRST + ch.old_l;
  unsigned newr = SFPU_REG_FIRST + ch.new_l;
  uint32_t oldbit = 1u << ch.old_l;
  uint32_t newbit = 1u << ch.new_l;
  rtx newreg = gen_rtx_REG (XTT32SImode, newr);
  rtx_insn *w_insn = ch.scan[ch.wi].insn;

  /* Recorded pre-commit facts for the lockstep re-verification.  */
  std::vector<size_t> edited;
  edited.push_back (ch.wi);
  for (size_t r : ch.readers)
    edited.push_back (r);
  std::vector<int> pre_len;
  std::vector<uint32_t> pre_write;
  for (size_t e : edited)
    {
      pre_len.push_back (get_attr_length (ch.scan[e].insn));
      pre_write.push_back (ch.scan[e].fx.lreg_write);
    }

  for (size_t e : edited)
    queue_reg_replacements (ch.scan[e].insn, &PATTERN (ch.scan[e].insn),
			    oldr, newreg);
  if (ch.close_reads)
    {
      /* The close both kills the register and genuinely reads the
	 chain value: only its clean OP_IN operand locations (and
	 their match_dups) move; the kill stays put.  */
      rtx_insn *close_insn = ch.scan[ch.close].insn;
      extract_insn (close_insn);
      bool renamed_op[MAX_RECOG_OPERANDS] = {};
      for (int oi = 0; oi < recog_data.n_operands; ++oi)
	{
	  rtx op = recog_data.operand[oi];
	  if (REG_P (op) && REGNO (op) == oldr
	      && recog_data.operand_type[oi] == OP_IN)
	    {
	      validate_change (close_insn, recog_data.operand_loc[oi],
			       gen_rtx_REG (GET_MODE (op), newr), true);
	      renamed_op[oi] = true;
	    }
	}
      for (int di = 0; di < recog_data.n_dups; ++di)
	if (renamed_op[recog_data.dup_num[di]])
	  {
	    rtx dup = *recog_data.dup_loc[di];
	    validate_change (close_insn, recog_data.dup_loc[di],
			     gen_rtx_REG (GET_MODE (dup), newr), true);
	  }
    }
  if (!apply_change_group ())
    {
      refuse_chain ("regrename-constraint", w_insn, bb);
      return false;
    }

  /* ---- Post-commit structural lockstep re-verification.  */
  const char *diverged = nullptr;
  for (size_t ei = 0; ei < edited.size () && !diverged; ++ei)
    {
      rtx_insn *insn = ch.scan[edited[ei]].insn;
      if (recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || get_attr_length (insn) != pre_len[ei])
	{
	  diverged = "edited insn shape";
	  break;
	}
      xtt_effect_set fx = rvtt_insn_effects (insn);
      if (fx.opaque)
	diverged = "edited insn effects opaque";
      else if (edited[ei] == ch.wi)
	{
	  /* The audited read mask may carry the tied-destination
	     artifact, which legitimately moved to the target -- but an
	     EXPLICIT input occurrence of the target would mean the
	     replacement rewrote a genuine source read (the self-merge
	     admission failed us): divergence.  */
	  if (fx.lreg_write != newbit || (fx.lreg_read & oldbit))
	    diverged = "writer web";
	  else
	    {
	      extract_insn (insn);
	      for (int oi = 0; oi < recog_data.n_operands; ++oi)
		{
		  rtx op = recog_data.operand[oi];
		  if (REG_P (op) && REGNO (op) == newr
		      && recog_data.operand_type[oi] == OP_IN)
		    diverged = "writer source rewritten";
		}
	    }
	}
      else if (!(fx.lreg_read & newbit) || (fx.lreg_read & oldbit)
	       || fx.lreg_write != pre_write[ei])
	diverged = "reader web";
      if (!diverged && count_reg_occurrences (PATTERN (insn), oldr))
	diverged = "stale source reference";
    }
  if (!diverged && ch.close != ch.scan.size ())
    {
      rtx_insn *close_insn = ch.scan[ch.close].insn;
      xtt_effect_set fx = rvtt_insn_effects (close_insn);
      if (!(fx.lreg_write & oldbit))
	diverged = "close no longer kills";
      else if (ch.close_reads)
	{
	  /* The moved reads must be on the target now and no clean
	     OP_IN read of the source may remain.  */
	  if (!(fx.lreg_read & newbit))
	    diverged = "reading close web";
	  else if (recog_memoized (close_insn) < 0)
	    diverged = "reading close shape";
	  else
	    {
	      extract_insn (close_insn);
	      for (int oi = 0; oi < recog_data.n_operands; ++oi)
		{
		  rtx op = recog_data.operand[oi];
		  if (REG_P (op) && REGNO (op) == oldr
		      && recog_data.operand_type[oi] == OP_IN)
		    diverged = "stale reading-close reference";
		}
	    }
	}
    }
  if (!diverged)
    {
      /* No other position in the block may reference the target.  */
      std::vector<span_insn> rescan;
      scan_block (bb, &rescan);
      unsigned hits = 0;
      for (const span_insn &si : rescan)
	if (si.touch & newbit)
	  ++hits;
      if (hits != edited.size () + (ch.close_reads ? 1 : 0))
	diverged = "target reference census";
    }
  if (diverged)
    {
      /* Fail closed: revert the web, refuse by name.  (For a reading
	 close, its only target references are the moved inputs, so
	 the full-pattern replacement reverts exactly those.)  */
      rtx oldreg_rtx = gen_rtx_REG (XTT32SImode, oldr);
      for (size_t e : edited)
	queue_reg_replacements (ch.scan[e].insn,
				&PATTERN (ch.scan[e].insn), newr,
				oldreg_rtx);
      if (ch.close_reads)
	queue_reg_replacements (ch.scan[ch.close].insn,
				&PATTERN (ch.scan[ch.close].insn), newr,
				oldreg_rtx);
      bool reverted = apply_change_group ();
      if (dump_file)
	fprintf (dump_file,
		 "Lreg chain rename POST-COMMIT DIVERGENCE (%s),"
		 " reverted=%d in bb %d\n", diverged, reverted, bb->index);
      refuse_chain ("regrename-postcommit-divergence", w_insn, bb);
      if (flag_checking)
	gcc_assert (reverted && !"lreg chain rename post-commit divergence");
      return false;
    }

  if (dump_file)
    {
      int words = 0;
      for (size_t e : edited)
	words += get_attr_length (ch.scan[e].insn) / 4;
      fprintf (dump_file,
	       "Lreg chain rename: L%d -> L%d in bb %d (def uid=%d,"
	       " %zu readers, close=%s)\n",
	       ch.old_l, ch.new_l, bb->index, INSN_UID (w_insn),
	       ch.readers.size (),
	       ch.close == ch.scan.size () ? "dead-at-exit"
	       : ch.close_reads ? "kill+read" : "kill");
      fprintf (dump_file,
	       "Lreg chain census: %d words before == %d words after,"
	       " register fields only\n", words, words);
    }
  n_chain_renamed++;
  return true;
}

const pass_data pass_data_rvtt_lreg_rename_chains =
{
  RTL_PASS,
  "rvtt_lreg_rename_chains",
  OPTGROUP_OTHER,
  TV_NONE,
  0,
  0,
  0,
  0,
  0,
};

class pass_rvtt_lreg_rename_chains : public rtl_opt_pass
{
public:
  pass_rvtt_lreg_rename_chains (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lreg_rename_chains, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_lreg_rename_chains > 0;
  }

  unsigned execute (function *fn) final override
  {
    df_analyze ();
    n_chain_renamed = 0;
    bool fn_has_opaque = function_has_opaque_insn_p (fn);
    basic_block bb;
    FOR_EACH_BB_FN (bb, fn)
      {
	/* Greedy standalone mode: rename storage-collision chains,
	   in stream order, each under the whole-row no-worse
	   acceptance.  Re-scan after every commit (effects moved).  */
	bool progress = true;
	unsigned attempts = 0;
	std::vector<bool> tried;
	while (progress && ++attempts < 64)
	  {
	    progress = false;
	    std::vector<span_insn> scan;
	    scan_block (bb, &scan);
	    if (tried.size () < scan.size ())
	      tried.resize (scan.size (), false);
	    for (size_t i = 0; i < scan.size (); ++i)
	      {
		if (tried[i] || scan[i].kind != span_insn::SI_TENSIX
		    || scan[i].fx.opaque
		    || popcount_hwi (scan[i].fx.lreg_write) != 1)
		  continue;
		uint32_t bit = scan[i].fx.lreg_write;
		if (bit & ~0xFFu)
		  continue;
		/* The wall: another position also writes this
		   register (the storage collision the fills refuse
		   by).  Non-colliding chains are not candidates in
		   standalone mode; consumers may still request them
		   through the service.  */
		bool collision = false;
		for (size_t j = 0; j < scan.size () && !collision; ++j)
		  if (j != i && scan[j].kind == span_insn::SI_TENSIX
		      && (scan[j].fx.lreg_write & bit))
		    collision = true;
		if (!collision)
		  continue;
		tried[i] = true;
		chain_desc ch;
		if (!analyze_chain (bb, scan, i, fn_has_opaque, -1, &ch))
		  continue;
		if (!span_no_worse_p (ch))
		  continue;
		if (commit_chain (ch))
		  {
		    progress = true;
		    break;	/* re-scan */
		  }
	      }
	  }
      }
    if (dump_file)
      fprintf (dump_file, "Lreg chain rename: renames=%u\n",
	       n_chain_renamed);
    if (n_chain_renamed)
      df_analyze ();
    return 0;
  }
};

} // anonymous namespace

/* Service export (item #7): attempt to rename the du-chain of
   DEF_INSN's single-LREG definition inside BB onto TARGET_LREG (an L
   index; -1 = lowest proven-free).  Carries the complete legality
   proof and the post-commit structural re-verification; refuses by
   name and changes nothing on any unproven clause.  No pricing: the
   requesting consumer prices (the item-#7 decoupling).  DF liveness
   must be current on entry; a committed rename leaves it valid (no
   upward exposure or cross-block extension is ever added).  Returns
   true iff a rename committed.  */

bool
rvtt_lreg_rename_chain (basic_block bb, rtx_insn *def_insn, int target_lreg)
{
  std::vector<span_insn> scan;
  scan_block (bb, &scan);
  size_t wi = scan.size ();
  for (size_t i = 0; i < scan.size (); ++i)
    if (scan[i].insn == def_insn)
      {
	wi = i;
	break;
      }
  if (wi == scan.size () || scan[wi].kind != span_insn::SI_TENSIX)
    {
      rvtt_refuse_by_name_at ("regrename-effect-veto", def_insn, dump_file,
			      "Lreg chain rename refused:"
			      " regrename-effect-veto in bb %d\n", bb->index);
      return false;
    }
  chain_desc ch;
  if (!analyze_chain (bb, scan, wi, function_has_opaque_insn_p (cfun),
		      target_lreg, &ch))
    return false;
  return commit_chain (ch);
}

rtl_opt_pass *
make_pass_rvtt_lreg_rename (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_rename (ctxt);
}

rtl_opt_pass *
make_pass_rvtt_lreg_rename_chains (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_rename_chains (ctxt);
}
