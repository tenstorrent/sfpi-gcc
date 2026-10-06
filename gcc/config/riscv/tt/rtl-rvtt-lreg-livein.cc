/* Make raw LLK L-register accesses visible to IRA.
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

/* This pass runs before IRA for every Tensix compilation, including -O0.
   Its gate is TARGET_XTT_TENSIX alone: exposing raw architectural register
   ownership is a correctness requirement at every optimization level.

   The missing dataflow

   Hand-written LLK instructions expressed as raw .ttinsn words have no RTL
   definitions or uses of their architectural L-registers.  For example, a
   raw SFPLOAD can put a value in L1 that a later raw instruction consumes.
   Between those instructions, IRA sees no live value in L1 and may assign
   it to a compiler-generated vFloat temporary.  That temporary silently
   overwrites the raw value.  Both instruction sequences look locally valid;
   neither allocation failure nor a diagnostic exposes the wrong code.

   Representing the reservation

   The sfprawlreg_effect metadata builtin names the read and write effects of
   the immediately preceding opaque instruction.  The sfprawlreg_access
   builtin names explicit ownership boundaries with release and write masks.
   Callers must supply these annotations; this pass does not decode arbitrary
   inline assembly.  A forward dataflow
   fixed point carries eight-bit ownership masks across CFG edges, taking
   the union of predecessor masks at joins.  A backward liveness fixed point
   carries effect reads to their reaching typed/raw definition or function
   entry, so inputs are protected across the entire producer-to-consumer gap.
   Effect writes are definitions in that backward problem, so dead raw outputs
   do not consume scarce LREGs to function exit.  An access marker releases
   the named reservations and opens explicitly owned/live-out writes.  A typed
   sfpreadlreg<N> or sfpwritelreg<N> ends the raw reservation for that register.

   Each block materializes its incoming reservations and new raw writes as
   zero-length reads of the corresponding hard LREG.  A USE keeps that hard
   register live until the consuming builtin, a release or rewrite marker,
   or the end of the block.  Successors create their own local intervals.
   This reserves the register across each affected block without inventing a
   cross-block pseudo or phi for the raw value.

   A point clobber alone cannot protect the interval between producer and
   consumer.  Permanently fixing the register would also remove it from
   allocation where raw code does not own it.  The sentinel intervals make
   precisely these conservative reservations visible to IRA instead.

   The sentinels and USEs emit no instruction words of their own.  Register
   assignments, and therefore the emitted instructions, can change when an
   unsafe allocation is prevented.  Removing this pass while retaining the
   metadata would silently discard the ownership contract, which is why it
   has no optimization flag.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "rvtt.h"

namespace {

/* Metadata builtins name raw architectural L-register accesses.  Raw .ttinsn
   otherwise has no RTL def/use, so IRA is free to reuse a future input (for
   example L1 after a raw SFPLOAD) as a vFloat temporary.  A zero-length,
   fixed-register read creates a normal IRA interval.  The hard register is
   used at each local endpoint; joins get a fresh local interval, deliberately
   avoiding a cross-CFG pseudo/phi while conservatively reserving the LREG.  */

static int
read_lregno (rtx_insn *insn)
{
  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfpreadlreg0: return 0;
    case CODE_FOR_rvtt_sfpreadlreg1: return 1;
    case CODE_FOR_rvtt_sfpreadlreg2: return 2;
    case CODE_FOR_rvtt_sfpreadlreg3: return 3;
    case CODE_FOR_rvtt_sfpreadlreg4: return 4;
    case CODE_FOR_rvtt_sfpreadlreg5: return 5;
    case CODE_FOR_rvtt_sfpreadlreg6: return 6;
    case CODE_FOR_rvtt_sfpreadlreg7: return 7;
    default: return -1;
    }
}

/* The architectural L-register number written when INSN is one of the
   rvtt_sfpwritelreg<N> metadata builtins, -1 otherwise.  Like a read,
   a write builtin ends any raw interval open on that LREG.  */

static int
write_lregno (rtx_insn *insn)
{
  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfpwritelreg0: return 0;
    case CODE_FOR_rvtt_sfpwritelreg1: return 1;
    case CODE_FOR_rvtt_sfpwritelreg2: return 2;
    case CODE_FOR_rvtt_sfpwritelreg3: return 3;
    case CODE_FOR_rvtt_sfpwritelreg4: return 4;
    case CODE_FOR_rvtt_sfpwritelreg5: return 5;
    case CODE_FOR_rvtt_sfpwritelreg6: return 6;
    case CODE_FOR_rvtt_sfpwritelreg7: return 7;
    default: return -1;
    }
}

/* Return true when INSN is the rvtt_sfprawlreg_access marker,
   extracting its two operands: *RELEASE_MASK names the LREGs whose raw
   ownership ends at this point and *WRITE_MASK the LREGs the raw code
   writes here (bit N is LREG N in both).  */

static bool
raw_access_p (rtx_insn *insn, unsigned *release_mask, unsigned *write_mask)
{
  if (recog_memoized (insn) != CODE_FOR_rvtt_sfprawlreg_access)
    return false;

  rtx pat = PATTERN (insn);
  gcc_assert (GET_CODE (pat) == UNSPEC_VOLATILE
	      && XINT (pat, 1) == UNSPECV_SFPRAWLREG_ACCESS);
  *release_mask = UINTVAL (XVECEXP (pat, 0, 0)) & 0xff;
  *write_mask = UINTVAL (XVECEXP (pat, 0, 1)) & 0xff;
  return true;
}

/* Return true when INSN is the rvtt_sfprawlreg_effect marker.  *READ_MASK
   names inputs of the immediately preceding opaque instruction and
   *WRITE_MASK names its outputs.  Reads are backward uses and writes are
   backward definitions.  Explicit raw ownership/live-out values use
   sfprawlreg_access instead.  */

static bool
raw_effect_p (rtx_insn *insn, unsigned *read_mask, unsigned *write_mask)
{
  if (recog_memoized (insn) != CODE_FOR_rvtt_sfprawlreg_effect)
    return false;

  rtx pat = PATTERN (insn);
  gcc_assert (GET_CODE (pat) == UNSPEC_VOLATILE
	      && XINT (pat, 1) == UNSPECV_SFPRAWLREG_EFFECT);
  rtx read = XVECEXP (pat, 0, 0);
  rtx write = XVECEXP (pat, 0, 1);
  *read_mask = CONST_INT_P (read) ? UINTVAL (read) & 0xff : 0xff;
  *write_mask = CONST_INT_P (write) ? UINTVAL (write) & 0xff : 0xff;

  /* Header-side decoders can be fed an instruction word containing a runtime
     address.  Its exact LREG fields are then not a front-end constant.  Make
     that case conservatively affect every LREG, and replace the operands now
     so no runtime mask computation survives this mandatory metadata pass.  */
  if (!CONST_INT_P (read) || !CONST_INT_P (write))
    {
      XVECEXP (pat, 0, 0) = GEN_INT (*read_mask);
      XVECEXP (pat, 0, 1) = GEN_INT (*write_mask);
      INSN_CODE (insn) = -1;
    }
  return true;
}

/* RTL for a sentinel read of hard LREG REGNO defining VALUE, which is the
   same hard register.  Using a pseudo here is insufficient: IRA may allocate
   it to another LREG and leave reload to satisfy the fixed output constraint,
   so the raw-owned LREG would not be reserved during allocation.  */

static rtx
make_sentinel (unsigned regno, rtx value)
{
  switch (regno)
    {
    case 0: return gen_rvtt_sfpreadlreg0 (value);
    case 1: return gen_rvtt_sfpreadlreg1 (value);
    case 2: return gen_rvtt_sfpreadlreg2 (value);
    case 3: return gen_rvtt_sfpreadlreg3 (value);
    case 4: return gen_rvtt_sfpreadlreg4 (value);
    case 5: return gen_rvtt_sfpreadlreg5 (value);
    case 6: return gen_rvtt_sfpreadlreg6 (value);
    case 7: return gen_rvtt_sfpreadlreg7 (value);
    default: gcc_unreachable ();
    }
}

/* Emit a sentinel read of LREG REGNO defining VALUE just after insn
   AFTER; returns the emitted insn.  */

static rtx_insn *
emit_sentinel_after (unsigned regno, rtx value, rtx_insn *after)
{
  return emit_insn_after (make_sentinel (regno, value), after);
}

/* Emit a sentinel read of LREG REGNO defining VALUE just before insn
   BEFORE; returns the emitted insn.  */

static rtx_insn *
emit_sentinel_before (unsigned regno, rtx value, rtx_insn *before)
{
  return emit_insn_before (make_sentinel (regno, value), before);
}

/* Make an opaque raw instruction's architectural write visible at its effect
   marker.  Unlike a sentinel interval, this point clobber is required even
   when the raw result is dead: IRA must not allocate an unrelated value that
   is live across the instruction to the overwritten LREG.  */

static void
emit_clobber_before (unsigned regno, rtx_insn *before)
{
  rtx reg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + regno);
  emit_insn_before (gen_rtx_CLOBBER (VOIDmode, reg), before);
}

/* Terminate a sentinel interval: emit a USE of VALUE just before
   BEFORE, so the reserved LREG stays live up to that point.  A null
   VALUE (no interval open) is a no-op.  */

static void
end_sentinel (rtx value, rtx_insn *before)
{
  if (value)
    emit_insn_before (gen_rtx_USE (VOIDmode, value), before);
}

/* Keep VALUE live through the final instruction in BB.  A USE before a
   non-jump BB_END leaves that final instruction free to reuse VALUE's hard
   LREG, so append the USE and let emit_insn_after extend BB_END.  A jump may
   not be followed by an instruction in its block, but it cannot define an
   allocatable SFPU value, so a USE immediately before it is sufficient.  */
static void
end_sentinel_at_block_end (rtx value, basic_block bb)
{
  if (!value)
    return;

  rtx_insn *last = BB_END (bb);
  if (JUMP_P (last))
    end_sentinel (value, last);
  else
    emit_insn_after (gen_rtx_USE (VOIDmode, value), last);
}

/* Dataflow transfer over BB: LIVE is the block-entry bitmask of LREGs
   holding a raw (RTL-invisible) value, bit N for LREG N.  A raw-access
   access marker clears its release mask and sets its write mask.  Effect
   reads and writes are handled solely by the backward demand problem below,
   so a dead per-instruction output does not become a function-wide ownership
   interval.  A read or write metadata builtin for LREG N clears bit N
   (from that point the value has visible RTL).  Returns the block-exit mask.  */

static unsigned
transfer_block (basic_block bb, unsigned live)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      unsigned releases, writes;
      if (raw_access_p (insn, &releases, &writes))
	live = (live & ~releases) | writes;
      else
	{
	  int regno = read_lregno (insn);
	  if (regno < 0)
	    regno = write_lregno (insn);
	  if (regno >= 0)
	    live &= ~(1u << regno);
	}
    }
  return live;
}

/* Backward demand transfer.  Effect reads are uses and effect writes are
   definitions.  A typed LREG read is likewise a use of the architectural
   value; a typed write defines it.  Access releases and writes are explicit
   boundaries and therefore stop a later demand from flowing farther back.  */

static unsigned
transfer_demand_block (basic_block bb, unsigned live)
{
  for (rtx_insn *insn = BB_END (bb);; insn = PREV_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  unsigned first, writes;
	  if (raw_effect_p (insn, &first, &writes))
	    live = (live & ~writes) | first;
	  else if (raw_access_p (insn, &first, &writes))
	    live &= ~(first | writes);
	  else
	    {
	      int regno = read_lregno (insn);
	      if (regno >= 0)
		live |= 1u << regno;
	      else if ((regno = write_lregno (insn)) >= 0)
		live &= ~(1u << regno);
	    }
	}
      if (insn == BB_HEAD (bb))
	break;
    }
  return live;
}

/* Backward demand originating only in raw-effect reads.  Unlike the combined
   problem above, this may justify a function-entry reservation without a
   reaching raw definition in the function.  Typed reads and writes are
   boundaries: crossing back into raw code requires a new typed write.  */

static unsigned
transfer_effect_demand_block (basic_block bb, unsigned live)
{
  for (rtx_insn *insn = BB_END (bb);; insn = PREV_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  unsigned first, writes;
	  if (raw_effect_p (insn, &first, &writes))
	    live = (live & ~writes) | first;
	  else if (raw_access_p (insn, &first, &writes))
	    live &= ~(first | writes);
	  else
	    {
	      int regno = read_lregno (insn);
	      if (regno < 0)
		regno = write_lregno (insn);
	      if (regno >= 0)
		live &= ~(1u << regno);
	    }
	}
      if (insn == BB_HEAD (bb))
	break;
    }
  return live;
}

/* Forward provenance: bit N means the current architectural value in LREG N
   was defined by raw code.  This is deliberately not liveness; it gates
   typed-read demand so an ordinary sfpreadlreg at function entry does not
   manufacture a reservation.  */

static unsigned
transfer_provenance_block (basic_block bb, unsigned live)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      unsigned first, writes;
      if (raw_effect_p (insn, &first, &writes))
	live |= writes;
      else if (raw_access_p (insn, &first, &writes))
	live = (live & ~first) | writes;
      else
	{
	  int regno = read_lregno (insn);
	  if (regno < 0)
	    regno = write_lregno (insn);
	  if (regno >= 0)
	    live &= ~(1u << regno);
	}
    }
  return live;
}

/* Pass body over FN.  First solve a forward dataflow fixed point over
   the per-block raw-liveness masks; then, per block, materialize each
   raw interval as a sentinel read of its hard LREG -- before the first real
   insn for values live on entry, after the raw write otherwise -- and end it
   with a USE at the consuming builtin, the releasing or rewriting raw access,
   or block end.  Each block gets its own local interval, deliberately avoiding
   any cross-CFG pseudo or phi.  */

static void
make_raw_lregs_live (function *fn)
{
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  auto_vec<unsigned> in (n_bbs), out (n_bbs);
  in.safe_grow_cleared (n_bbs);
  out.safe_grow_cleared (n_bbs);

  bool changed;
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_in = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    next_in |= out[e->src->index];
	  unsigned next_out = transfer_block (bb, next_in);
	  if (next_in != in[bb->index] || next_out != out[bb->index])
	    {
	      in[bb->index] = next_in;
	      out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  /* Track whether a raw definition reaches each point.  Typed-read demand is
     admitted only where this provenance exists; raw-effect demand has its own
     entry-capable backward problem below.  */
  auto_vec<unsigned> provenance_in (n_bbs), provenance_out (n_bbs);
  provenance_in.safe_grow_cleared (n_bbs);
  provenance_out.safe_grow_cleared (n_bbs);
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_in = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    next_in |= provenance_out[e->src->index];
	  unsigned next_out = transfer_provenance_block (bb, next_in);
	  if (next_in != provenance_in[bb->index]
	      || next_out != provenance_out[bb->index])
	    {
	      provenance_in[bb->index] = next_in;
	      provenance_out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  /* Solve the complementary backward problem.  A per-instruction effect is
     useful only if its input stays reserved all the way from the reaching
     definition (including a typed sfpwritelreg) or function entry.  */
  auto_vec<unsigned> demand_in (n_bbs), demand_out (n_bbs);
  demand_in.safe_grow_cleared (n_bbs);
  demand_out.safe_grow_cleared (n_bbs);
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_out = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    next_out |= demand_in[e->dest->index];
	  unsigned next_in = transfer_demand_block (bb, next_out);
	  if (next_in != demand_in[bb->index]
	      || next_out != demand_out[bb->index])
	    {
	      demand_in[bb->index] = next_in;
	      demand_out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  auto_vec<unsigned> effect_in (n_bbs), effect_out (n_bbs);
  effect_in.safe_grow_cleared (n_bbs);
  effect_out.safe_grow_cleared (n_bbs);
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned next_out = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    next_out |= effect_in[e->dest->index];
	  unsigned next_in = transfer_effect_demand_block (bb, next_out);
	  if (next_in != effect_in[bb->index]
	      || next_out != effect_out[bb->index])
	    {
	      effect_in[bb->index] = next_in;
	      effect_out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  const unsigned max_uid = get_max_uid ();
  auto_vec<unsigned> demand_after (max_uid);
  demand_after.safe_grow_cleared (max_uid);
  auto_vec<unsigned> effect_after (max_uid);
  effect_after.safe_grow_cleared (max_uid);
  auto_vec<unsigned> provenance_after (max_uid);
  provenance_after.safe_grow_cleared (max_uid);
  basic_block demand_bb;
  FOR_EACH_BB_FN (demand_bb, fn)
    {
      unsigned live = demand_out[demand_bb->index];
      for (rtx_insn *insn = BB_END (demand_bb);;
	   insn = PREV_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn))
	    {
	      gcc_assert ((unsigned) INSN_UID (insn) < max_uid);
	      demand_after[INSN_UID (insn)] = live;
	      unsigned first, writes;
	      if (raw_effect_p (insn, &first, &writes))
		live = (live & ~writes) | first;
	      else if (raw_access_p (insn, &first, &writes))
		live &= ~(first | writes);
	      else
		{
		  int regno = read_lregno (insn);
		  if (regno >= 0)
		    live |= 1u << regno;
		  else if ((regno = write_lregno (insn)) >= 0)
		    live &= ~(1u << regno);
		}
	    }
	  if (insn == BB_HEAD (demand_bb))
	    break;
	}
    }

  FOR_EACH_BB_FN (demand_bb, fn)
    {
      unsigned live = effect_out[demand_bb->index];
      for (rtx_insn *insn = BB_END (demand_bb);;
	   insn = PREV_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn))
	    {
	      effect_after[INSN_UID (insn)] = live;
	      unsigned first, writes;
	      if (raw_effect_p (insn, &first, &writes))
		live = (live & ~writes) | first;
	      else if (raw_access_p (insn, &first, &writes))
		live &= ~(first | writes);
	      else
		{
		  int regno = read_lregno (insn);
		  if (regno < 0)
		    regno = write_lregno (insn);
		  if (regno >= 0)
		    live &= ~(1u << regno);
		}
	    }
	  if (insn == BB_HEAD (demand_bb))
	    break;
	}
    }

  FOR_EACH_BB_FN (demand_bb, fn)
    {
      unsigned live = provenance_in[demand_bb->index];
      rtx_insn *insn;
      FOR_BB_INSNS (demand_bb, insn)
	if (NONDEBUG_INSN_P (insn))
	  {
	    unsigned first, writes;
	    if (raw_effect_p (insn, &first, &writes))
	      live |= writes;
	    else if (raw_access_p (insn, &first, &writes))
	      live = (live & ~first) | writes;
	    else
	      {
		int regno = read_lregno (insn);
		if (regno < 0)
		  regno = write_lregno (insn);
		if (regno >= 0)
		  live &= ~(1u << regno);
	      }
	    provenance_after[INSN_UID (insn)] = live;
	  }
    }

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx live[8] = {};
      rtx_insn *producer[8] = {};
      rtx_insn *first = NULL;
      for (rtx_insn *insn = BB_HEAD (bb);; insn = NEXT_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn))
	    {
	      first = insn;
	      break;
	    }
	  if (insn == BB_END (bb))
	    break;
	}
      if (!first)
	continue;

      unsigned owned = in[bb->index];
      unsigned entry_demand
	= effect_in[bb->index]
	  | (demand_in[bb->index] & provenance_in[bb->index]);
      for (unsigned regno = 0; regno != 8; ++regno)
	if ((owned | entry_demand) & (1u << regno))
	  {
	    rtx value = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + regno);
	    live[regno] = value;
	    producer[regno] = emit_sentinel_before (regno, value, first);
	  }

      for (rtx_insn *insn = BB_HEAD (bb), *next;
	   insn != NEXT_INSN (BB_END (bb)); insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  unsigned releases, writes;
	  if (raw_effect_p (insn, &releases, &writes))
	    {
	      /* Backward demand normally made each input live from its reaching
		 definition.  Keep a local fallback for malformed or newly split RTL:
		 the marker must at minimum reserve the preceding raw instruction.  */
	      rtx_insn *raw_insn = PREV_INSN (insn);
	      while (raw_insn && !NONDEBUG_INSN_P (raw_insn))
		raw_insn = PREV_INSN (raw_insn);
	      if (!raw_insn || BLOCK_FOR_INSN (raw_insn) != bb)
		raw_insn = insn;

	      for (unsigned regno = 0; regno != 8; ++regno)
		if ((releases & (1u << regno)) && !live[regno])
		  {
		    rtx value = gen_rtx_REG (XTT32SImode,
					     SFPU_REG_FIRST + regno);
		    producer[regno]
		      = emit_sentinel_before (regno, value, raw_insn);
		    live[regno] = value;
		  }

	      unsigned after
		= owned | effect_after[INSN_UID (insn)]
		  | (demand_after[INSN_UID (insn)]
		     & provenance_after[INSN_UID (insn)]);

	      /* A write supersedes the old raw value after the opaque instruction,
		 including the read/write case.  Other inputs end here only when this
		 is their last demanded use and no forward ownership remains.  */
	      for (unsigned regno = 0; regno != 8; ++regno)
		if ((writes & (1u << regno)) || !(after & (1u << regno)))
		  {
		    end_sentinel (live[regno], insn);
		    live[regno] = NULL_RTX;
		    producer[regno] = NULL;
		  }

	      /* Point-clobber every raw output independently of output liveness.
		 This protects unrelated typed values that span a dead raw write.  */
	      for (unsigned regno = 0; regno != 8; ++regno)
		if (writes & (1u << regno))
		  emit_clobber_before (regno, insn);

	      /* A demanded raw output starts a fresh reservation after the marker.  */
	      for (unsigned regno = 0; regno != 8; ++regno)
		if ((writes & after) & (1u << regno))
		  {
		    rtx value = gen_rtx_REG (XTT32SImode,
					     SFPU_REG_FIRST + regno);
		    producer[regno] = emit_sentinel_after (regno, value, insn);
		    live[regno] = value;
		  }
	      continue;
	    }

	  if (raw_access_p (insn, &releases, &writes))
	    {
	      owned = (owned & ~releases) | writes;
	      unsigned after
		= owned | effect_after[INSN_UID (insn)]
		  | (demand_after[INSN_UID (insn)]
		     & provenance_after[INSN_UID (insn)]);
	      for (unsigned regno = 0; regno != 8; ++regno)
		if ((releases | writes) & (1u << regno))
		  {
		    end_sentinel (live[regno], insn);
		    live[regno] = NULL_RTX;
		    producer[regno] = NULL;
		    if (after & (1u << regno))
		      {
			rtx value = gen_rtx_REG (XTT32SImode,
						 SFPU_REG_FIRST + regno);
			producer[regno]
			  = emit_sentinel_after (regno, value, insn);
			live[regno] = value;
		      }
		  }
	      continue;
	    }

	  int regno = read_lregno (insn);
	  if (regno < 0)
	    regno = write_lregno (insn);
	  if (regno >= 0)
	    {
	      if (insn == producer[regno])
		continue;
	      end_sentinel (live[regno], insn);
	      live[regno] = NULL_RTX;
	      producer[regno] = NULL;
	      owned &= ~(1u << regno);
	      unsigned after
		= effect_after[INSN_UID (insn)]
		  | (demand_after[INSN_UID (insn)]
		     & provenance_after[INSN_UID (insn)]);
	      if (after & (1u << regno))
		{
		  rtx value = gen_rtx_REG (XTT32SImode,
					   SFPU_REG_FIRST + regno);
		  producer[regno] = emit_sentinel_after (regno, value, insn);
		  live[regno] = value;
		}
	    }
	}

      /* A successor gets its own entry interval.  This endpoint keeps the local
	 interval alive through all instructions in the current block without
	 inventing a cross-CFG pseudo or phi.  */
      for (unsigned regno = 0; regno != 8; ++regno)
	if (live[regno])
	  end_sentinel_at_block_end (live[regno], bb);
    }
}

const pass_data pass_data_rvtt_lreg_livein =
{
  RTL_PASS, "rvtt_lreg_livein", OPTGROUP_OTHER, TV_NONE,
  0, 0, 0, 0, TODO_df_finish
};

class pass_rvtt_lreg_livein : public rtl_opt_pass
{
public:
  pass_rvtt_lreg_livein (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lreg_livein, ctxt) {}

  bool gate (function *) final override { return TARGET_XTT_TENSIX; }

  unsigned execute (function *fn) final override
  {
    make_raw_lregs_live (fn);
    return 0;
  }
};

} /* anonymous namespace */

/* Instantiate the raw-LREG reservation pass for CTXT.  The pass runs
   immediately before IRA and is a correctness requirement rather than an
   optional optimization.  */

rtl_opt_pass *
make_pass_rvtt_lreg_livein (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_livein (ctxt);
}
