/* DSATUR graph-coloring LREG allocator (M2) for Tensix SFPU.
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

/* This pass is the M2 milestone allocator (SFPI_COMPILER_UPGRADE.md
   section 4), replacing the former dump-only audit stub.  It has two
   independent layers:

   1. The pre-IRA pressure audit (under
      -mtt-tensix-optimize-pressure-schedule, byte-identical to the
      historical stub's dump).

   2. Colorability enforcement (under the default-off
      -mtt-tensix-optimize-lreg-alloc): a Chaitin-style
      build/color/spill loop whose coloring engine is DSATUR over the
      eight-register LREG file.

      - The interference graph is built over XTT32SI pseudo webs after
	pass_rvtt_lreg_livein has materialized every raw-LREG
	reservation as a sentinel pseudo interval, so raw reservations
	participate as ordinary precolored nodes.  Precolors come from
	the singleton-class constraints of the rvtt_sfpreadlregN /
	rvtt_sfpwritelregN metadata patterns.

      - When the function's peak simultaneous SFPU pressure fits the
	file, the pass is a proven NO-OP: nothing is emitted, nothing
	is rewritten, and the compilation is byte-identical with the
	flag on or off.

      - When DSATUR cannot color the graph, a selected web is spilled
	through a Dst scratch-row round trip: SFPSTORE mod0 4 (INT32)
	after each def, SFPLOAD mod0 4 before each use, with the
	audited no-increment address mode.  The INT32 format pair is
	the bit-exact 32-bit round trip on both WH and BH (simulator
	models read_dst32b/write_dst32b through the exact
	encode_fp32/decode_fp32 involution; FP32 mod0 3 is NOT used
	because the BH store flushes denormals).

      - After spilling makes the graph 8-colorable, register
	assignment is deliberately left to IRA: the DSATUR verdict is
	the colorability certificate, and delegating assignment keeps
	IRA's coalescing and guarantees untouched functions allocate
	exactly as before.  If IRA still spills (it is not an optimal
	colorer), the post-RA rtl-rvtt-spill-diag.cc named error
	remains the backstop.

   Bit-exactness gates (each failure is a named refusal that keeps
   today's lreg-pressure-exceeded error byte-identically -- refusal
   paths never mutate anything):

      - lreg-spill-inexact-dst-mode: the spill round trip is bit-exact
	only through the 32-bit Dst formats.  Any typed Dst access in
	the function carrying a 16-bit data mode (FP16A/FP16B/INT8/
	UINT16/INT16/INT8_COMP/LO16_ONLY/HI16_ONLY), the runtime-
	resolved SRCB mode 0, or a non-constant mode operand proves a
	16-bit (or unprovable) Dst view and refuses.  A function whose
	typed accesses are all 32-bit-class, or that performs no typed
	Dst access at all, proceeds; the residual assumption -- that
	the surrounding kernel does not view the scratch rows through a
	16-bit format -- is the flag's documented contract, exactly
	parallel to the ambient all-lanes CC contract the shipped CC
	synthesis already bakes in (see rtl-rvtt-dst-ownership.cc).

      - lreg-spill-no-free-dst: scratch rows are derived from the
	function's own typed Dst addresses.  All of them must be
	CONST_INT and the RWC/layout base must be provably stable
	function-wide (no RWC-effect, config-write, address-modifier
	or opaque instruction anywhere), because a Dst address is
	base-relative ((imm + RWC_Dst + MATH_Offset + REGW_Base) &
	0x3FF).  Two accesses can only touch the same physical rows
	when their immediates are congruent within +/-3 modulo 256
	(the dst32b_adjust_row aliasing window; the same physical-row
	model gimple-rvtt-transp-involution.cc:486 audits), so a
	scratch row is proven free when its immediate keeps that
	distance from every kernel immediate.  Loads carrying mod0 10
	(INT32_ALL) refuse: that mode masks the RWC base (offset &= 3)
	and breaks the shared-base disjointness proof.

      - cc-enable-unproved: SFPSTORE and SFPLOAD move only CC-enabled
	lanes and no all-lanes store variant exists, so the round trip
	is complete only under all-lanes CC.  Any instruction anywhere
	in the function that can narrow CC (PUSHC/POPC/COMPC, or a
	typed CC write that is not the proven all-lanes SFPENCC)
	refuses.  This is deliberately function-coarse and
	fail-closed; a point-wise CC lattice (the
	rtl-rvtt-dst-ownership.cc machinery) can refine it later.

      - dst-rwc-effect-unproved: any opaque instruction (call, asm,
	unaudited pattern), RWC boundary, or layout boundary refuses,
	per the vocabulary of SFPLOADMACRO_FORMATION.md.

   QSR is excluded by the gate (and independently by the unproven
   no-increment address mode).  XTT64/XTT128-mode pseudos refuse
   enforcement fail-closed.

   Fire tests: g++.target/riscv/tt/tensix/lreg-alloc-*.C.  */

#define IN_TARGET_CODE 1

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
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"

namespace {

/* ------------------------- pressure audit -------------------------- */

static bool
xtt32_allocation_unit_p (unsigned regno)
{
  if (regno < FIRST_PSEUDO_REGISTER)
    return SFPU_REG_P (regno);
  return regno < static_cast<unsigned> (max_reg_num ()) && regno_reg_rtx[regno]
    && GET_MODE (regno_reg_rtx[regno]) == XTT32SImode;
}

static unsigned
count_xtt32_units (bitmap live)
{
  unsigned count = 0;
  unsigned regno;
  bitmap_iterator iterator;
  EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, iterator)
    count += xtt32_allocation_unit_p (regno);
  return count;
}

/* The historical dump-only audit (kept byte-identical for
   -mtt-tensix-optimize-pressure-schedule consumers).  */

static void
audit_function (function *fn)
{
  auto_bitmap live;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      bitmap_copy (live, DF_LR_IN (bb));
      df_simulate_initialize_forwards (bb, live);
      const unsigned live_in = count_xtt32_units (live);
      unsigned peak = live_in;
      unsigned tensix_insns = 0;

      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    ++tensix_insns;
	  df_simulate_one_insn_forwards (bb, insn, live);
	  peak = MAX (peak, count_xtt32_units (live));
	}

      if (dump_file && (tensix_insns || peak))
	fprintf (dump_file,
		 "SFPU pre-IRA audit: bb=%d insns=%u live-in=%u peak=%u "
		 "live-out=%u capacity=%u colorability=unchecked\n",
		 bb->index, tensix_insns, live_in, peak,
		 count_xtt32_units (live), SFPU_REG_NUM);
    }
}

/* Function-wide peak simultaneous SFPU pressure.  */

static unsigned
function_peak_pressure (function *fn)
{
  unsigned peak = 0;
  auto_bitmap live;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      bitmap_copy (live, DF_LR_IN (bb));
      df_simulate_initialize_forwards (bb, live);
      peak = MAX (peak, count_xtt32_units (live));
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  df_simulate_one_insn_forwards (bb, insn, live);
	  peak = MAX (peak, count_xtt32_units (live));
	}
    }
  return peak;
}

/* ---------------------- effect classification ---------------------- */

/* Structural transparency: no unspec_volatile anywhere in the pattern,
   no memory store, no call, no asm.  Such an instruction cannot reach
   Dst, the RWC counters, configuration state, or CC.  (Mirror of
   rtl-rvtt-dst-ownership.cc pattern_transparent_p.)  */

static bool
pattern_transparent_p (rtx_insn *insn)
{
  if (CALL_P (insn))
    return false;
  rtx pat = PATTERN (insn);
  if (asm_noperands (pat) >= 0)
    return false;
  subrtx_iterator::array_type array;
  FOR_EACH_SUBRTX (iter, array, pat, ALL)
    {
      const_rtx x = *iter;
      if (GET_CODE (x) == UNSPEC_VOLATILE)
	return false;
      if (GET_CODE (x) == SET && MEM_P (SET_DEST (x)))
	return false;
      if (GET_CODE (x) == CLOBBER && MEM_P (XEXP (x, 0)))
	return false;
    }
  return true;
}

/* Audited architectural effect data for typed value-op patterns the
   generated attribute family does not carry yet.  Copied verbatim from
   rtl-rvtt-dst-ownership.cc (the principled home is the attribute
   family; that migration is blocked on a planner-oracle re-freeze, so
   both consumers carry the same audited table until then).  */

struct lpa_effect_override
{
  insn_code code;
  bool cc_writes;
};

static const lpa_effect_override effect_overrides[] = {
  { CODE_FOR_rvtt_sfploadi_lv_int,	false },
  { CODE_FOR_rvtt_sfpsetsgn_v_lv,	false },
  { CODE_FOR_rvtt_sfpsetsgn_i_lv_int,	false },
  { CODE_FOR_rvtt_sfpsetexp_v_lv,	false },
  { CODE_FOR_rvtt_sfpsetexp_i_lv_int,	false },
  { CODE_FOR_rvtt_sfpsetman_v_lv,	false },
  { CODE_FOR_rvtt_sfpsetman_i_lv_int,	false },
  { CODE_FOR_rvtt_sfpabs_lv,		false },
  { CODE_FOR_rvtt_sfpabs_nv,		false },
  { CODE_FOR_rvtt_sfpmov_lv,		false },
  { CODE_FOR_rvtt_sfpmov_nv,		false },
  { CODE_FOR_rvtt_sfpcast_lv,		false },
  { CODE_FOR_rvtt_sfpexman_lv,		false },
  { CODE_FOR_rvtt_sfpexman_nv,		false },
  { CODE_FOR_rvtt_sfpdivp2_lv_int,	false },
  { CODE_FOR_rvtt_sfparecip_lv,		false },
  { CODE_FOR_rvtt_sfpexexp_lv,		true },
  { CODE_FOR_rvtt_sfpexexp_nv,		true },
  { CODE_FOR_rvtt_sfplz_lv,		true },
  { CODE_FOR_rvtt_sfplz_nv,		true },
  { CODE_FOR_rvtt_sfpiadd_v_lv,		true },
  { CODE_FOR_rvtt_sfpiadd_v_nv,		true },
  { CODE_FOR_rvtt_sfpiadd_i_lv_int,	true },
  { CODE_FOR_rvtt_sfpiadd_i_nv,		true },
};

/* 32-bit Dst format class (simulator-audited; the same class
   gimple-rvtt-transp-involution.cc uses), minus mod0 10 (INT32_ALL)
   which masks the RWC base and is handled separately.  */

static bool
dst_mode_32bit_p (HOST_WIDE_INT m)
{
  return m == 3 || m == 4 || m == 7 || m == 9 || m == 12;
}

static int sentinel_read_lregno (int code);
static int sentinel_write_lregno (int code);

/* -------------------------- spill legality -------------------------- */

struct spill_ctx
{
  bool ok;
  const char *refusal;		/* named refusal */
  const char *detail;
  rtx_insn *at;
  int noinc_addr_mode;
  /* Every CONST_INT Dst immediate the function's typed accesses touch,
     plus the scratch immediates this pass has assigned.  */
  auto_vec<HOST_WIDE_INT> used_rows;
};

static void
refuse (spill_ctx &ctx, const char *name, const char *detail, rtx_insn *at)
{
  if (!ctx.ok)
    return;			/* keep the first refusal */
  ctx.ok = false;
  ctx.refusal = name;
  ctx.detail = detail;
  ctx.at = at;
}

/* Prove the function admits Dst scratch-row spills at all: 32-bit-only
   typed Dst views, stable RWC/layout base, all-lanes CC everywhere,
   and no opacity.  Collect the used Dst immediates.  */

static void
scan_spill_legality (function *fn, spill_ctx &ctx)
{
  ctx.ok = true;
  ctx.refusal = NULL;
  ctx.detail = NULL;
  ctx.at = NULL;

  ctx.noinc_addr_mode = rvtt_no_increment_address_mode ();
  if (ctx.noinc_addr_mode < 0)
    {
      refuse (ctx, "lreg-spill-no-free-dst", "no-increment-mode-unproven",
	      NULL);
      return;
    }

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (GET_CODE (PATTERN (insn)) == USE
	      || GET_CODE (PATTERN (insn)) == CLOBBER)
	    continue;

	  int code = recog_memoized (insn);
	  /* The zero-length LREG metadata patterns (raw-access sentinels
	     and user read/write markers) emit nothing and have no Dst,
	     RWC, configuration, or CC effect; the raw .ttinsn region
	     they bracket is an asm and refuses below on its own.  */
	  if (sentinel_read_lregno (code) >= 0
	      || sentinel_write_lregno (code) >= 0)
	    continue;

	  if (pattern_transparent_p (insn))
	    continue;
	  /* The compiler's own CC bracket patterns: any bracket implies
	     a narrowed region somewhere -- function-coarse refusal.  */
	  if (code == CODE_FOR_rvtt_sfppushc
	      || code == CODE_FOR_rvtt_sfppopc
	      || code == CODE_FOR_rvtt_sfpcompc)
	    {
	      refuse (ctx, "cc-enable-unproved", "cc-bracket-present", insn);
	      return;
	    }

	  /* The predicated-assign copy: a pure CC-reading LREG move.  */
	  {
	    rtx pat = PATTERN (insn);
	    if (GET_CODE (pat) == SET
		&& GET_CODE (SET_SRC (pat)) == UNSPEC_VOLATILE
		&& XINT (SET_SRC (pat), 1) == UNSPECV_SFPASSIGN)
	      continue;
	  }

	  bool overridden = false;
	  for (const lpa_effect_override &o : effect_overrides)
	    if (code == o.code)
	      {
		if (o.cc_writes)
		  {
		    refuse (ctx, "cc-enable-unproved",
			    "mod-conditional-cc-write", insn);
		    return;
		  }
		overridden = true;
		break;
	      }
	  if (overridden)
	    continue;

	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque)
	    {
	      refuse (ctx, "dst-rwc-effect-unproved", "opaque-insn", insn);
	      return;
	    }
	  if (e.rwc.kind != xtt_rwc_effect_t::NONE)
	    {
	      refuse (ctx, "dst-rwc-effect-unproved", "rwc-boundary", insn);
	      return;
	    }
	  if (e.config_dests_written != 0 || e.addr_mod_slot_write)
	    {
	      refuse (ctx, "dst-rwc-effect-unproved", "layout-boundary", insn);
	      return;
	    }
	  if (e.cc_write && !e.cc_write_all_lanes)
	    {
	      refuse (ctx, "cc-enable-unproved", "cc-write-not-all-lanes",
		      insn);
	      return;
	    }

	  if (e.dst_mem_read || e.dst_mem_write)
	    {
	      rtx addr, mode, addr_mode;
	      if (!rvtt_dst_access_operands (insn, e, &addr, &mode,
					     &addr_mode))
		{
		  refuse (ctx, "dst-rwc-effect-unproved",
			  "unaudited-dst-access", insn);
		  return;
		}
	      if (!CONST_INT_P (mode))
		{
		  refuse (ctx, "lreg-spill-inexact-dst-mode",
			  "mode-nonconstant", insn);
		  return;
		}
	      HOST_WIDE_INT m = INTVAL (mode);
	      if (m == 0)
		{
		  refuse (ctx, "lreg-spill-inexact-dst-mode",
			  "srcb-runtime-resolved", insn);
		  return;
		}
	      if (m == 10)
		{
		  refuse (ctx, "lreg-spill-no-free-dst",
			  "int32-all-masks-rwc-base", insn);
		  return;
		}
	      if (!dst_mode_32bit_p (m))
		{
		  refuse (ctx, "lreg-spill-inexact-dst-mode",
			  "16-bit-dst-format", insn);
		  return;
		}
	      if (!CONST_INT_P (addr))
		{
		  refuse (ctx, "lreg-spill-no-free-dst",
			  "address-nonconstant", insn);
		  return;
		}
	      ctx.used_rows.safe_push (INTVAL (addr));
	    }
	}
    }
}

/* A scratch immediate S may alias a used immediate K only when they
   are congruent within +/-3 modulo 256 (see the file comment).  */

static bool
rows_may_alias_p (HOST_WIDE_INT s, HOST_WIDE_INT k)
{
  HOST_WIDE_INT d = (s - k) % 256;
  if (d < 0)
    d += 256;
  return d <= 3 || d >= 253;
}

/* Pick the highest proven-free 4-aligned scratch row in [0, 252].
   Returns -1 when none exists.  */

static HOST_WIDE_INT
choose_scratch_row (spill_ctx &ctx)
{
  for (HOST_WIDE_INT s = 252; s >= 0; s -= 4)
    {
      bool clash = false;
      for (HOST_WIDE_INT k : ctx.used_rows)
	if (rows_may_alias_p (s, k))
	  {
	    clash = true;
	    break;
	  }
      if (!clash)
	{
	  ctx.used_rows.safe_push (s);
	  return s;
	}
    }
  return -1;
}

/* --------------------------- web collection ------------------------ */

struct lpa_web
{
  unsigned regno;
  int precolor;			/* -1, or the pinned LREG index */
  bool reservation;		/* livein sentinel: never spill */
  bool reload_tmp;		/* spill-generated: never spill */
  unsigned occ;			/* occurrence count (spill cost) */
};

struct lpa_graph
{
  auto_vec<lpa_web> webs;
  auto_vec<int> node_of_reg;	/* regno -> node, -1 */
  sbitmap conflicts;		/* n*n symmetric matrix */
  auto_vec<unsigned> degree;
  const char *fail;		/* fail-closed collection refusal */
  rtx_insn *fail_at;

  lpa_graph () : conflicts (NULL), fail (NULL), fail_at (NULL) {}
  ~lpa_graph ()
  {
    if (conflicts)
      sbitmap_free (conflicts);
  }

  bool conflict_p (unsigned i, unsigned j) const
  {
    return bitmap_bit_p (conflicts, i * webs.length () + j);
  }
  void add_conflict (unsigned i, unsigned j)
  {
    if (i == j)
      return;
    unsigned n = webs.length ();
    if (!bitmap_bit_p (conflicts, i * n + j))
      {
	bitmap_set_bit (conflicts, i * n + j);
	bitmap_set_bit (conflicts, j * n + i);
	degree[i]++;
	degree[j]++;
      }
  }
};

static int
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

static int
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

static bool
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

static void
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

/* ------------------------------ DSATUR ----------------------------- */

/* DSATUR over SFPU_REG_NUM colors with precolored nodes fixed.
   Deterministic: saturation desc, degree desc, node index asc.
   Returns true when fully colored; otherwise *BLOCKED names a node
   with a saturated palette.  */

static bool
dsatur_color (const lpa_graph &g, auto_vec<int> &color, int *blocked)
{
  unsigned n = g.webs.length ();
  color.truncate (0);
  color.safe_grow_cleared (n);
  for (unsigned i = 0; i < n; i++)
    color[i] = -1;

  /* Fix precolored nodes first; equal-precolor conflicts block.  */
  for (unsigned i = 0; i < n; i++)
    if (g.webs[i].precolor >= 0)
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
	  if (color[i] >= 0)
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

/* Deterministic spill choice around BLOCKED: the cheapest spillable
   web among the blocked node and its neighbors
   (cost = occurrences scaled down by degree).  -1 when none.  */

static int
choose_spill_web (const lpa_graph &g, int blocked)
{
  unsigned n = g.webs.length ();
  int best = -1;
  HOST_WIDE_INT best_cost = 0;
  for (unsigned i = 0; i < n; i++)
    {
      if ((int) i != blocked && !g.conflict_p (blocked, i))
	continue;
      const lpa_web &w = g.webs[i];
      if (w.reservation || w.reload_tmp)
	continue;
      HOST_WIDE_INT cost
	= (HOST_WIDE_INT) w.occ * 1024 / (g.degree[i] + 1);
      if (best < 0 || cost < best_cost
	  || (cost == best_cost && w.regno < g.webs[best].regno))
	{
	  best = i;
	  best_cost = cost;
	}
    }
  return best;
}

/* --------------------------- spill rewrite -------------------------- */

/* Spill web REGNO through Dst scratch row ROW: SFPSTORE mod0 4 after
   each def, SFPLOAD mod0 4 before each reading insn, fresh pseudo per
   insn.  New pseudos are recorded in SPILL_TMPS.  Returns the number
   of memory round-trip insns emitted.  */

static unsigned
spill_web (function *fn, unsigned regno, HOST_WIDE_INT row, int addr_mode,
	   bitmap spill_tmps)
{
  rtx preg = regno_reg_rtx[regno];
  unsigned emitted = 0;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn, *next;
      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb)); insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  bool reads = reg_referenced_p (preg, PATTERN (insn));
	  bool writes = reg_set_p (preg, insn);
	  if (!reads && !writes)
	    continue;
	  gcc_assert (GET_CODE (PATTERN (insn)) != USE);

	  rtx q = gen_reg_rtx (XTT32SImode);
	  bitmap_set_bit (spill_tmps, REGNO (q));

	  if (reads)
	    {
	      rtx_insn *reload = emit_insn_before (
		gen_rvtt_sfpload_lv_int (q, const0_rtx, const0_rtx,
					 const0_rtx, GEN_INT (row),
					 rvtt_gen_rtx_noval (XTT32SImode),
					 rvtt_gen_rtx_noval (XTT32SImode),
					 GEN_INT (4 /* INT32 */),
					 GEN_INT (addr_mode)),
		insn);
	      emitted++;
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: reload insn %d (r%u -> r%u) before "
			 "insn %d from Dst row " HOST_WIDE_INT_PRINT_DEC "\n",
			 INSN_UID (reload), regno, REGNO (q),
			 INSN_UID (insn), row);
	    }

	  bool ok = validate_replace_rtx (preg, q, insn);
	  gcc_assert (ok);

	  if (writes)
	    {
	      rtx_insn *store = emit_insn_after (
		gen_rvtt_sfpstore_int (const0_rtx, const0_rtx, const0_rtx,
				       GEN_INT (row), q,
				       GEN_INT (4 /* INT32 */),
				       GEN_INT (addr_mode)),
		insn);
	      emitted++;
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: spill store insn %d (r%u via r%u) "
			 "after insn %d to Dst row "
			 HOST_WIDE_INT_PRINT_DEC "\n",
			 INSN_UID (store), regno, REGNO (q),
			 INSN_UID (insn), row);
	    }
	}
    }
  return emitted;
}

/* --------------------------- enforcement --------------------------- */

static void
dump_spill_refusal (const spill_ctx &ctx)
{
  if (dump_file)
    fprintf (dump_file,
	     "lreg-alloc spill-refusal: %s (%s) at insn %d; "
	     "keeping lreg-pressure-exceeded\n",
	     ctx.refusal, ctx.detail ? ctx.detail : "",
	     ctx.at ? INSN_UID (ctx.at) : -1);
}

static unsigned
enforce_colorability (function *fn)
{
  unsigned peak = function_peak_pressure (fn);
  if (peak <= SFPU_REG_NUM)
    {
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc: peak pressure %u within the %u-LREG file; "
		 "no-op (colorability=trivial)\n",
		 peak, SFPU_REG_NUM);
      return 0;
    }

  if (dump_file)
    fprintf (dump_file,
	     "lreg-alloc: peak pressure %u exceeds the %u-LREG file; "
	     "engaging DSATUR coloring\n",
	     peak, SFPU_REG_NUM);

  spill_ctx ctx;
  bool ctx_scanned = false;
  auto_bitmap spill_tmps;
  unsigned spills = 0, emitted = 0;
  bool mutated = false;

  const unsigned max_rounds = 256;
  for (unsigned round = 0; round < max_rounds; round++)
    {
      lpa_graph g;
      build_graph (fn, g, spill_tmps);
      if (g.fail)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc refusal: %s at insn %d; "
		     "keeping lreg-pressure-exceeded\n",
		     g.fail, g.fail_at ? INSN_UID (g.fail_at) : -1);
	  return mutated ? TODO_df_finish : 0;
	}

      auto_vec<int> color;
      int blocked = -1;
      if (dsatur_color (g, color, &blocked))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: %u web(s) DSATUR-colored with %u colors "
		     "after %u spill(s), %u round-trip insn(s); "
		     "colorability=proven\n",
		     g.webs.length (), SFPU_REG_NUM, spills, emitted);
	  return mutated ? TODO_df_finish : 0;
	}

      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc: round %u: %u webs, DSATUR blocked at web "
		 "r%u (degree %u)\n",
		 round, g.webs.length (), g.webs[blocked].regno,
		 g.degree[blocked]);

      if (!ctx_scanned)
	{
	  scan_spill_legality (fn, ctx);
	  ctx_scanned = true;
	  if (!ctx.ok)
	    {
	      dump_spill_refusal (ctx);
	      return 0;		/* refusals never mutate */
	    }
	}

      int victim = choose_spill_web (g, blocked);
      if (victim < 0)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc refusal: lreg-spill-no-candidate "
		     "(only reservations/reload temporaries in the blocked "
		     "neighborhood); keeping lreg-pressure-exceeded\n");
	  return mutated ? TODO_df_finish : 0;
	}

      HOST_WIDE_INT row = choose_scratch_row (ctx);
      if (row < 0)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc spill-refusal: lreg-spill-no-free-dst "
		     "(scratch rows exhausted); "
		     "keeping lreg-pressure-exceeded\n");
	  return mutated ? TODO_df_finish : 0;
	}

      unsigned vregno = g.webs[victim].regno;
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc: spilling web r%u (occ %u, degree %u) to Dst "
		 "scratch row " HOST_WIDE_INT_PRINT_DEC
		 " (mod0 4 INT32 round trip, addr_mode %d)\n",
		 vregno, g.webs[victim].occ, g.degree[victim], row,
		 ctx.noinc_addr_mode);

      emitted += spill_web (fn, vregno, row, ctx.noinc_addr_mode,
			    spill_tmps);
      spills++;
      mutated = true;
      df_analyze ();
    }

  if (dump_file)
    fprintf (dump_file,
	     "lreg-alloc refusal: round limit reached; "
	     "keeping lreg-pressure-exceeded\n");
  return mutated ? TODO_df_finish : 0;
}

/* ------------------------------- pass ------------------------------ */

const pass_data pass_data_rvtt_lp_alloc =
{
  RTL_PASS, /* type */
  "rvtt_lp_alloc", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_lp_alloc : public rtl_opt_pass
{
public:
  pass_rvtt_lp_alloc (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lp_alloc, ctxt)
  {}

  bool gate (function *) final override
  {
    return optimize > 0 && (TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH)
      && (riscv_tt_opt_pressure_schedule || riscv_tt_opt_lreg_alloc);
  }

  unsigned execute (function *fn) final override
  {
    df_note_add_problem ();
    df_analyze ();
    if (riscv_tt_opt_pressure_schedule)
      audit_function (fn);
    unsigned todo = TODO_df_finish;
    if (riscv_tt_opt_lreg_alloc)
      todo |= enforce_colorability (fn);
    return todo;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_lp_alloc (gcc::context *ctxt)
{
  return new pass_rvtt_lp_alloc (ctxt);
}
