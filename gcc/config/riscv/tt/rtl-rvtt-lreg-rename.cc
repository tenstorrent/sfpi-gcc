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
   changes nothing.  */

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

namespace {

static unsigned n_renamed;

static void
refuse (const char *reason, basic_block bb)
{
  if (dump_file)
    fprintf (dump_file, "Lreg rename refused: %s in bb %d\n",
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
  OPTGROUP_NONE,
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

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_lreg_rename (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_rename (ctxt);
}
