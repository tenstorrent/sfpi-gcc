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
	encode_fp32/decode_fp32 involution, verified bit-exact over all
	2^32 patterns in the adjudicated simulator; FP32 mod0 3 is NOT
	used because the BH store flushes denormals).

      - The whole allocation is TRANSACTIONAL: every emitted round-trip
	insn and every operand rewrite is recorded, and any refusal
	discovered after mutation (no spillable candidate, scratch rows
	exhausted, a rewrite the insn does not admit, the round limit)
	rolls the stream back to the pre-allocation shape before
	returning, so every refusal path hands the post-RA spill
	diagnosis exactly today's stream.

      - After spilling makes the graph 8-colorable, register
	assignment is deliberately left to IRA: the DSATUR verdict is
	the colorability certificate, and delegating assignment keeps
	IRA's coalescing and guarantees untouched functions allocate
	exactly as before.  If IRA still spills (it is not an optimal
	colorer), the post-RA rtl-rvtt-spill-diag.cc named error
	remains the backstop.

   Bit-exactness gates (each failure is a named refusal that keeps
   today's lreg-pressure-exceeded error; refusals either precede any
   mutation or roll the stream back transactionally):

      - lreg-spill-inexact-dst-mode: the spill round trip is bit-exact
	only through the 32-bit Dst formats.  Any typed Dst access in
	the function carrying an AFFIRMATIVE 16-bit data mode (FP16A/
	FP16B/INT8/UINT16/INT16/INT8_COMP/LO16_ONLY/HI16_ONLY) or a
	non-constant mode operand proves a 16-bit (or unprovable) Dst
	view and refuses.  Runtime-resolved SRCB accesses (mod0 0 --
	every plain SFPI dst_reg[] access) and functions with no typed
	Dst access at all are admitted SOLELY under the explicit
	integration-layer declaration -mtt-tensix-dst-layout-32b (DP-9:
	an in-function 32-bit access is NOT layout proof -- a mixed-view
	kernel can park through an explicit 32-bit view while its SRCB
	accesses resolve 16-bit); evidence only ever REFUSES, so an
	affirmative 16-bit access refuses even against the declaration.

      - lreg-spill-no-free-dst: scratch rows are derived from the
	function's own typed Dst addresses.  All of them must be
	CONST_INT and reachable at a PROVEN RWC delta (typed INC/FACE
	effects contribute audited deltas; TTSETRWC, opaque insns and
	disagreeing joins make the delta unknown), and the layout must
	be free of config/address-modifier writes, because a Dst
	address is base-relative ((imm + RWC_Dst + MATH_Offset +
	REGW_Base) & 0x3FF).  Two accesses can only touch the same physical rows
	when their immediates are congruent within +/-3 modulo 256
	(the dst32b_adjust_row aliasing window; the same physical-row
	model gimple-rvtt-transp-involution.cc:486 audits), so a
	scratch row is proven free when its immediate keeps that
	distance from every kernel immediate.  Loads carrying mod0 10
	(INT32_ALL) refuse: that mode masks the RWC base (offset &= 3)
	and breaks the shared-base disjointness proof.

      - cc-enable-unproved: SFPSTORE and SFPLOAD move only CC-enabled
	lanes and no all-lanes store variant exists, so the round trip
	is complete only under all-lanes CC.  A point-wise CC lattice
	(the rtl-rvtt-dst-ownership.cc machinery: PUSHC/POPC stack,
	COMPC/typed writes narrow, the proven all-lanes SFPENCC
	restores) annotates every insn; a web is spillable only when
	every occurrence point -- store points use the AFTER-insn
	state -- is provably all-lanes.  Webs touched inside predicated
	regions are simply not candidates; others still relieve the
	pressure.

      - RWC motion: the epoch/offset lattice tracks the Dst counter
	symbolically (typed INC/FACE effects add audited deltas within
	an epoch; disagreeing joins mint the block's stable epoch
	token, so a row loop's whole body shares the header's epoch;
	TTSETRWC or any unproven effect clears the proof).  Kernel rows
	are recorded epoch-relative and every spill immediate is
	compensated per point (S - off), so face-advancing and
	row-looping kernels spill correctly; rows or spill points at an
	unknown or foreign epoch refuse (lreg-spill-no-free-dst), and a
	web live into a minted join (loop-carried across the rwc
	backedge) is never a candidate.

      - lreg-spill-laneconfig-unproven: SFPLOAD/SFPSTORE lane-to-cell
	addressing is redirected by the LaneConfig column-exchange bits
	(DEST_RD_COL_EXCHANGE / DEST_WR_COL_EXCHANGE) and gated by the
	per-lane block bits (BLOCK_SFPU_RD_FROM_DEST /
	BLOCK_DEST_WR_FROM_SFPU): under a nondefault LaneConfig the
	round trip silently moves or drops lanes.  Any function-local
	SFPCONFIG write to dest 15 (LaneConfig) therefore refuses by
	this name (other config writes refuse as layout boundaries).

      - dst-rwc-effect-unproved: any opaque instruction (call, asm,
	unaudited pattern), RWC boundary, or layout boundary refuses,
	per the vocabulary of SFPLOADMACRO_FORMATION.md.

   Ambient contracts the flag carries (function-local analysis cannot
   see state established before the function; these are the same trust
   boundary as the ambient all-lanes CC contract the shipped CC
   synthesis bakes in, and they are spelled out in the flag's
   documentation):

      - ambient Dst data width: the flag asserts the calc body runs
	under a 32-bit-row Dst layout (SFPU_Fp32_enabled /
	dst_32bit_addr_en) and that the surrounding kernel does not
	view the scratch rows through a 16-bit format.  This is what
	admits mod0-0 (SRCB-resolved) accesses and zero-access
	functions; an affirmative 16-bit access in the function still
	refuses.

      - ambient LaneConfig: the architectural default (no column
	exchange, no block bits -- the simulator's reset state, and
	what the LLK init sequence leaves in place per the audited
	dest-15 table) is assumed to be active at the calc body.

      - concurrent Dst consumers: scratch rows are proven free only
	against the FUNCTION'S OWN typed accesses.  The surrounding
	kernel contract is that no concurrent consumer (the packer
	reading result tiles, a neighbouring thread) touches Dst rows
	the calc body does not itself address while it runs.  A kernel
	that violates this cannot be detected function-locally; the
	CRAQ gate on every newly-compiling kernel is the empirical
	backstop.

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
#include "cfgloop.h"
#include "expr.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "diagnostic-core.h"
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

struct spill_ctx
{
  bool ok;
  const char *refusal;		/* named refusal */
  const char *detail;
  rtx_insn *at;
  int noinc_addr_mode;
  /* Epoch-relative Dst rows the function's typed accesses touch
     (immediate + proven offset, with the epoch token), plus assigned
     scratch offsets.  */
  auto_vec<HOST_WIDE_INT> used_rows;
  auto_vec<int> row_epoch;
  bool have_dst_access;
  bool have_mod0_srcb;		/* runtime-resolved (mod0 0) access */
  /* Per-insn recorded states, indexed by INSN_UID.  */
  auto_vec<uint8_t> cc_before, cc_after;
  auto_vec<int> epoch_before, epoch_after;
  auto_vec<int> off_before, off_after;
  auto_vec<bool> known_before, known_after;
  /* Blocks whose in-state minted a fresh epoch: values live into them
     cross a base-identity boundary and are never spilled.  */
  auto_vec<basic_block> minted_bbs;
  /* Per-bb sweep metadata for minted epochs (DP-11): the audited
     per-iteration step (0 = unproven) and the proven max trip count
     (-1 = unproven) of the epoch's own cycle.  A spill in a minted
     epoch is admitted only when both are proven, and its scratch
     window is checked across the WHOLE swept range.  */
  auto_vec<int> mint_step;
  auto_vec<int> mint_trips;
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

  for (const lpa_effect_override &o : effect_overrides)
    if (code == o.code)
      {
	if (o.cc_writes)
	  /* Mod-conditional CC write (SFPEXEXP/SFPLZ/SFPIADD),
	     recorded conservatively: the lane state narrows.  */
	  s.cc = LPA_CC_OTHER;
	return;
      }

  if (pattern_transparent_p (insn))
    return;

  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    {
      if (collect)
	refuse (*collect, "dst-rwc-effect-unproved", "opaque-insn", insn);
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
	refuse (*collect, "lreg-spill-laneconfig-unproven",
		"laneconfig-written-in-function", insn);
    }
  else if (e.config_dests_written != 0 || e.addr_mod_slot_write)
    {
      /* Any other configuration write can change the Dst layout or
	 address-modifier interpretation under the accesses.  */
      if (collect)
	refuse (*collect, "lreg-spill-no-free-dst", "layout-boundary", insn);
    }

  /* Typed Dst accesses read the counter BEFORE this insn's own RWC
     effect applies; collect rows first.  */
  if (collect && (e.dst_mem_read || e.dst_mem_write))
    {
      spill_ctx &ctx = *collect;
      ctx.have_dst_access = true;
      rtx addr, mode, addr_mode;
      if (!rvtt_dst_access_operands (insn, e, &addr, &mode, &addr_mode))
	refuse (ctx, "dst-rwc-effect-unproved", "unaudited-dst-access", insn);
      else if (!CONST_INT_P (mode))
	refuse (ctx, "lreg-spill-inexact-dst-mode", "mode-nonconstant", insn);
      else
	{
	  HOST_WIDE_INT m = INTVAL (mode);
	  /* mod0 0 (FMT_SRCB) resolves at runtime from the ALU
	     configuration: admitted only under the declared or
	     evidenced 32-bit-row layout (checked at scan end);
	     affirmative 16-bit formats refuse regardless.  */
	  if (m == 10)
	    refuse (ctx, "lreg-spill-no-free-dst",
		    "int32-all-masks-rwc-base", insn);
	  else if (m != 0 && !dst_mode_32bit_p (m) && e.dst_mem_read)
	    /* An EXPLICIT 16-bit-format READ of Dst is layout
	       counter-evidence (its author knows the rows are 16-bit)
	       and refuses even against the declaration.  A 16-bit-
	       format STORE is an output-format conversion, routine in
	       declared-32-bit kernels (the store writes through the
	       32-bit geometry there); it is admitted and its row
	       accounted like any other.  */
	    refuse (ctx, "lreg-spill-inexact-dst-mode",
		    "16-bit-dst-format", insn);
	  else if (!CONST_INT_P (addr))
	    refuse (ctx, "lreg-spill-no-free-dst",
		    "address-nonconstant", insn);
	  else if (!s.known)
	    refuse (ctx, "lreg-spill-no-free-dst",
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
      refuse (ctx, "lreg-spill-inexact-dst-mode", "dst-layout-undeclared",
	      NULL);
      return;
    }

  /* Rows in more than one epoch cannot all be proven disjoint from
     one scratch base.  */
  for (unsigned i = 1; i < ctx.row_epoch.length (); i++)
    if (ctx.row_epoch[i] != ctx.row_epoch[0])
      {
	refuse (ctx, "lreg-spill-no-free-dst", "cross-epoch-rows", NULL);
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

static HOST_WIDE_INT
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

/* Consumers whose LREG/Dst WRITES are lane-gated and whose dataflow
   is lane-local (no value movement between lanes): a reload feeding
   only such an insn is complete under narrowed CC -- the reload's
   disabled-lane garbage is computed on but never written through the
   lane gate -- and an RMW def by such an insn under narrowed CC
   store-backs only its enabled lanes while the scratch keeps the old
   disabled lanes, which is exactly the predicated-write semantics.
   Cross-lane ops (SFPSWAP/SFPTRANSP/SFPSHFT2/SELECT/CONCAT), plain
   all-lanes copies (rvtt_sfpassign SETs, SFPMOV mod 2), SrcS stores,
   loadmacro forms and the zero-length LREG markers are deliberately
   ABSENT: fail-closed allowlist, generated from the audited
   lane-local families.  */

static const insn_code lane_gated_consumers[] = {
  CODE_FOR_rvtt_sfpabs,
  CODE_FOR_rvtt_sfpabs_lv,
  CODE_FOR_rvtt_sfpabs_nv,
  CODE_FOR_rvtt_sfpadd,
  CODE_FOR_rvtt_sfpaddi,
  CODE_FOR_rvtt_sfpaddi_int_lv,
  CODE_FOR_rvtt_sfpaddi_lv,
  CODE_FOR_rvtt_sfpadd_lv,
  CODE_FOR_rvtt_sfpand,
  CODE_FOR_rvtt_sfpand_lv,
  CODE_FOR_rvtt_sfpand_lv_2op,
  CODE_FOR_rvtt_sfpand_lv_bh,
  CODE_FOR_rvtt_sfparecip,
  CODE_FOR_rvtt_sfparecip_lv,
  CODE_FOR_rvtt_sfpcast,
  CODE_FOR_rvtt_sfpcast_lv,
  CODE_FOR_rvtt_sfpdivp2,
  CODE_FOR_rvtt_sfpdivp2_lv,
  CODE_FOR_rvtt_sfpdivp2_lv_int,
  CODE_FOR_rvtt_sfpexexp,
  CODE_FOR_rvtt_sfpexexp_lv,
  CODE_FOR_rvtt_sfpexexp_nv,
  CODE_FOR_rvtt_sfpexman,
  CODE_FOR_rvtt_sfpexman_lv,
  CODE_FOR_rvtt_sfpexman_nv,
  CODE_FOR_rvtt_sfpiadd_i,
  CODE_FOR_rvtt_sfpiadd_i_lv,
  CODE_FOR_rvtt_sfpiadd_i_lv_int,
  CODE_FOR_rvtt_sfpiadd_i_nv,
  CODE_FOR_rvtt_sfpiadd_v,
  CODE_FOR_rvtt_sfpiadd_v_lv,
  CODE_FOR_rvtt_sfpiadd_v_nv,
  CODE_FOR_rvtt_sfploadi,
  CODE_FOR_rvtt_sfploadi_lv,
  CODE_FOR_rvtt_sfploadi_lv_int,
  CODE_FOR_rvtt_sfplut,
  CODE_FOR_rvtt_sfplutfp32_3r,
  CODE_FOR_rvtt_sfplutfp32_3r_split,
  CODE_FOR_rvtt_sfplutfp32_6r,
  CODE_FOR_rvtt_sfplz,
  CODE_FOR_rvtt_sfplz_lv,
  CODE_FOR_rvtt_sfplz_nv,
  CODE_FOR_rvtt_sfpmad,
  CODE_FOR_rvtt_sfpmad_lv,
  CODE_FOR_rvtt_sfpmov,
  CODE_FOR_rvtt_sfpmov_lv,
  CODE_FOR_rvtt_sfpmov_nv,
  CODE_FOR_rvtt_sfpmul,
  CODE_FOR_rvtt_sfpmul24,
  CODE_FOR_rvtt_sfpmul24_lv,
  CODE_FOR_rvtt_sfpmuli,
  CODE_FOR_rvtt_sfpmuli_int_lv,
  CODE_FOR_rvtt_sfpmuli_lv,
  CODE_FOR_rvtt_sfpmul_lv,
  CODE_FOR_rvtt_sfpnonlinear,
  CODE_FOR_rvtt_sfpnonlinear_lv,
  CODE_FOR_rvtt_sfpnot,
  CODE_FOR_rvtt_sfpnot_lv,
  CODE_FOR_rvtt_sfpor,
  CODE_FOR_rvtt_sfpor_lv,
  CODE_FOR_rvtt_sfpor_lv_2op,
  CODE_FOR_rvtt_sfpor_lv_bh,
  CODE_FOR_rvtt_sfpsetcc_i,
  CODE_FOR_rvtt_sfpsetcc_v,
  CODE_FOR_rvtt_sfpsetexp_i,
  CODE_FOR_rvtt_sfpsetexp_i_lv,
  CODE_FOR_rvtt_sfpsetexp_i_lv_int,
  CODE_FOR_rvtt_sfpsetexp_v,
  CODE_FOR_rvtt_sfpsetexp_v_lv,
  CODE_FOR_rvtt_sfpsetman_i,
  CODE_FOR_rvtt_sfpsetman_i_lv,
  CODE_FOR_rvtt_sfpsetman_i_lv_int,
  CODE_FOR_rvtt_sfpsetman_v,
  CODE_FOR_rvtt_sfpsetman_v_lv,
  CODE_FOR_rvtt_sfpsetsgn_i,
  CODE_FOR_rvtt_sfpsetsgn_i_lv,
  CODE_FOR_rvtt_sfpsetsgn_i_lv_int,
  CODE_FOR_rvtt_sfpsetsgn_v,
  CODE_FOR_rvtt_sfpsetsgn_v_lv,
  CODE_FOR_rvtt_sfpshft_i,
  CODE_FOR_rvtt_sfpshft_i_lv,
  CODE_FOR_rvtt_sfpshft_i_lv_int,
  CODE_FOR_rvtt_sfpshft_v,
  CODE_FOR_rvtt_sfpshft_v_lv,
  CODE_FOR_rvtt_sfpshft_v_lv_int,
  CODE_FOR_rvtt_sfpstochrnd_i,
  CODE_FOR_rvtt_sfpstochrnd_i_lv,
  CODE_FOR_rvtt_sfpstochrnd_i_lv_int,
  CODE_FOR_rvtt_sfpstochrnd_v,
  CODE_FOR_rvtt_sfpstochrnd_v_lv,
  CODE_FOR_rvtt_sfpstore,
  CODE_FOR_rvtt_sfpstore_int,
  CODE_FOR_rvtt_sfpxor,
  CODE_FOR_rvtt_sfpxor_lv,
  CODE_FOR_rvtt_sfpxor_lv_2op,
  CODE_FOR_rvtt_sfpxor_lv_bh,
};

static bool
lane_gated_consumer_p (rtx_insn *insn)
{
  int code = recog_memoized (insn);
  for (insn_code c : lane_gated_consumers)
    if (code == c)
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
   *WHY/*MAX_DELTA describe the choice or the cheapest inadmissible
   candidate's blocker.  */

static int
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
      int woff = 0, wepoch = 0;
      const char *wwhy = NULL;
      if (!web_spill_admissible_p (fn, w.regno, ctx, &woff, &wepoch, &wwhy))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: candidate r%u inadmissible (%s)\n",
		     w.regno, wwhy);
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

/* --------------------------- spill rewrite -------------------------- */

/* Transaction log: everything the allocator does to the stream, so any
   later refusal can roll the function back to its pre-allocation
   shape.  */

struct spill_transaction
{
  auto_vec<rtx_insn *> emitted;		/* round-trip insns, delete on undo */
  auto_vec<rtx_insn *> replaced_insn;	/* operand rewrites, revert on undo */
  auto_vec<rtx> replaced_from;
  auto_vec<rtx> replaced_to;
  unsigned n_round_trips () const { return emitted.length (); }
};

static void
rollback (spill_transaction &tx)
{
  for (unsigned i = tx.replaced_insn.length (); i-- > 0;)
    {
      /* Reversing a just-validated reg-for-reg replacement re-forms the
	 exact original pattern; recog cannot answer differently.  */
      bool ok = validate_replace_rtx (tx.replaced_to[i],
				      tx.replaced_from[i],
				      tx.replaced_insn[i]);
      gcc_assert (ok);
    }
  for (unsigned i = tx.emitted.length (); i-- > 0;)
    delete_insn (tx.emitted[i]);
  tx.emitted.truncate (0);
  tx.replaced_insn.truncate (0);
  tx.replaced_from.truncate (0);
  tx.replaced_to.truncate (0);
}

/* Spill web REGNO through the entry-relative Dst scratch offset X:
   SFPSTORE mod0 4 after each def, SFPLOAD mod0 4 before each reading
   insn, fresh pseudo per insn, each immediate compensated by the
   proven RWC delta at its point (X - delta names the same physical
   row everywhere).  New pseudos are recorded in SPILL_TMPS; every
   stream change is recorded in TX.  Returns false with *WHY named
   when some insn does not admit the rewrite (the caller rolls the
   whole transaction back) -- never asserts after emission.  */

static bool
spill_web (function *fn, unsigned regno, HOST_WIDE_INT x, int addr_mode,
	   const spill_ctx &ctx, bitmap spill_tmps, spill_transaction &tx,
	   const char **why)
{
  rtx preg = regno_reg_rtx[regno];

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn, *next;
      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb)); insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (GET_CODE (PATTERN (insn)) == USE)
	    {
	      /* Bare USEs belong to the livein reservation sentinels,
		 which are never chosen for spilling; skip them.  A bare
		 USE of the web being spilled would be an interval this
		 pass does not understand: refuse it by name.  */
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
	      *why = "lreg-alloc-post-scan-insn";
	      return false;
	    }
	  HOST_WIDE_INT imm_r = x - ctx.off_before[uid];
	  HOST_WIDE_INT imm_w = x - ctx.off_after[uid];
	  if ((reads && (imm_r < 0 || imm_r > 1023))
	      || (writes && (imm_w < 0 || imm_w > 1023)))
	    {
	      *why = "lreg-spill-no-free-dst";
	      return false;
	    }

	  rtx q = gen_reg_rtx (XTT32SImode);
	  bitmap_set_bit (spill_tmps, REGNO (q));

	  /* Rewrite first: a refused rewrite must precede any emission
	     for this insn (the caller still rolls back prior ones).  */
	  if (!validate_replace_rtx (preg, q, insn))
	    {
	      *why = "lreg-spill-rewrite-refused";
	      return false;
	    }
	  tx.replaced_insn.safe_push (insn);
	  tx.replaced_from.safe_push (preg);
	  tx.replaced_to.safe_push (q);

	  if (reads)
	    {
	      rtx_insn *reload = emit_insn_before (
		gen_rvtt_sfpload_lv_int (q, const0_rtx, const0_rtx,
					 const0_rtx, GEN_INT (imm_r),
					 rvtt_gen_rtx_noval (XTT32SImode),
					 rvtt_gen_rtx_noval (XTT32SImode),
					 GEN_INT (4 /* INT32 */),
					 GEN_INT (addr_mode)),
		insn);
	      tx.emitted.safe_push (reload);
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: reload insn %d (r%u -> r%u) before "
			 "insn %d from Dst row " HOST_WIDE_INT_PRINT_DEC
			 " (offset " HOST_WIDE_INT_PRINT_DEC " - delta %d)\n",
			 INSN_UID (reload), regno, REGNO (q),
			 INSN_UID (insn), imm_r, x, ctx.off_before[uid]);
	    }

	  if (writes)
	    {
	      rtx_insn *store = emit_insn_after (
		gen_rvtt_sfpstore_int (const0_rtx, const0_rtx, const0_rtx,
				       GEN_INT (imm_w), q,
				       GEN_INT (4 /* INT32 */),
				       GEN_INT (addr_mode)),
		insn);
	      tx.emitted.safe_push (store);
	      if (dump_file)
		fprintf (dump_file,
			 "lreg-alloc: spill store insn %d (r%u via r%u) "
			 "after insn %d to Dst row " HOST_WIDE_INT_PRINT_DEC
			 " (offset " HOST_WIDE_INT_PRINT_DEC " - delta %d)\n",
			 INSN_UID (store), regno, REGNO (q),
			 INSN_UID (insn), imm_w, x, ctx.off_after[uid]);
	    }
	}
    }
  return true;
}

/* --------------------------- enforcement --------------------------- */

/* Name a refusal to the user (the enabled allocator stood down; the
   post-RA lreg-pressure-exceeded error follows).  The parenthesized
   name is the machine-parseable token, mirroring the spill-diag error
   format.  Notes appear only under the flag on refusing compilations;
   flag-off diagnostics are untouched.  */

static void
inform_refusal (function *fn, const char *name, const char *detail,
		rtx_insn *at)
{
  location_t loc = (at && INSN_HAS_LOCATION (at))
    ? INSN_LOCATION (at) : fn->function_start_locus;
  inform (loc,
	  "SFPU LREG allocator refused to spill (%s): %s; "
	  "the register-pressure error stands",
	  name, detail ? detail : "unproven");
}

static void
dump_spill_refusal (function *fn, const spill_ctx &ctx)
{
  if (dump_file)
    fprintf (dump_file,
	     "lreg-alloc spill-refusal: %s (%s) at insn %d; "
	     "keeping lreg-pressure-exceeded\n",
	     ctx.refusal, ctx.detail ? ctx.detail : "",
	     ctx.at ? INSN_UID (ctx.at) : -1);
  inform_refusal (fn, ctx.refusal, ctx.detail, ctx.at);
}

static unsigned
enforce_colorability (function *fn)
{
  unsigned peak = function_peak_pressure (fn);
  if (peak <= SFPU_REG_NUM)
    {
      /* Peak pressure within the file does NOT certify 8-colorability
	 (the chromatic number can exceed the clique bound); it is the
	 no-op condition: today's pipeline (IRA + the post-RA spill
	 diagnosis) handles this case exactly as before, byte-identically.  */
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc: peak pressure %u within the %u-LREG file; "
		 "no-op (allocation left to IRA as today)\n",
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
  spill_transaction tx;
  unsigned spills = 0;

  /* Transactional bail-out: restore the exact pre-allocation stream,
     then let the post-RA spill diagnosis speak.  */
  auto bail = [&] (const char *name, const char *detail) -> unsigned
    {
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc refusal: %s (%s); rolling back %u round-trip "
		 "insn(s) and %u rewrite(s); keeping lreg-pressure-exceeded\n",
		 name, detail ? detail : "",
		 tx.emitted.length (), tx.replaced_insn.length ());
      bool had_mutations = !tx.emitted.is_empty ()
	|| !tx.replaced_insn.is_empty ();
      rollback (tx);
      if (had_mutations)
	df_analyze ();
      inform_refusal (fn, name, detail, NULL);
      return 0;
    };

  const unsigned max_rounds = 256;
  for (unsigned round = 0; round < max_rounds; round++)
    {
      lpa_graph g;
      build_graph (fn, g, spill_tmps);
      if (g.fail)
	return bail (g.fail, "graph-collection");

      auto_vec<int> color;
      int blocked = -1;
      if (dsatur_color (g, color, &blocked))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: %u web(s) DSATUR-colored with %u colors "
		     "after %u spill(s), %u round-trip insn(s); "
		     "colorability=proven (graph-level certificate: "
		     "pattern tie/matching constraints remain IRA's, with "
		     "the post-RA spill diagnosis as backstop)\n",
		     g.webs.length (), SFPU_REG_NUM, spills,
		     tx.n_round_trips ());
	  return spills ? TODO_df_finish : 0;
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
	      dump_spill_refusal (fn, ctx);
	      return 0;		/* precedes any mutation */
	    }
	}

      int vmax_delta = 0, vepoch = 0;
      const char *why = NULL;
      int victim = choose_spill_web (g, blocked, fn, ctx, &vmax_delta,
				     &vepoch, &why);
      if (victim < 0)
	return bail (why, "no admissible web in the blocked neighborhood");

      HOST_WIDE_INT x = choose_scratch_row (ctx, vmax_delta, vepoch);
      if (x < 0)
	return bail ("lreg-spill-no-free-dst", "scratch rows exhausted");

      unsigned vregno = g.webs[victim].regno;
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc: spilling web r%u (occ %u, degree %u) to Dst "
		 "scratch offset " HOST_WIDE_INT_PRINT_DEC
		 " (mod0 4 INT32 round trip, addr_mode %d, max delta %d)\n",
		 vregno, g.webs[victim].occ, g.degree[victim], x,
		 ctx.noinc_addr_mode, vmax_delta);

      if (!spill_web (fn, vregno, x, ctx.noinc_addr_mode, ctx, spill_tmps,
		      tx, &why))
	return bail (why, "web rewrite abandoned");
      spills++;
      df_analyze ();
    }

  return bail ("lreg-spill-round-limit", "allocation did not converge");
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
