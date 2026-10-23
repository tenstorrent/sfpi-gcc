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

/* Raw instructions emitted as .ttinsn words (as in library LLK code)
   have no RTL definitions or uses of the L-registers they access.  A raw
   SFPLOAD may leave a value in L1 for a later raw instruction, and in
   between IRA sees nothing live in L1 and may give it to a vFloat
   temporary, silently clobbering the raw value.  This pass makes those
   accesses visible to IRA.  It is a correctness requirement, so it runs at
   every optimization level and has no flag.

   Callers describe raw accesses with metadata builtins; this pass does not
   decode arbitrary inline asm.  sfprawlreg_effect gives the read and write
   masks of the immediately preceding raw instruction; sfprawlreg_access
   marks explicit ownership boundaries (release and write masks).  A
   forward problem carries the explicitly owned LREGs across the CFG; a
   backward problem carries effect reads back to their reaching definition
   or function entry, treating effect writes as definitions so that dead
   raw outputs do not hold an LREG.  A typed sfpreadlreg<N> or
   sfpwritelreg<N> ends the raw reservation of LREG N.

   A raw instruction writes only the enabled lanes; the others keep the old
   value, which is then also an input of the write if a later read may see
   one of those lanes enabled again.  A forward must-problem finds where all
   lanes are known to be enabled (after all-column flags and a clear
   LaneConfig ROW_MASK have both been established), and a backward problem finds reads
   that may follow a lane re-enable.  Headers that declare only true reads
   (see __riscv_xtt_sfprawlreg_merge) then reserve the old value only where
   a disabled lane can leak it.

   Each block materializes its incoming reservations and demanded raw
   writes as zero-length reads of the hard LREG (a sentinel), kept live by
   a USE at the consumer, a release or rewrite marker, or the block end.
   Each block gets its own local interval, so no cross-block pseudo or phi
   is needed.  A point clobber alone would not protect the gap between
   producer and consumer, and fixing the register outright would remove it
   from allocation everywhere.  The sentinels and USEs emit no code.  */

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
#include "df.h"
#include "function.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-rtl-lane-state.h"

namespace {

/* The L-register number read when INSN is one of the rvtt_sfpreadlreg<N>
   builtins, -1 otherwise.  */

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

/* The architectural L-register number written by an explicit hard-register
   copy or rvtt_sfpwritelreg<N>, -1 otherwise.  Like a typed read,
   a typed definition ends the preceding raw interval on that LREG.  */

static int
write_lregno (rtx_insn *insn)
{
  rtx set = single_set (insn);
  if (TARGET_XTT_TENSIX_WH_BH && set
      && REG_P (SET_DEST (set)) && REG_P (SET_SRC (set))
      && GET_MODE (SET_DEST (set)) == XTT32SImode
      && REGNO (SET_DEST (set)) >= SFPU_REG_FIRST
      && REGNO (SET_DEST (set)) < SFPU_REG_FIRST + 8)
    return REGNO (SET_DEST (set)) - SFPU_REG_FIRST;
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

/* Return true when INSN is the rvtt_sfprawlreg_effect marker, setting
   *READ_MASK to the inputs and *WRITE_MASK to the outputs of the
   immediately preceding raw instruction.  */

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

  /* A header-side decoder fed an instruction word containing a runtime
     value produces non-constant masks.  Treat them as every LREG, and
     replace the operands so no runtime mask computation survives.  */
  if (!CONST_INT_P (read) || !CONST_INT_P (write))
    {
      XVECEXP (pat, 0, 0) = GEN_INT (*read_mask);
      XVECEXP (pat, 0, 1) = GEN_INT (*write_mask);
      INSN_CODE (insn) = -1;
      /* DF use locations still point into the old operand vector.  Without a
	 rescan, its note problem can attach REG_DEAD to the new CONST_INT;
	 IRA subsequently interprets that constant as a register number.  */
      df_insn_rescan (insn);
    }
  return true;
}

/* For each effect marker (by UID), the written LREGs whose old value is an
   input because a disabled lane may leak it.  Filled before any demand is
   solved.  */

static vec<unsigned> effect_merges;

/* For each effect marker (by UID), the declared reads that no instruction
   observes: SFPLOADI writing one half of an LREG keeps the other half, but
   when the next instruction loads that other half the old value is gone.  */

static vec<unsigned> effect_dead_reads;

/* The inputs of the raw instruction described by effect marker INSN, which
   reads READS and writes WRITES.  */

static unsigned
effect_inputs (rtx_insn *insn, unsigned reads, unsigned writes)
{
  return ((reads & ~effect_dead_reads[INSN_UID (insn)])
	  | (writes & effect_merges[INSN_UID (insn)]));
}

/* The raw instruction an effect marker describes: the preceding
   non-debug instruction in its block, or null.  A BARRIER ends the
   predecessor walk and has no BLOCK_FOR_INSN field to inspect.  */

static rtx_insn *
marker_raw_insn (rtx_insn *marker)
{
  for (rtx_insn *insn = PREV_INSN (marker);
       insn && !BARRIER_P (insn)
	 && BLOCK_FOR_INSN (insn) == BLOCK_FOR_INSN (marker);
       insn = PREV_INSN (insn))
    if (NONDEBUG_INSN_P (insn))
      return insn;
  return NULL;
}

/* Fill EFFECT_DEAD_READS for FN: an SFPLOADI that writes one half of an
   LREG (UPPER, mod0 8, or LOWER, mod0 10; see SFPLOADI.md) reads the other
   half, but if the very next instruction is an SFPLOADI of the other half
   to the same LREG, both halves are new before anything reads them.  Lanes
   cannot change between the two loads, so the lane analysis still decides
   whether disabled lanes keep the old value.  */

static void
find_dead_half_reads (function *fn)
{
  effect_dead_reads.safe_grow_cleared (get_max_uid ());
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *half = NULL;
      unsigned half_lreg = 0, half_mod = 0;
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  unsigned reads, writes;
	  if (!raw_effect_p (insn, &reads, &writes))
	    continue;
	  rtx_insn *raw = marker_raw_insn (insn);
	  uint32_t word = 0;
	  bool loadi = (raw && rvtt_rtl_raw_insn_fields (raw, &word)
			&& (word >> 24) == 0x71);
	  unsigned lreg = (word >> 20) & 0xf, mod = (word >> 16) & 0xf;
	  if (loadi && half && marker_raw_insn (raw) == half
	      && lreg == half_lreg && lreg < 8 && mod + half_mod == 18
	      && (mod == 8 || mod == 10))
	    effect_dead_reads[INSN_UID (half)] |= 1u << lreg;
	  half = loadi && (mod == 8 || mod == 10) ? insn : NULL;
	  half_lreg = lreg;
	  half_mod = mod;
	}
    }
}

/* Fill EFFECT_MERGES for FN.  ALL_LANES collects the UIDs of the effect
   markers and typed LREG writes that issue with every lane enabled; the
   backward problem then keeps an old value as an input only for the other
   writes, and only for the LREGs a later read may see in a re-enabled
   lane.  */

static void
find_lane_merges (function *fn)
{
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  effect_merges.safe_grow_cleared (get_max_uid ());

  rvtt_rtl_lane_facts lanes (fn);

  /* Forward: LREGs some definition in FN may reach.  A disabled lane of
     an LREG nothing in FN defined holds no value anyone can rely on, so only
     a defined old value is worth keeping.  A call may define any LREG.  */
  auto defined_after = [&] (rtx_insn *insn, unsigned defined)
    {
      unsigned reads, writes;
      int regno;
      if (CALL_P (insn))
	return 0xffu;
      if (raw_effect_p (insn, &reads, &writes)
	  || raw_access_p (insn, &reads, &writes))
	return defined | writes;
      if ((regno = write_lregno (insn)) >= 0)
	return defined | (1u << regno);
      return defined;
    };
  bool changed;
  auto_vec<unsigned> defined_out (n_bbs);
  defined_out.safe_grow_cleared (n_bbs);
  auto entry_defined = [&] (basic_block bb)
    {
      unsigned defined = 0;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->preds)
	defined |= defined_out[e->src->index];
      return defined;
    };
  do
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  unsigned defined = entry_defined (bb);
	  rtx_insn *insn;
	  FOR_BB_INSNS (bb, insn)
	    if (NONDEBUG_INSN_P (insn))
	      defined = defined_after (insn, defined);
	  if (defined != defined_out[bb->index])
	    {
	      defined_out[bb->index] = defined;
	      changed = true;
	    }
	}
    }
  while (changed);

  auto_bitmap all_lanes_uids;
  auto_vec<unsigned> defined_at (get_max_uid ());
  defined_at.safe_grow_cleared (get_max_uid ());
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {

      unsigned defined = entry_defined (bb);
      bool before_prev = false;
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  unsigned reads, writes;
	  bool marker = raw_effect_p (insn, &reads, &writes);
	  if (marker
	      ? marker_raw_insn (insn) && before_prev
	      : write_lregno (insn) >= 0 && lanes.before (insn) == RVTT_RTL_ALL_LANES)
	    bitmap_set_bit (all_lanes_uids, INSN_UID (insn));
	  if (marker)
	    defined_at[INSN_UID (insn)] = defined;
	  before_prev = lanes.before (insn) == RVTT_RTL_ALL_LANES;
	  defined = defined_after (insn, defined);
	}
    }

  /* Backward: NARROW holds LREGs whose current value is read later with no
     lane re-enabled in between, WIDE those it may be read after a re-enable
     (or by something we cannot follow).  */
  auto_vec<unsigned> narrow_in (n_bbs), wide_in (n_bbs);
  narrow_in.safe_grow_cleared (n_bbs);
  wide_in.safe_grow_cleared (n_bbs);
  auto transfer = [&] (basic_block bb, unsigned &narrow, unsigned &wide,
		       bool record)
    {
      rtx_insn *raw_done = NULL;
      for (rtx_insn *insn = BB_END (bb);; insn = PREV_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn) && insn != raw_done)
	    {
	      unsigned reads, writes;
	      int regno;
	      rvtt_rtl_lane_effect effect = LANES_KEPT;
	      if (raw_effect_p (insn, &reads, &writes))
		{
		  raw_done = marker_raw_insn (insn);
		  effect = raw_done ? rvtt_rtl_lane_effect_of (raw_done) : LANES_ANY;
		  if (effect != LANES_KEPT)
		    {
		      wide |= narrow;
		      narrow = 0;
		    }
		  bool full = bitmap_bit_p (all_lanes_uids, INSN_UID (insn));
		  if (record)
		    {
		      effect_merges[INSN_UID (insn)]
			= full ? 0 : writes & wide & defined_at[INSN_UID (insn)];
		      if (dump_file && effect_merges[INSN_UID (insn)])
			fprintf (dump_file, "Lane merge: insn %d keeps the old"
				 " value of LREG mask 0x%02x\n", INSN_UID (insn),
				 effect_merges[INSN_UID (insn)]);
		    }
		  narrow &= ~writes;
		  if (full)
		    wide &= ~writes;
		  narrow |= reads & ~effect_dead_reads[INSN_UID (insn)];
		}
	      else if (raw_access_p (insn, &reads, &writes))
		narrow &= ~writes;
	      else if ((regno = read_lregno (insn)) >= 0)
		wide |= 1u << regno;
	      else if ((regno = write_lregno (insn)) >= 0)
		{
		  narrow &= ~(1u << regno);
		  if (bitmap_bit_p (all_lanes_uids, INSN_UID (insn)))
		    wide &= ~(1u << regno);
		}
	      else if (CALL_P (insn))
		{
		  narrow = 0;
		  wide = 0xff;
		}
	      else if (rvtt_rtl_lane_effect_of (insn) != LANES_KEPT)
		{
		  wide |= narrow;
		  narrow = 0;
		}
	    }
	  if (insn == BB_HEAD (bb))
	    break;
	}
    };
  do
    {
      changed = false;
      FOR_EACH_BB_REVERSE_FN (bb, fn)
	{
	  unsigned narrow = 0, wide = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->dest != EXIT_BLOCK_PTR_FOR_FN (fn))
	      {
		narrow |= narrow_in[e->dest->index];
		wide |= wide_in[e->dest->index];
	      }
	  transfer (bb, narrow, wide, false);
	  if (narrow != narrow_in[bb->index] || wide != wide_in[bb->index])
	    {
	      narrow_in[bb->index] = narrow;
	      wide_in[bb->index] = wide;
	      changed = true;
	    }
	}
    }
  while (changed);

  FOR_EACH_BB_FN (bb, fn)
    {
      unsigned narrow = 0, wide = 0;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest != EXIT_BLOCK_PTR_FOR_FN (fn))
	  {
	    narrow |= narrow_in[e->dest->index];
	    wide |= wide_in[e->dest->index];
	  }
      transfer (bb, narrow, wide, true);
    }
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
	    live = (live & ~writes) | effect_inputs (insn, first, writes);
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
	    live = (live & ~writes) | effect_inputs (insn, first, writes);
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

/* Solve the dataflow problems for FN, then in each block start a sentinel
   for every reserved LREG (before the first insn for values live on entry,
   after the raw write otherwise) and end it with a USE at the consumer, a
   release or rewrite marker, or the block end.  */

static void
make_raw_lregs_live (function *fn)
{
  find_dead_half_reads (fn);
  find_lane_merges (fn);

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
		live = (live & ~writes) | effect_inputs (insn, first, writes);
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
		live = (live & ~writes) | effect_inputs (insn, first, writes);
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
	      releases = effect_inputs (insn, releases, writes);
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
	  /* A real hard-register definition already supplies the value.
	     A fresh zero-word sentinel SET would redefine it in RTL and
	     let DCE delete the required hardware copy.  Its later USE
	     must instead keep this actual definition live.  Opaque typed
	     markers still need a synthetic definition for their output.  */
	  rtx set = single_set (insn);
	  if (set && rtx_equal_p (SET_DEST (set), value)
	      && REG_P (SET_SRC (set)))
	    producer[regno] = insn;
	  else
	    producer[regno] = emit_sentinel_after (regno, value, insn);
		  live[regno] = value;
		}
	    }
	}

      /* Keep the local interval live to the end of the block; each
	 successor starts its own.  */
      for (unsigned regno = 0; regno != 8; ++regno)
	if (live[regno])
	  end_sentinel_at_block_end (live[regno], bb);
    }
  effect_merges.release ();
  effect_dead_reads.release ();
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
    /* Value-form SFPCONFIG reads L0.  Its fixed input constraint would
       otherwise hide the architectural assignment until LRA inserts a
       copy, after raw ownership has already been solved.  Expose exactly
       that transfer now so a later raw read observes the newly assigned
       L0 rather than reserving its overwritten incoming value.  */
    basic_block bb;
    FOR_EACH_BB_FN (bb, fn)
      {
	rtx_insn *insn;
	FOR_BB_INSNS (bb, insn)
	  if (NONDEBUG_INSN_P (insn)
	      && recog_memoized (insn) == CODE_FOR_rvtt_sfpwriteconfig_v)
	    {
	      rtx &input = XVECEXP (PATTERN (insn), 0, 0);
	      rtx l0 = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST);
	      if (!rtx_equal_p (input, l0))
		{
		  rtx_insn *copy = emit_insn_before (gen_rtx_SET (l0, input), insn);
		  INSN_LOCATION (copy) = INSN_LOCATION (insn);
		  input = l0;
		  INSN_CODE (insn) = -1;
		  df_insn_rescan (copy);
		  df_insn_rescan (insn);
		}
	    }
      }
    make_raw_lregs_live (fn);
    return 0;
  }
};

} /* anonymous namespace */

/* Create the raw-LREG reservation pass.  */

rtl_opt_pass *
make_pass_rvtt_lreg_livein (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_livein (ctxt);
}
