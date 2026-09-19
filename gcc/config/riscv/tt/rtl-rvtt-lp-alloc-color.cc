/* Tensix LREG allocator: the graph-colouring core.

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

/* Split out of rtl-rvtt-lp-alloc.cc; see rtl-rvtt-lp-alloc-int.h for
   what crosses between the halves.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "rtl-iter.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "cfgloop.h"
#include "expr.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "diagnostic-core.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rtl-rvtt-lp-alloc-int.h"

namespace rvtt_lpa {

/* 32-bit Dst format class (simulator-audited; the same class
   gimple-rvtt-transp-involution.cc uses), minus mod0 10 (INT32_ALL)
   which masks the RWC base and is handled separately.  */

bool
dst_mode_32bit_p (HOST_WIDE_INT m)
{
  return m == 3 || m == 4 || m == 7 || m == 9 || m == 12;
}

/* -------------------------- spill legality -------------------------- */

/* CC lane-state lattice value (mirror of rtl-rvtt-dst-ownership.cc).  */
enum lpa_cc_val : uint8_t { LPA_CC_ALL, LPA_CC_OTHER, LPA_CC_UNPROVED };

constexpr unsigned LPA_CC_STACK_MAX = 16;

/* Abstract state at a program point: the CC lane state (with the
   explicit PUSHC/POPC stack, exactly the rtl-rvtt-dst-ownership.cc
   lattice) plus a SYMBOLIC Dst-counter position: an epoch token (base
   identity; joins that disagree mint the block's stable token -- the
   dst-ownership idiom) and a proven offset within the epoch (typed
   INC/FACE effects add their audited deltas).  Two points name the
   same physical row exactly when they share the epoch and the
   compensated immediates agree, so a spill at epoch-relative offset S
   uses immediate S - off at each insertion point; loop-variant bases
   (a row loop's dst_reg++) work because all in-body points share the
   header's epoch.  A value LIVE ACROSS a minted join (a loop-carried
   web crossing the rwc backedge) is never spilled: its base changes
   between instances.  An unproven counter effect (TTSETRWC, opacity)
   clears `known'.  */

struct lpa_state
{
  lpa_cc_val cc;
  uint8_t cc_depth;
  uint8_t cc_stack[LPA_CC_STACK_MAX];
  bool known;			/* epoch/offset proven */
  int epoch;			/* base-identity token; 0 = function entry */
  int off;			/* proven Dst-counter offset within the epoch */
  bool reached;

  static lpa_state entry ()
  {
    lpa_state s = {};
    s.cc = LPA_CC_ALL;
    s.known = true;
    s.reached = true;
    return s;
  }
  static lpa_state unreached ()
  {
    lpa_state s = {};
    s.reached = false;
    return s;
  }
  bool operator== (const lpa_state &o) const
  {
    if (reached != o.reached)
      return false;
    if (!reached)
      return true;
    if (cc != o.cc || cc_depth != o.cc_depth || known != o.known
	|| (known && (epoch != o.epoch || off != o.off)))
      return false;
    for (unsigned i = 0; i < cc_depth && i < LPA_CC_STACK_MAX; i++)
      if (cc_stack[i] != o.cc_stack[i])
	return false;
    return true;
  }
  bool operator!= (const lpa_state &o) const { return !(*this == o); }

  void poison_cc () { cc = LPA_CC_UNPROVED; cc_depth = 0; }
  void cc_push ()
  {
    if (cc == LPA_CC_UNPROVED)
      return;
    if (cc_depth >= LPA_CC_STACK_MAX)
      {
	poison_cc ();
	return;
      }
    cc_stack[cc_depth++] = cc;
  }
  void cc_pop ()
  {
    if (cc == LPA_CC_UNPROVED)
      return;
    if (cc_depth == 0)
      {
	poison_cc ();
	return;
      }
    cc = (lpa_cc_val) cc_stack[--cc_depth];
  }
};


/* Record in CTX the named spill-legality refusal NAME (with DETAIL and
   the blocking insn AT).  Only the first refusal is kept; later calls
   on a refused context are ignored.  */

void
lpa_refuse (spill_ctx &ctx, const char *name, const char *detail, rtx_insn *at)
{
  if (!ctx.ok)
    return;			/* keep the first refusal */
  ctx.ok = false;
  ctx.refusal = name;
  ctx.detail = detail;
  ctx.at = at;
}

/* Apply INSN's audited effects to S; when COLLECT is non-null, also
   record Dst rows, layout evidence, and hard refusals.  */

static void
lpa_transfer (lpa_state &s, rtx_insn *insn, spill_ctx *collect)
{
  int code = recog_memoized (insn);
  /* Zero-length LREG metadata: no Dst/RWC/config/CC effect; raw
     .ttinsn regions are asm and refuse as opaque on their own.  */
  if (sentinel_read_lregno (code) >= 0
      || sentinel_write_lregno (code) >= 0
      || code == CODE_FOR_rvtt_sfprawlreg_access)
    return;

  if (code == CODE_FOR_rvtt_sfppushc)
    {
      extract_insn (insn);
      bool plain = CONST_INT_P (recog_data.operand[0])
	&& INTVAL (recog_data.operand[0]) == 0;
      s.cc_push ();
      if (!plain)
	/* A mod-bearing PUSHC (e.g. replace) is a CC write of
	   unmodeled shape.  */
	s.cc = LPA_CC_OTHER;
      return;
    }
  if (code == CODE_FOR_rvtt_sfppopc)
    {
      s.cc_pop ();
      return;
    }
  if (code == CODE_FOR_rvtt_sfpcompc)
    {
      s.cc = LPA_CC_OTHER;
      return;
    }

  /* The predicated-assign copy: a pure CC-reading LREG move.  */
  {
    rtx pat = PATTERN (insn);
    if (GET_CODE (pat) == SET
	&& GET_CODE (SET_SRC (pat)) == UNSPEC_VOLATILE
	&& XINT (SET_SRC (pat), 1) == UNSPECV_SFPASSIGN)
      return;
  }

  {
    bool cc_writes;
    if (rvtt_lane_local_effects (insn, &cc_writes))
      {
	if (cc_writes)
	  /* Mod-conditional CC write (SFPEXEXP/SFPLZ/SFPIADD),
	     recorded conservatively: the lane state narrows.  */
	  s.cc = LPA_CC_OTHER;
	return;
      }
  }

  if (rvtt_pattern_transparent_p (insn))
    return;

  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    {
      if (collect)
	lpa_refuse (*collect, "dst-rwc-effect-unproved", "opaque-insn", insn);
      s.poison_cc ();
      s.known = false;
      return;
    }

  if (e.config_dests_written & (1u << 15))
    {
      /* LaneConfig (SFPCONFIG dest 15): the column-exchange and
	 lane-block bits redirect or drop SFPLOAD/SFPSTORE lanes
	 -- a silent round-trip corruption, not a truncation.  */
      if (collect)
	lpa_refuse (*collect, "lreg-spill-laneconfig-unproven",
		"laneconfig-written-in-function", insn);
    }
  else if (e.config_dests_written != 0 || e.addr_mod_slot_write)
    {
      /* Any other configuration write can change the Dst layout or
	 address-modifier interpretation under the accesses.  */
      if (collect)
	lpa_refuse (*collect, "lreg-spill-no-free-dst", "layout-boundary", insn);
    }

  /* Typed Dst accesses read the counter BEFORE this insn's own RWC
     effect applies; collect rows first.  */
  if (collect && (e.dst_mem_read || e.dst_mem_write))
    {
      spill_ctx &ctx = *collect;
      ctx.have_dst_access = true;
      rtx addr, mode, addr_mode;
      if (!rvtt_dst_access_operands (insn, e, &addr, &mode, &addr_mode))
	lpa_refuse (ctx, "dst-rwc-effect-unproved", "unaudited-dst-access", insn);
      else if (!CONST_INT_P (mode))
	lpa_refuse (ctx, "lreg-spill-inexact-dst-mode", "mode-nonconstant", insn);
      else
	{
	  HOST_WIDE_INT m = INTVAL (mode);
	  /* mod0 0 (FMT_SRCB) resolves at runtime from the ALU
	     configuration: admitted only under the declared or
	     evidenced 32-bit-row layout (checked at scan end);
	     affirmative 16-bit formats refuse regardless.  */
	  if (m == 10)
	    lpa_refuse (ctx, "lreg-spill-no-free-dst",
		    "int32-all-masks-rwc-base", insn);
	  else if (m != 0 && !dst_mode_32bit_p (m) && e.dst_mem_read)
	    /* An EXPLICIT 16-bit-format READ of Dst is layout
	       counter-evidence (its author knows the rows are 16-bit)
	       and refuses even against the declaration.  A 16-bit-
	       format STORE is an output-format conversion, routine in
	       declared-32-bit kernels (the store writes through the
	       32-bit geometry there); it is admitted and its row
	       accounted like any other.  */
	    lpa_refuse (ctx, "lreg-spill-inexact-dst-mode",
		    "16-bit-dst-format", insn);
	  else if (!CONST_INT_P (addr))
	    lpa_refuse (ctx, "lreg-spill-no-free-dst",
		    "address-nonconstant", insn);
	  else if (!s.known)
	    lpa_refuse (ctx, "lreg-spill-no-free-dst",
		    "rwc-window-unproven", insn);
	  else
	    {
	      if (m == 0)
		ctx.have_mod0_srcb = true;
	      ctx.row_epoch.safe_push (s.epoch);
	      ctx.used_rows.safe_push (INTVAL (addr) + s.off);
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: dst row " HOST_WIDE_INT_PRINT_DEC
			 " (imm " HOST_WIDE_INT_PRINT_DEC " + off %d) "
			 "epoch %d mode " HOST_WIDE_INT_PRINT_DEC
			 " at insn %d\n",
			 INTVAL (addr) + s.off, INTVAL (addr), s.off,
			 s.epoch, m, INSN_UID (insn));
	    }
	}
    }

  switch (e.rwc.kind)
    {
    case xtt_rwc_effect_t::NONE:
      break;
    case xtt_rwc_effect_t::INC:
    case xtt_rwc_effect_t::FACE:
      if (s.known)
	s.off += e.rwc.dst_delta;
      break;
    default:
      /* SET / UNKNOWN: the window moves by an unproven amount.  */
      s.known = false;
      break;
    }

  if (e.cc_write)
    s.cc = e.cc_write_all_lanes ? LPA_CC_ALL : LPA_CC_OTHER;
}

/* Join PRED into ACC for block BB.  A pred carrying BB's OWN token is
   the value coming back around BB's own cycle: at offset 0 the cycle
   is rwc-net-zero and the pred is self-consistent (ignored -- no mint
   needed); at a nonzero offset the base really moves per iteration
   (the caller records the step and forces the mint).  Any other
   disagreeing counter position mints BB's stable epoch token
   (offset 0); a disagreeing CC poisons.  */

static void
lpa_join (lpa_state &acc, const lpa_state &pred, basic_block bb)
{
  if (!pred.reached)
    return;
  if (!acc.reached)
    {
      acc = pred;
      return;
    }
  if (!acc.known || !pred.known)
    acc.known = false;
  else if (acc.epoch != pred.epoch || acc.off != pred.off)
    {
      acc.epoch = -(bb->index + 2);
      acc.off = 0;
    }
  if (acc.cc != pred.cc || acc.cc_depth != pred.cc_depth)
    acc.poison_cc ();
  else
    for (unsigned i = 0; i < acc.cc_depth; i++)
      if (acc.cc_stack[i] != pred.cc_stack[i])
	{
	  acc.poison_cc ();
	  break;
	}
}

/* Prove what the function admits: run the CC/delta dataflow to a
   fixpoint, record per-insn states, collect entry-relative Dst rows
   and layout evidence, and apply the ambient-layout admission rule
   (DP-8): runtime-resolved (mod0 0) accesses and Dst-untouched
   functions are admitted ONLY under -mtt-tensix-dst-layout-32b (the
   integration-layer declaration) or an affirmative in-function
   32-bit-class access; declaring the flag falsely on a 16-bit-layout
   kernel makes a spilled compilation produce SILENT WRONG OUTPUT.  */

void
scan_spill_legality (function *fn, spill_ctx &ctx)
{
  ctx.ok = true;
  ctx.refusal = NULL;
  ctx.detail = NULL;
  ctx.at = NULL;

  ctx.noinc_addr_mode = rvtt_no_increment_address_mode ();
  if (ctx.noinc_addr_mode < 0)
    {
      lpa_refuse (ctx, "lreg-spill-no-free-dst", "no-increment-mode-unproven",
	      NULL);
      return;
    }

  /* Fixpoint.  */
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  auto_vec<lpa_state> in, out;
  in.safe_grow_cleared (n_bbs);
  out.safe_grow_cleared (n_bbs);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      in[bb->index] = lpa_state::unreached ();
      out[bb->index] = lpa_state::unreached ();
    }
  bool changed;
  do
    {
      changed = false;
      FOR_EACH_BB_FN (bb, fn)
	{
	  lpa_state next_in = lpa_state::unreached ();
	  bool entry_pred = false;
	  const int own_token = -(bb->index + 2);
	  bool moving_backedge = false;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
		{
		  lpa_join (next_in, lpa_state::entry (), bb);
		  entry_pred = true;
		  continue;
		}
	      const lpa_state &p = out[e->src->index];
	      if (p.reached && p.known && p.epoch == own_token)
		{
		  /* Own-cycle pred: net-zero is self-consistent; a
		     moving cycle forces the mint below.  */
		  if (p.off != 0)
		    moving_backedge = true;
		  continue;
		}
	      lpa_join (next_in, p, bb);
	    }
	  if (!entry_pred && EDGE_COUNT (bb->preds) == 0)
	    next_in = lpa_state::entry ();
	  if (moving_backedge && next_in.reached && next_in.known)
	    {
	      next_in.epoch = own_token;
	      next_in.off = 0;
	    }
	  lpa_state next_out = next_in;
	  if (next_in.reached)
	    {
	      rtx_insn *insn;
	      FOR_BB_INSNS (bb, insn)
		if (NONDEBUG_INSN_P (insn))
		  lpa_transfer (next_out, insn, NULL);
	    }
	  if (next_in != in[bb->index] || next_out != out[bb->index])
	    {
	      in[bb->index] = next_in;
	      out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);

  /* Recording pass: per-insn states + row/evidence collection +
     hard refusals.  Unreached insns keep the fail-closed defaults
     (CC unproved, delta unknown).  */
  ctx.mint_step.safe_grow_cleared (n_bbs);
  ctx.mint_trips.safe_grow_cleared (n_bbs);
  for (unsigned i = 0; i < n_bbs; i++)
    ctx.mint_trips[i] = -1;

  unsigned max_uid = get_max_uid () + 1;
  ctx.cc_before.safe_grow_cleared (max_uid);
  ctx.cc_after.safe_grow_cleared (max_uid);
  ctx.epoch_before.safe_grow_cleared (max_uid);
  ctx.epoch_after.safe_grow_cleared (max_uid);
  ctx.off_before.safe_grow_cleared (max_uid);
  ctx.off_after.safe_grow_cleared (max_uid);
  ctx.known_before.safe_grow_cleared (max_uid);
  ctx.known_after.safe_grow_cleared (max_uid);
  for (unsigned i = 0; i < max_uid; i++)
    {
      ctx.cc_before[i] = LPA_CC_UNPROVED;
      ctx.cc_after[i] = LPA_CC_UNPROVED;
    }

  FOR_EACH_BB_FN (bb, fn)
    {
      if (!in[bb->index].reached)
	continue;
      lpa_state s = in[bb->index];
      if (s.known && s.epoch == -(bb->index + 2))
	{
	  ctx.minted_bbs.safe_push (bb);
	  /* The epoch's own per-iteration step: every own-token pred
	     must return with the same nonzero offset.  */
	  int step = 0;
	  bool step_ok = true;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
		continue;
	      const lpa_state &p = out[e->src->index];
	      if (p.reached && p.known && p.epoch == s.epoch && p.off != 0)
		{
		  if (step == 0)
		    step = p.off;
		  else if (step != p.off)
		    step_ok = false;
		}
	    }
	  ctx.mint_step[bb->index] = step_ok ? step : 0;
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: bb %d minted epoch %d (step %d)\n",
		     bb->index, s.epoch, step_ok ? step : 0);
	}
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  unsigned uid = INSN_UID (insn);
	  ctx.cc_before[uid] = s.cc;
	  ctx.epoch_before[uid] = s.epoch;
	  ctx.off_before[uid] = s.off;
	  ctx.known_before[uid] = s.known;
	  lpa_transfer (s, insn, &ctx);
	  ctx.cc_after[uid] = s.cc;
	  ctx.epoch_after[uid] = s.epoch;
	  ctx.off_after[uid] = s.off;
	  ctx.known_after[uid] = s.known;
	}
    }

  /* DP-11: bound each minted epoch's sweep with the RTL loop
     analysis.  The minted block must be its loop's header and the
     loop must have a proven constant iteration count (capped: a wide
     sweep covers every mod-256 residue anyway).  */
  if (!ctx.minted_bbs.is_empty ())
    {
      /* Full loop normalization (preheaders/simple latches) is
	 required by the simple-loop analysis.  This scan runs only
	 once coloring has blocked: every continuation either mutates
	 the stream (spills) or ends in the hard pressure error, so
	 the normalization's forwarder blocks never perturb a
	 byte-identity surface.  */
      loop_optimizer_init (LOOPS_NORMAL | LOOPS_HAVE_RECORDED_EXITS);
      for (basic_block mbb : ctx.minted_bbs)
	{
	  if (ctx.mint_step[mbb->index] == 0)
	    continue;
	  class loop *l = mbb->loop_father;
	  if (!l || l->header != mbb)
	    continue;
	  struct niter_desc *desc = get_simple_loop_desc (l);
	  if (desc && desc->simple_p && !desc->infinite && desc->const_iter
	      && desc->niter <= 96)
	    ctx.mint_trips[mbb->index] = (int) desc->niter;
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: minted bb %d trip bound %d\n",
		     mbb->index, ctx.mint_trips[mbb->index]);
	}
      iv_analysis_done ();
      loop_optimizer_finalize ();
    }

  if (!ctx.ok)
    return;

  /* DP-9: in-function 32-bit accesses are NOT layout proof (a mixed-
     view kernel can park through an explicit 32-bit view while its
     SRCB accesses resolve 16-bit).  Evidence only ever REFUSES;
     admission of runtime-resolved accesses and Dst-untouched bodies
     comes SOLELY from the integration-layer declaration.  */
  if ((ctx.have_mod0_srcb || !ctx.have_dst_access)
      && !riscv_tt_dst_layout_32b)
    {
      lpa_refuse (ctx, "lreg-spill-inexact-dst-mode", "dst-layout-undeclared",
	      NULL);
      return;
    }

  /* Rows in more than one epoch cannot all be proven disjoint from
     one scratch base.  */
  for (unsigned i = 1; i < ctx.row_epoch.length (); i++)
    if (ctx.row_epoch[i] != ctx.row_epoch[0])
      {
	lpa_refuse (ctx, "lreg-spill-no-free-dst", "cross-epoch-rows", NULL);
	return;
      }
}

/* A scratch offset S may alias a used row K only when they are
   congruent within +/-3 modulo 256 (see the file comment).  */

static bool
rows_may_alias_p (HOST_WIDE_INT s, HOST_WIDE_INT k)
{
  HOST_WIDE_INT d = (s - k) % 256;
  if (d < 0)
    d += 256;
  return d <= 3 || d >= 253;
}

/* Pick the highest proven-free 4-aligned epoch-relative scratch offset
   >= MAX_DELTA (so every compensated immediate S - off stays
   non-negative): [252..0] first (the historical range), then
   [1008..256] for offset-heavy functions.  In a minted epoch the
   check runs across the whole bounded sweep (DP-11): S is free only
   when S - K - m*step clears the alias window for every kernel row K
   and every iteration distance |m| <= trips.  Returns -1 when none.  */

HOST_WIDE_INT
choose_scratch_row (spill_ctx &ctx, int max_delta, int epoch)
{
  int step = 0, trips = 0;
  if (epoch < 0)
    {
      int mbb = -epoch - 2;
      gcc_assert (mbb >= 0 && mbb < (int) ctx.mint_step.length ()
		  && ctx.mint_step[mbb] != 0 && ctx.mint_trips[mbb] >= 0);
      step = ctx.mint_step[mbb];
      trips = ctx.mint_trips[mbb];
    }
  for (int range = 0; range < 2; range++)
    {
      HOST_WIDE_INT hi = range == 0 ? 252 : 1008;
      HOST_WIDE_INT lo = range == 0 ? 0 : 256;
      for (HOST_WIDE_INT s = hi; s >= lo; s -= 4)
	{
	  if (s < max_delta)
	    break;
	  bool clash = false;
	  for (HOST_WIDE_INT k : ctx.used_rows)
	    {
	      for (int m = -trips; m <= trips && !clash; m++)
		if (rows_may_alias_p (s, k + (HOST_WIDE_INT) m * step))
		  clash = true;
	      if (clash)
		break;
	    }
	  if (!clash)
	    {
	      ctx.used_rows.safe_push (s);
	      return s;
	    }
	}
    }
  return -1;
}



/* Hard LREG number named by the sentinel read pattern with insn code
   CODE (the rvtt_sfpreadlreg<N> family), or -1 when CODE is not a
   sentinel read.  */

int
sentinel_read_lregno (int code)
{
  switch (code)
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

/* Hard LREG number named by the sentinel write pattern with insn code
   CODE (the rvtt_sfpwritelreg<N> family), or -1 when CODE is not a
   sentinel write.  */

int
sentinel_write_lregno (int code)
{
  switch (code)
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

/* Whether REGNO is an XTT32SI pseudo.  */

bool
xtt32_pseudo_p (unsigned regno)
{
  return regno >= FIRST_PSEUDO_REGISTER
    && regno < static_cast<unsigned> (max_reg_num ())
    && regno_reg_rtx[regno]
    && GET_MODE (regno_reg_rtx[regno]) == XTT32SImode;
}

/* Record a precolor on NODE; conflicting constraints fail closed.  */

static void
set_precolor (lpa_graph &g, int node, int color, rtx_insn *insn)
{
  lpa_web &w = g.webs[node];
  if (w.precolor >= 0 && w.precolor != color)
    {
      if (!g.fail)
	{
	  g.fail = "precolor-conflict";
	  g.fail_at = insn;
	}
      return;
    }
  w.precolor = color;
}

/* Build webs (pseudo = web) and the interference graph.  Requires
   up-to-date DF LR.  SPILL_TMPS marks reload pseudos from earlier
   rounds.  */

void
build_graph (function *fn, lpa_graph &g, bitmap spill_tmps)
{
  unsigned max_regno = max_reg_num ();
  g.node_of_reg.safe_grow_cleared (max_regno);
  for (unsigned i = 0; i < max_regno; i++)
    g.node_of_reg[i] = -1;

  /* Pass 1: collect webs, occurrences, precolors, reservations.  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx pat = PATTERN (insn);
	  bool bare_use = GET_CODE (pat) == USE;

	  subrtx_iterator::array_type array;
	  FOR_EACH_SUBRTX (iter, array, pat, ALL)
	    {
	      const_rtx x = *iter;
	      if (!REG_P (x))
		continue;
	      unsigned regno = REGNO (x);
	      machine_mode mode = GET_MODE (x);
	      if (regno < FIRST_PSEUDO_REGISTER)
		{
		  if (SFPU_REG_P (regno) && !g.fail)
		    {
		      g.fail = "hard-sfpu-reg-pre-ira";
		      g.fail_at = insn;
		    }
		  continue;
		}
	      if (mode == XTT64SImode || mode == XTT128SImode)
		{
		  if (!g.fail)
		    {
		      g.fail = "wide-sfpu-mode-unproven";
		      g.fail_at = insn;
		    }
		  continue;
		}
	      if (mode != XTT32SImode)
		continue;
	      int node = g.node_of_reg[regno];
	      if (node < 0)
		{
		  lpa_web w = {};
		  w.regno = regno;
		  w.precolor = -1;
		  w.reload_tmp = spill_tmps && bitmap_bit_p (spill_tmps,
							     regno);
		  node = g.webs.length ();
		  g.webs.safe_push (w);
		  g.node_of_reg[regno] = node;
		}
	      g.webs[node].occ++;
	      if (bare_use)
		g.webs[node].reservation = true;
	    }

	  int code = recog_memoized (insn);
	  int lregno = sentinel_read_lregno (code);
	  if (lregno >= 0)
	    {
	      rtx set = single_set (insn);
	      if (set && REG_P (SET_DEST (set))
		  && g.node_of_reg[REGNO (SET_DEST (set))] >= 0)
		set_precolor (g, g.node_of_reg[REGNO (SET_DEST (set))],
			      lregno, insn);
	    }
	  lregno = sentinel_write_lregno (code);
	  if (lregno >= 0)
	    {
	      rtx op = XVECEXP (pat, 0, 0);
	      if (REG_P (op) && g.node_of_reg[REGNO (op)] >= 0)
		set_precolor (g, g.node_of_reg[REGNO (op)], lregno, insn);
	    }
	}
    }

  unsigned n = g.webs.length ();
  g.conflicts = sbitmap_alloc (n * n);
  bitmap_clear (g.conflicts);
  g.degree.safe_grow_cleared (n);
  g.alias.safe_grow_cleared (n);
  for (unsigned i = 0; i < n; i++)
    g.alias[i] = -1;

  if (g.fail)
    return;

  /* Pass 2: interference by live-at-def, backward DF simulation.  A
     simple copy's source does not interfere with its dest.  */
  FOR_EACH_BB_FN (bb, fn)
    {
      auto_bitmap live;
      bitmap_copy (live, DF_LR_OUT (bb));
      df_simulate_initialize_backwards (bb, live);
      rtx_insn *insn;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  int move_src_node = -1;
	  rtx set = single_set (insn);
	  if (set && REG_P (SET_DEST (set)) && REG_P (SET_SRC (set))
	      && xtt32_pseudo_p (REGNO (SET_DEST (set)))
	      && xtt32_pseudo_p (REGNO (SET_SRC (set))))
	    move_src_node = g.node_of_reg[REGNO (SET_SRC (set))];

	  df_ref def;
	  FOR_EACH_INSN_DEF (def, insn)
	    {
	      unsigned dregno = DF_REF_REGNO (def);
	      if (!xtt32_pseudo_p (dregno))
		continue;
	      int dnode = g.node_of_reg[dregno];
	      if (dnode < 0)
		continue;
	      /* live here = live after the insn.  */
	      unsigned lregno;
	      bitmap_iterator bi;
	      EXECUTE_IF_SET_IN_BITMAP (live, 0, lregno, bi)
		{
		  if (!xtt32_pseudo_p (lregno) || lregno == dregno)
		    continue;
		  int lnode = g.node_of_reg[lregno];
		  if (lnode < 0 || lnode == move_src_node)
		    continue;
		  g.add_conflict (dnode, lnode);
		}
	      /* Parallel defs of one insn interfere.  */
	      df_ref def2;
	      FOR_EACH_INSN_DEF (def2, insn)
		{
		  unsigned d2 = DF_REF_REGNO (def2);
		  if (d2 != dregno && xtt32_pseudo_p (d2)
		      && g.node_of_reg[d2] >= 0)
		    g.add_conflict (dnode, g.node_of_reg[d2]);
		}
	    }
	  df_simulate_one_insn_backwards (bb, insn, live);
	}
    }
}

/* ---------------- conservative coalescing (Briggs/George) ----------------

   Under -mtt-tensix-optimize-lreg-coalesce, copy-related webs merge on
   the just-built interference graph BEFORE the DSATUR colorability
   verdict and spill-victim selection, so a web that only spilled
   through a Dst round trip because its copy halves were counted
   separately colors for free.  Merges are CONSERVATIVE in the
   classical sense (Briggs/Cooper/Torczon, TOPLAS 1994): a merge is
   performed only when it provably cannot turn an 8-colorable graph
   uncolorable, so coalescing only ever REMOVES spills.

   - Briggs test (neither web precolored): the merged node must have
     fewer than SFPU_REG_NUM neighbors of significant degree.  A
     common neighbor of both halves loses one edge to the merge, so it
     is judged at degree-1; a precolored neighbor is always
     significant (it is never simplifiable).  Failing the test refuses
     coalesce-conservative-degree.

   - George test (one web precolored): the uncolored web merges into
     the precolored one only when every significant neighbor of the
     uncolored web already interferes with the precolored one
     (insignificant = uncolored neighbor of degree < SFPU_REG_NUM).
     Failing the test refuses coalesce-george-interference.

   - Equal-precolor pairs merge unconditionally (both fixed to the
     same color already: no neighbor's palette changes and common
     neighbors only lose degree).  Distinct-precolor pairs refuse
     coalesce-precolor-conflict; pairs touching a livein reservation
     sentinel or a spill-generated reload temporary refuse
     coalesce-web-class (merging would export the never-spill class to
     an ordinary web and could only remove spill candidates).

   Merging is graph-side only: alias[] records the union-find, the
   representative's conflict row absorbs the merged row, occurrence
   counts accumulate for the Chaitin cost, and degrees are recomputed
   over live representatives.  The instruction stream is untouched;
   assignment stays delegated to IRA (whose own coalescing re-derives
   the merge when it assigns).  Spilling a merged web round-trips ALL
   of its constituents through ONE scratch row: constituents are
   copy-related and never interfere, so at any point at most one is
   live (or all live ones hold the same value, the copy-chain case)
   and the shared row always holds that value; constituents with
   disagreeing RWC epochs refuse lreg-spill-merged-epoch-mismatch in
   the victim chooser.  Everything here runs to a fixpoint bounded by
   the node count; iteration order is insn-stream order, so the
   result is deterministic.  */

/* Recompute degrees over live representatives after merges.  */

static void
coalesce_recompute_degrees (lpa_graph &g)
{
  unsigned n = g.webs.length ();
  for (unsigned i = 0; i < n; i++)
    {
      g.degree[i] = 0;
      if (!g.live_p (i))
	continue;
      for (unsigned j = 0; j < n; j++)
	if (j != i && g.live_p (j) && g.conflict_p (i, j))
	  g.degree[i]++;
    }
}

/* Whether the conservative test admits merging live representatives I
   and J (non-interfering, copy-related).  On refusal, *WHY names it.  */

static bool
coalesce_admissible_p (const lpa_graph &g, unsigned i, unsigned j,
		       const char **why, const char **test)
{
  const lpa_web &wi = g.webs[i];
  const lpa_web &wj = g.webs[j];

  if (wi.reservation || wj.reservation || wi.reload_tmp || wj.reload_tmp)
    {
      *why = "coalesce-web-class";
      return false;
    }

  unsigned n = g.webs.length ();
  if (wi.precolor >= 0 && wj.precolor >= 0)
    {
      if (wi.precolor != wj.precolor)
	{
	  *why = "coalesce-precolor-conflict";
	  return false;
	}
      *test = "precolor-equal";
      return true;
    }

  if (wi.precolor >= 0 || wj.precolor >= 0)
    {
      /* George: every significant neighbor of the uncolored web U must
	 already interfere with the precolored web P.  */
      unsigned p = wi.precolor >= 0 ? i : j;
      unsigned u = wi.precolor >= 0 ? j : i;
      for (unsigned m = 0; m < n; m++)
	{
	  if (m == p || m == u || !g.live_p (m) || !g.conflict_p (u, m))
	    continue;
	  bool significant = g.webs[m].precolor >= 0
	    || g.degree[m] >= SFPU_REG_NUM;
	  if (significant && !g.conflict_p (p, m))
	    {
	      *why = "coalesce-george-interference";
	      return false;
	    }
	}
      *test = "george";
      return true;
    }

  /* Briggs: the merged node must have fewer than SFPU_REG_NUM
     significant-degree neighbors.  */
  unsigned significant = 0;
  for (unsigned m = 0; m < n; m++)
    {
      if (m == i || m == j || !g.live_p (m))
	continue;
      bool ni = g.conflict_p (i, m);
      bool nj = g.conflict_p (j, m);
      if (!ni && !nj)
	continue;
      unsigned dm = g.degree[m] - ((ni && nj) ? 1 : 0);
      if (g.webs[m].precolor >= 0 || dm >= SFPU_REG_NUM)
	significant++;
    }
  if (significant >= SFPU_REG_NUM)
    {
      *why = "coalesce-conservative-degree";
      return false;
    }
  *test = "briggs";
  return true;
}

/* Merge live representative J into live representative I.  */

static void
coalesce_merge (lpa_graph &g, unsigned i, unsigned j)
{
  unsigned n = g.webs.length ();
  for (unsigned m = 0; m < n; m++)
    if (m != i && g.conflict_p (j, m))
      g.add_conflict (i, m);
  if (g.webs[j].precolor >= 0)
    g.webs[i].precolor = g.webs[j].precolor;
  g.webs[i].occ += g.webs[j].occ;
  g.alias[j] = i;
  coalesce_recompute_degrees (g);
}

/* Conservative-coalesce copy-related webs on G to a fixpoint.
   Returns the number of merges performed.  */

unsigned
coalesce_conservative (function *fn, lpa_graph &g)
{
  /* Copy pairs in insn-stream order (deterministic).  */
  auto_vec<int> pair_dst, pair_src;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx set = single_set (insn);
	  if (!set || !REG_P (SET_DEST (set)) || !REG_P (SET_SRC (set))
	      || !xtt32_pseudo_p (REGNO (SET_DEST (set)))
	      || !xtt32_pseudo_p (REGNO (SET_SRC (set))))
	    continue;
	  int dn = g.node_of_reg[REGNO (SET_DEST (set))];
	  int sn = g.node_of_reg[REGNO (SET_SRC (set))];
	  if (dn < 0 || sn < 0)
	    continue;
	  pair_dst.safe_push (dn);
	  pair_src.safe_push (sn);
	}
    }

  unsigned merges = 0;
  unsigned n = g.webs.length ();
  /* Each pass without a merge terminates; each merge kills one node.  */
  for (unsigned pass = 0; pass <= n; pass++)
    {
      bool progress = false;
      for (unsigned k = 0; k < pair_dst.length (); k++)
	{
	  unsigned i = g.rep (pair_dst[k]);
	  unsigned j = g.rep (pair_src[k]);
	  if (i == j)
	    continue;		/* already one web */
	  if (g.conflict_p (i, j))
	    {
	      /* A redefinition overlap: the halves hold different
		 values somewhere.  Not a coalescing candidate.  */
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc coalesce-refusal:"
			 " coalesce-interfering-copy (r%u / r%u)\n",
			 g.webs[i].regno, g.webs[j].regno);
	      continue;
	    }
	  const char *why = NULL, *test = NULL;
	  /* Keep the representative deterministic: merge the higher
	     node index into the lower unless a precolor pins the
	     representative.  */
	  unsigned ri = MIN (i, j), rj = MAX (i, j);
	  if (g.webs[rj].precolor >= 0 && g.webs[ri].precolor < 0)
	    std::swap (ri, rj);
	  if (!coalesce_admissible_p (g, ri, rj, &why, &test))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc coalesce-refusal: %s (r%u / r%u)\n",
			 why, g.webs[ri].regno, g.webs[rj].regno);
	      continue;
	    }
	  coalesce_merge (g, ri, rj);
	  merges++;
	  progress = true;
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc coalesce: merged web r%u into r%u"
		     " (%s test, merged degree %u, occ %u)\n",
		     g.webs[rj].regno, g.webs[ri].regno, test,
		     g.degree[ri], g.webs[ri].occ);
	}
      if (!progress)
	break;
    }
  if (dump_file && merges)
    {
      unsigned live = 0;
      for (unsigned i = 0; i < n; i++)
	if (g.live_p (i))
	  live++;
      fprintf (dump_file,
	       "lreg-alloc coalesce: %u merge(s); %u web(s) -> %u"
	       " coalesced web(s)\n",
	       merges, n, live);
    }
  return merges;
}

/* ------------------------------ DSATUR ----------------------------- */

/* DSATUR over SFPU_REG_NUM colors with precolored nodes fixed.
   Deterministic: saturation desc, degree desc, node index asc.
   Returns true when fully colored; otherwise *BLOCKED names a node
   with a saturated palette.  */

bool
dsatur_color (const lpa_graph &g, auto_vec<int> &color, int *blocked)
{
  unsigned n = g.webs.length ();
  color.truncate (0);
  color.safe_grow_cleared (n);
  for (unsigned i = 0; i < n; i++)
    color[i] = -1;

  /* Fix precolored nodes first; equal-precolor conflicts block.
     Coalesced-away nodes are skipped throughout: their conflicts and
     precolors live on their representative.  */
  for (unsigned i = 0; i < n; i++)
    if (g.live_p (i) && g.webs[i].precolor >= 0)
      {
	color[i] = g.webs[i].precolor;
	for (unsigned j = 0; j < i; j++)
	  if (color[j] == color[i] && g.conflict_p (i, j))
	    {
	      *blocked = i;
	      return false;
	    }
      }

  const unsigned full = (1u << SFPU_REG_NUM) - 1;
  for (;;)
    {
      int best = -1;
      unsigned best_sat = 0, best_deg = 0;
      for (unsigned i = 0; i < n; i++)
	{
	  if (color[i] >= 0 || !g.live_p (i))
	    continue;
	  unsigned sat_mask = 0;
	  for (unsigned j = 0; j < n; j++)
	    if (color[j] >= 0 && g.conflict_p (i, j))
	      sat_mask |= 1u << color[j];
	  unsigned sat = popcount_hwi (sat_mask & full);
	  if (best < 0 || sat > best_sat
	      || (sat == best_sat && g.degree[i] > best_deg))
	    {
	      best = i;
	      best_sat = sat;
	      best_deg = g.degree[i];
	    }
	}
      if (best < 0)
	return true;		/* all colored */

      unsigned sat_mask = 0;
      for (unsigned j = 0; j < n; j++)
	if (color[j] >= 0 && g.conflict_p (best, j))
	  sat_mask |= 1u << color[j];
      unsigned avail = ~sat_mask & full;
      if (!avail)
	{
	  *blocked = best;
	  return false;
	}
      color[best] = ctz_hwi (avail);
    }
}

/* Consumers whose LREG/Dst WRITES are lane-gated and whose dataflow
   is lane-local (no value movement between lanes): a reload feeding
   only such an insn is complete under narrowed CC -- the reload's
   disabled-lane garbage is computed on but never written through the
   lane gate -- and an RMW def by such an insn under narrowed CC
   store-backs only its enabled lanes while the scratch keeps the old
   disabled lanes, which is exactly the predicated-write semantics.
   Membership lives at the definitions (the xtt_lane_gated attribute,
   rvtt.md -- the former ~100-entry hand insn_code
   allowlist here is deleted): cross-lane ops
   (SFPSWAP/SFPTRANSP/SFPSHFT2/SELECT/CONCAT), plain all-lanes copies
   (the rvtt_sfpassign SET pattern -- the unconditional SFPMOV-mod-2
   move; NOT the CC-gated predicated-assign below, whose SET_SRC is
   UNSPECV_SFPASSIGN and which IS admitted), SrcS stores, loadmacro
   forms and the zero-length LREG markers keep the refusing default
   (fail-closed).  */

static bool
lane_gated_consumer_p (rtx_insn *insn)
{
  if (rvtt_lane_gated_consumer_p (insn))
    return true;
  /* The predicated-assign copy is CC-gated by definition.  */
  rtx pat = PATTERN (insn);
  return GET_CODE (pat) == SET
    && GET_CODE (SET_SRC (pat)) == UNSPEC_VOLATILE
    && XINT (SET_SRC (pat), 1) == UNSPECV_SFPASSIGN;
}

/* Whether web REGNO admits the exact Dst round trip: every occurrence
   point must have provably all-lanes CC (SFPSTORE/SFPLOAD move only
   CC-enabled lanes; a narrowed point would silently lose disabled
   lanes) and a proven RWC delta (so the compensated immediate names
   the same physical row at every point).  Stores-after-def use the
   AFTER-insn state (the def itself may narrow CC or move the
   counter).  *MAX_DELTA collects the largest compensation needed.  */

static bool
web_spill_admissible_p (function *fn, unsigned regno, const spill_ctx &ctx,
			int *max_off, int *epoch_out, const char **why)
{
  rtx preg = regno_reg_rtx[regno];
  *max_off = 0;
  bool have_epoch = false;
  int epoch = 0;
  /* A value live into a minted join crosses a base-identity boundary
     (e.g. a loop-carried web across the rwc backedge): its spill row
     would name different physical rows at def and use.  */
  for (basic_block mbb : ctx.minted_bbs)
    if (REGNO_REG_SET_P (DF_LR_IN (mbb), regno))
      {
	*why = "lreg-spill-no-free-dst";
	return false;
      }
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (GET_CODE (PATTERN (insn)) == USE)
	    {
	      if (reg_referenced_p (preg, PATTERN (insn)))
		{
		  *why = "lreg-alloc-unknown-use";
		  return false;
		}
	      continue;
	    }
	  bool reads = reg_referenced_p (preg, PATTERN (insn));
	  bool writes = reg_set_p (preg, insn);
	  if (!reads && !writes)
	    continue;
	  unsigned uid = INSN_UID (insn);
	  if (uid >= ctx.cc_before.length ())
	    {
	      /* An insn minted after the legality scan (another web's
		 round trip) never references this web; fail closed.  */
	      *why = "lreg-alloc-post-scan-insn";
	      return false;
	    }
	  if (reads)
	    {
	      if (ctx.cc_before[uid] != LPA_CC_ALL
		  && !lane_gated_consumer_p (insn))
		{
		  *why = "cc-enable-unproved";
		  return false;
		}
	      if (!ctx.known_before[uid])
		{
		  *why = "lreg-spill-no-free-dst";
		  return false;
		}
	      if (!have_epoch)
		{
		  have_epoch = true;
		  epoch = ctx.epoch_before[uid];
		}
	      else if (epoch != ctx.epoch_before[uid])
		{
		  *why = "lreg-spill-no-free-dst";
		  return false;
		}
	      *max_off = MAX (*max_off, ctx.off_before[uid]);
	    }
	  if (writes)
	    {
	      /* A pure def must be all-lanes; an RMW def by a
		 lane-gated insn is exact at any CC (the scratch keeps
		 the old disabled lanes -- predicated-write semantics).  */
	      if (ctx.cc_after[uid] != LPA_CC_ALL
		  && !(reads && lane_gated_consumer_p (insn)))
		{
		  *why = "cc-enable-unproved";
		  return false;
		}
	      if (!ctx.known_after[uid])
		{
		  *why = "lreg-spill-no-free-dst";
		  return false;
		}
	      if (!have_epoch)
		{
		  have_epoch = true;
		  epoch = ctx.epoch_after[uid];
		}
	      else if (epoch != ctx.epoch_after[uid])
		{
		  *why = "lreg-spill-no-free-dst";
		  return false;
		}
	      *max_off = MAX (*max_off, ctx.off_after[uid]);
	    }
	}
    }
  /* Every recorded kernel row must share the web's epoch, or scratch
     disjointness cannot be proven against it.  */
  if (have_epoch)
    for (int re : ctx.row_epoch)
      if (re != epoch)
	{
	  *why = "lreg-spill-no-free-dst";
	  return false;
	}
  /* DP-11: a minted (loop) epoch's base advances per iteration, so the
     scratch row SWEEPS Dst across iterations while earlier iterations'
     kernel rows stay live for pack.  Such spills are admitted only
     when the sweep is bounded: proven step AND proven trip count (the
     chooser then checks the alias window across the whole range).  */
  if (have_epoch && epoch < 0)
    {
      int mbb = -epoch - 2;
      if (mbb < 0 || mbb >= (int) ctx.mint_step.length ()
	  || ctx.mint_step[mbb] == 0 || ctx.mint_trips[mbb] < 0)
	{
	  *why = "lreg-spill-no-free-dst";
	  return false;
	}
    }
  *epoch_out = have_epoch ? epoch : 0;
  return true;
}

/* Deterministic spill choice around BLOCKED: the cheapest spillable,
   round-trip-admissible web among the blocked node and its neighbors
   (cost = occurrences scaled down by degree).  -1 when none;
   *WHY / *MAX_DELTA describe the choice or the cheapest inadmissible
   candidate's blocker.  */

int
choose_spill_web (const lpa_graph &g, int blocked, function *fn,
		  const spill_ctx &ctx, int *max_delta, int *epoch_out,
		  const char **why)
{
  unsigned n = g.webs.length ();
  int best = -1;
  HOST_WIDE_INT best_cost = 0;
  const char *blocked_why = NULL;
  HOST_WIDE_INT blocked_cost = 0;
  for (unsigned i = 0; i < n; i++)
    {
      if (!g.live_p (i))
	continue;		/* coalesced away; its rep is the web */
      if ((int) i != blocked && !g.conflict_p (blocked, i))
	continue;
      const lpa_web &w = g.webs[i];
      if (w.reservation || w.reload_tmp)
	continue;
      HOST_WIDE_INT cost
	= (HOST_WIDE_INT) w.occ * 1024 / (g.degree[i] + 1);
      if (best >= 0 && (cost > best_cost
			|| (cost == best_cost
			    && w.regno >= g.webs[best].regno)))
	continue;
      /* Every constituent of a coalesced web must admit the round
	 trip through ONE shared scratch row: all admissible, one
	 common RWC epoch, the largest compensation wins.  An
	 uncoalesced web is its own single constituent (the plain
	 pre-coalescing path, byte-identical).  */
      int woff = 0, wepoch = 0;
      const char *wwhy = NULL;
      bool admissible = true, have_epoch = false;
      for (unsigned m = 0; m < n; m++)
	{
	  if (g.rep (m) != (int) i)
	    continue;
	  int moff = 0, mepoch = 0;
	  if (!web_spill_admissible_p (fn, g.webs[m].regno, ctx, &moff,
				       &mepoch, &wwhy))
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: candidate r%u inadmissible (%s)\n",
			 g.webs[m].regno, wwhy);
	      admissible = false;
	      break;
	    }
	  if (have_epoch && mepoch != wepoch)
	    {
	      wwhy = "lreg-spill-merged-epoch-mismatch";
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: candidate r%u inadmissible (%s)\n",
			 g.webs[m].regno, wwhy);
	      admissible = false;
	      break;
	    }
	  wepoch = mepoch;
	  have_epoch = true;
	  woff = MAX (woff, moff);
	}
      if (!admissible)
	{
	  /* Report the cheapest inadmissible candidate's blocker.  */
	  if (!blocked_why || cost < blocked_cost)
	    {
	      blocked_why = wwhy;
	      blocked_cost = cost;
	    }
	  continue;
	}
      best = i;
      best_cost = cost;
      *max_delta = woff;
      *epoch_out = wepoch;
    }
  if (best < 0)
    *why = blocked_why ? blocked_why : "lreg-spill-no-candidate";
  return best;
}

} /* namespace rvtt_lpa */
