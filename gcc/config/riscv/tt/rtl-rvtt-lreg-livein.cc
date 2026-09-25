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

/* No flag.  The gate is TARGET_XTT_TENSIX alone, so this pass is
   ALWAYS ON for every Tensix compilation, at every optimization level
   including -O0.  That is deliberate and it is not an optimization
   decision: what this pass does is make a piece of the machine state
   visible to the register allocator, and an allocator that cannot see
   a reservation does not allocate conservatively -- it allocates
   wrongly.

   THE HOLE.  Hand-written LLK code reaches the compiler as raw
   .ttinsn words.  An SFPU instruction spelled that way reads and
   writes architectural L-registers, but its RTL carries no def and no
   use of them: to GCC's dataflow the words are opaque.  So after a
   raw SFPLOAD into L1, nothing tells IRA that L1 is spoken for, and
   IRA is free -- correctly, by its own information -- to hand L1 to
   the next vFloat temporary.  The raw code's value is then silently
   overwritten.  This is wrong code with no diagnostic: the LLK
   sequence and the compiler-generated sequence each look locally
   right.

   THE FILLING.  Metadata builtins (rvtt_sfpreadlreg<N> /
   rvtt_sfpwritelreg<N>, and the rvtt_sfprawlreg_access marker with its
   release and write masks) name the raw accesses.  This pass turns
   each raw ownership interval into an interval IRA can see:

     1. A forward dataflow fixed point over per-block eight-bit masks
	(bit N = LREG N currently holds a raw, RTL-invisible value).
	A raw-access marker clears its release mask and sets its write
	mask; a read or write metadata builtin for LREG N clears bit N,
	because from that point the value has visible RTL.

     2. Per block, every interval open at entry or opened by a raw
	write is materialized as a fresh XTT32SImode pseudo defined by
	a SENTINEL: a zero-length fixed-register read of that LREG
	(gen_rvtt_sfpreadlreg<N>).  The interval is closed with a plain
	USE of the pseudo at the consuming builtin, at the releasing or
	rewriting raw access, or at block end.  A zero-length
	fixed-register def/use pair is exactly what IRA understands as
	"this hard register is occupied here".

     3. Joins get a FRESH LOCAL TOKEN rather than a shared pseudo:
	every block materializes its own pseudo for a value it
	inherits.  This deliberately avoids inventing a cross-CFG
	pseudo or a phi for a value the compiler does not actually
	own, and is conservative in the right direction -- it reserves
	the LREG in every block on the path without claiming to know
	where the value came from.

   Sentinels and USEs deliver NO instruction words; the emitted object
   is unchanged by their presence, and the whole effect of the pass is
   on what IRA and the pre-IRA allocator stack are allowed to believe.

   WHAT BREAKS IF THIS PASS IS DROPPED.  Two things, both silent:

     - IRA reuses a raw-owned LREG for a compiler temporary and the
       LLK value is destroyed.  There is no error and no refusal: the
       first symptom is wrong numerics in a kernel.

     - rtl-rvtt-lp-alloc.cc loses its precolored nodes.  Its
       interference graph is built over XTT32SI pseudo webs AFTER this
       pass has materialized every raw reservation, precisely so that
       raw reservations participate as ordinary precolored nodes; drop
       them and DSATUR colours a graph that omits real interference
       and certifies as 8-colorable a function that is not.  The
       colorability certificate would then be false, which is worse
       than absent.

   The pass sits before ira in rvtt-passes.def, ahead of
   rvtt_lp_alloc.  It is unconditional, it never refuses, and there is
   no flag to turn it off.

   LINEAGE.
     technique  none.  Reserving an architecturally-owned register by
                synthesizing a zero-length def/use interval for it has
                no published antecedent worth citing; it is a local
                idiom, not an adapted result, and stretching it onto a
                register-allocation paper would misdescribe both.
     modelled on  none, and the reason is that the two spellings
                generic GCC does offer are each wrong here.  Marking
                the LREG global (gcc/reginfo.cc: global_regs, and the
                fixed-register machinery beside it) removes it from
                allocation for the WHOLE translation unit, which costs
                the register in every function whether or not any raw
                word touches it -- out of eight, unaffordable.
                Emitting a CLOBBER at the raw site
                (gcc/emit-rtl.cc: emit_clobber) marks a POINT,
                not an interval, so it says nothing about the span
                between the raw producer and the raw consumer, which
                is exactly the span that must stay reserved.  GCC has
                no generic "this hard register is externally owned
                between here and there" construct; an interval built
                from a real def and a real use is the construct it
                does have, and that is what this pass emits.

   HARDWARE.  The eight architectural SFPU vector registers L0-L7
   (riscv.h SFPU_REG_NUM), which are a SHARED resource: hand-written
   LLK raw .ttinsn words and compiler-allocated vector values live in
   the same eight names, in the same two banks (L0-L3 / L4-L7) the IRA
   dual-bank binding describes, with no memory spill path to relieve
   the contention.  The cost is paid entirely in LREG live ranges --
   up to eight per block, exactly the ones raw code already owns --
   and in nothing else: every sentinel read and every closing USE is
   zero-length, so delivered words, issue slots and the emitted object
   are unchanged.  The pass makes the register file smaller as seen by
   the allocator, which is the point; it does not make the program
   bigger.
     - eight allocatable LREGs      riscv.h SFPU_REG_NUM
     - raw words are DF-opaque      raw .ttinsn has no RTL def or use;
                                    the metadata builtins
                                    rvtt_sfpreadlreg<N> /
                                    rvtt_sfpwritelreg<N> and
                                    UNSPECV_SFPRAWLREG_ACCESS are the
                                    only naming
     - sentinels deliver no word    zero-length fixed-register reads;
                                    closings are plain USEs
     - downstream consumer          rtl-rvtt-lp-alloc.cc builds its
                                    interference graph after this pass
                                    so raw reservations are precolored
                                    nodes

   BIRTH KERNEL.  None, and none is possible: the pass has NO flag, so
   it has no FIRE-BREADTH.tsv row, no birth row and no birth_share --
   it is not a fire that can be attributed to a kernel, it is a
   standing correctness condition.  What the tree records instead is
   thin and worth stating plainly: ONE test,
   g++.target/riscv/tt/tensix/raw-lreg-livein-cfg-wh.C, in a
   1530-test suite.  That is a coverage fact, not a benefit
   measurement, and it is a small belt for a pass whose failure mode
   is silent wrong code in every kernel that mixes raw LLK words with
   compiler-allocated vectors.  The real load is carried by the
   downstream consumer's own tests (the lregalloc/ precolored-node
   rows) and by the reference-simulator bit-exactness gate on
   newly-compiling kernels.  */

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
   fixed-register read creates a normal IRA interval.  The artificial value is
   used at each local endpoint; joins get a fresh local token, deliberately
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

/* RTL for a sentinel read of hard LREG REGNO defining the pseudo VALUE:
   a zero-length fixed-register read whose interval is what makes IRA
   reserve the LREG.  */

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
   marker clears its release mask and sets its write mask; a read or
   write metadata builtin for LREG N clears bit N (from that point the
   value has visible RTL).  Returns the block-exit mask.  */

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

/* Pass body over FN.  First solve a forward dataflow fixed point over
   the per-block raw-liveness masks; then, per block, materialize each
   raw interval as a fresh XTT32SImode pseudo defined by a sentinel read
   of its LREG -- before the first real insn for values live on entry,
   after the raw write otherwise -- and end it with a USE at the
   consuming builtin, the releasing or rewriting raw access, or block
   end.  Each block gets its own local pseudo for a value it inherits,
   deliberately avoiding any cross-CFG pseudo or phi.  */

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

      for (unsigned regno = 0; regno != 8; ++regno)
        if (in[bb->index] & (1u << regno))
          {
            rtx value = gen_reg_rtx (XTT32SImode);
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
	  if (raw_access_p (insn, &releases, &writes))
            {
              for (unsigned regno = 0; regno != 8; ++regno)
		if (releases & (1u << regno))
                  {
                    end_sentinel (live[regno], insn);
                    live[regno] = NULL_RTX;
                    producer[regno] = NULL;
                  }
              for (unsigned regno = 0; regno != 8; ++regno)
                if (writes & (1u << regno))
                  {
                    end_sentinel (live[regno], insn);
                    rtx value = gen_reg_rtx (XTT32SImode);
                    producer[regno] = emit_sentinel_after (regno, value, insn);
                    live[regno] = value;
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
            }
        }

      /* A successor gets its own entry token.  This endpoint keeps the local
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
    return TODO_df_finish;
  }
};

} /* anonymous namespace */

/* Instantiate the raw-LREG reservation pass for CTXT; rvtt-passes.def
   places it before ira, ahead of rvtt_lp_alloc, and it gates on the
   Tensix extension.  */

rtl_opt_pass *
make_pass_rvtt_lreg_livein (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_livein (ctxt);
}
