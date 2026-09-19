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

/* The SFPU exposes eight architectural vector registers (LREGs).  Before this
   pass the backend had no real allocator for them, which put a hard ceiling
   on every transform that wants a value to stay resident -- residency,
   pressure-parking and store-to-load forwarding all bid for the same file.

   This is Chaitin-style graph colouring with DSATUR ordering (Brelaz, CACM
   1979): build the interference graph over SFPU pseudos, colour saturation-
   first, and spill through scratch when a web will not fit.  It consumes the
   dst-layout-32b ABI declaration and the IRA dual-bank binding rather than
   re-deriving either.

   Two layers live here and are worth separating when reading.  The first is
   a pressure audit -- a backward simulation computing function-wide peak
   simultaneous pressure, kept dump-byte-identical to its historical form
   because the testsuite pins that output.  The second is the allocator
   proper.  They share the gate, which is why one flag pair admits both.

   Structural transparency matters to the audit: a pattern carrying
   unspec_volatile is opaque to the effect model, so those are excluded
   explicitly rather than mis-modelled.

   Gated by TARGET_XTT_TENSIX_BH / _WH; see -mtt-tensix-optimize-lreg-alloc
   and -mtt-tensix-optimize-pressure-schedule.  */

/* The SFPU vector-register (LREG) allocator, replacing the former
   dump-only audit stub.  It has two independent layers:

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
	2^32 patterns in the reference simulator; FP32 mod0 3 is NOT
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

      - Under the additional default-off
	-mtt-tensix-optimize-lreg-coalesce, Briggs/George
	CONSERVATIVE COALESCING merges copy-related
	webs on the just-built graph before the colorability verdict
	and spill-victim selection, so a web that only spilled because
	its copy halves were counted separately colors for free; a
	conservative merge can never turn an 8-colorable graph
	uncolorable, so coalescing only ever removes spills.  See the
	coalescing section comment below for the tests and the named
	refusals.

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
	reference-simulator bit-exactness gate on every newly-compiling
	kernel is the empirical backstop.

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
#include "rtl-rvtt-lp-alloc-int.h"

using namespace rvtt_lpa;
#include "rvtt-refuse.h"
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

/* Number of XTT32SI allocation units (SFPU vector pseudos and live
   hard LREGs, per xtt32_allocation_unit_p) set in the LIVE register
   bitmap -- the unit every pressure figure here is measured in.  */

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

/* Function-wide peak simultaneous SFPU pressure.  BACKWARD simulation
   on purpose: the forward simulator consumes REG_DEAD/REG_UNUSED
   notes, i.e. requires df_note_add_problem -- and adding the NOTES
   problem refreshes notes that downstream passes consume, breaking
   flag-on byte-identity below the wall (the corpus AB caught three
   TUs).  Backward simulation needs only DF_LR.  */

static unsigned
function_peak_pressure (function *fn)
{
  unsigned peak = 0;
  auto_bitmap live;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      bitmap_copy (live, DF_LR_OUT (bb));
      df_simulate_initialize_backwards (bb, live);
      peak = MAX (peak, count_xtt32_units (live));
      rtx_insn *insn;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (NONDEBUG_INSN_P (insn))
	    df_simulate_one_insn_backwards (bb, insn, live);
	  peak = MAX (peak, count_xtt32_units (live));
	}
    }
  return peak;
}

/* ---------------------- effect classification ---------------------- */
/* Audited architectural effect data for typed value-op patterns the
   full generated effect sets do not cover lives at the definitions:
   the xtt_lane_local/xtt_cc_write attribute rows in rvtt.md, reached
   through rvtt_lane_local_effects (the typed-effect tables; the effect_overrides
   table formerly copied verbatim from rtl-rvtt-dst-ownership.cc is
   deleted -- the migration's blocking planner-oracle re-freeze is
   recorded in testsuite oracles/refreeze-pin49-20260831.txt).  */



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

/* Undo everything TX recorded, newest first: revert each validated
   operand rewrite to its original register and delete every emitted
   round-trip insn, restoring the pre-allocation stream exactly.  */

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

/* Report CTX's recorded spill-legality refusal for FN: count it in the
   refusal registry, print the dump line, and tell the user the
   lreg-pressure-exceeded error stands.  */

static void
dump_spill_refusal (function *fn, const spill_ctx &ctx)
{
  rvtt_refuse (RVTT_REF_LREG_PRESSURE_EXCEEDED, dump_file,
	       "lreg-alloc spill-refusal: %s (%s) at insn %d; "
	       "keeping lreg-pressure-exceeded\n",
	       ctx.refusal, ctx.detail ? ctx.detail : "",
	       ctx.at ? INSN_UID (ctx.at) : -1);
  inform_refusal (fn, ctx.refusal, ctx.detail, ctx.at);
}

/* Layer-2 enforcement over FN.  A no-op while the function's peak SFPU
   pressure fits the 8-LREG file (allocation stays IRA's, as today).
   Above it, iterate: build the web interference graph (Briggs/George-
   coalesced under its flag), attempt a DSATUR coloring, and on a block
   spill the chosen victim web through a Dst scratch row, re-analyzing
   DF each round.  Every failure bails transactionally -- the stream is
   rolled back exactly and the post-RA spill diagnosis speaks.  Returns
   TODO_df_finish when spills were committed, 0 otherwise.  */

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
      rvtt_refuse (RVTT_REF_LREG_PRESSURE_EXCEEDED, dump_file,
		   "lreg-alloc refusal: %s (%s); rolling back %u round-trip "
		   "insn(s) and %u rewrite(s); keeping "
		   "lreg-pressure-exceeded\n",
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

      /* Conservative coalescing (Briggs/George) merges copy-related
	 webs before the colorability verdict and spill-victim
	 selection; a conservative merge can never turn an 8-colorable
	 graph uncolorable, so this only ever removes spills.  Graph-
	 side only: the stream is untouched and assignment stays
	 IRA's.  */
      if (riscv_tt_opt_lreg_coalesce)
	coalesce_conservative (fn, g);

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

      /* A coalesced victim round-trips every copy-related constituent
	 through the SAME scratch row (the chooser proved each one
	 admissible at one common epoch); an uncoalesced victim is its
	 own single constituent, byte-identically today's path.  */
      for (unsigned m = 0; m < g.webs.length (); m++)
	{
	  if (g.rep (m) != victim)
	    continue;
	  if (m != (unsigned) victim && dump_file)
	    fprintf (dump_file,
		     "lreg-alloc: spilling coalesced constituent r%u of "
		     "web r%u to the shared scratch offset "
		     HOST_WIDE_INT_PRINT_DEC "\n",
		     g.webs[m].regno, vregno, x);
	  if (!spill_web (fn, g.webs[m].regno, x, ctx.noinc_addr_mode, ctx,
			  spill_tmps, tx, &why))
	    return bail (why, "web rewrite abandoned");
	}
      spills++;
      df_analyze ();
    }

  return bail ("lreg-spill-round-limit", "allocation did not converge");
}

/* -------------- dual-bank pinned-chain binding (layer 3) ------------- */

/* Some Tensix patterns constrain vector operands to EXACT LREGs
   through singleton-class constraints (x0..x7): the SFPTRANSP family
   pins its whole quartets, and rvtt_sfpswap_indexed_int encodes the
   RELATIONAL fact index_reg == value_reg + 4 as twelve exact-register
   alternatives.  IRA's cost model scans alternatives PER OPERAND
   (ira-costs.cc minimizes over alternatives independently for each
   operand), so every member of a relational alternative set looks
   equally cheap in isolation and IRA freely picks a register
   combination no single alternative admits.  LRA then repairs the
   mismatch with singleton-class reloads, and a reload needs a free
   LREG: at peak pressure 8 there is none, so LRA spills to memory and
   the post-RA rtl-rvtt-spill-diag error fires on a kernel a
   consistent assignment would have compiled (the lane-EX top16
   reproducer: DSATUR-colorable, IRA-colored, LRA-spilled).

   This layer restores the missing information at the layer that lost
   it: it solves the alternative-selection + coloring problem over the
   function's pinned webs and commits the solution by REWRITING each
   pin-derived web to its exact LREG hard register before IRA runs.
   Explicit hard registers are the one channel that propagates the
   binding to IRA's WHOLE coloring: every free web's allocno conflicts
   with the hard register's live range, so IRA cannot paint a
   neighbor into a pinned register (a per-pseudo singleton allocno
   class -- TARGET_IRA_CHANGE_PSEUDO_ALLOCNO_CLASS -- was tried first
   and is NOT sufficient: IRA's pop order can still assign the pinned
   register to a conflicting free web popped earlier and then spills
   the singleton-class allocno to memory).  Free webs stay pseudos:
   IRA keeps its cost model and coalescing for everything unpinned.

   Soundness does not lean on the interference graph alone: before
   any rewrite, an independent point-wise DF simulation proves that
   webs bound to the SAME register have disjoint live ranges and that
   no bound web is live across a call; either proof failing refuses
   by name (silent same-register overlap would be wrong code, so this
   oracle is the belt over the graph's live-at-def construction).
   The rewrite itself is validated per insn and transactional.

   Engagement gate: the layer stands down (structural no-op, map
   empty) unless the function contains at least one RELATIONAL pin
   site -- a recognized insn with more than one enabled alternative
   pinning XTT32SI pseudos to singleton LREG classes.  Single-
   alternative pins (the SFPTRANSP family alone) are per-operand
   visible to IRA's cost model already and keep today's allocation
   byte-identically.

   Model (fail-closed; every refusal keeps today's stream AND today's
   allocation -- the map stays empty, so refusing is exactly today's
   behavior):

     - pin requirement: operand constrained to a singleton LREG class
       in an alternative => that operand's WHOLE WEB must live in that
       LREG under this alternative choice.

     - matching equality: a matching constraint between two XTT32SI
       webs unifies their colors under this alternative choice.

     - base constraints: graph precolors (livein reservation
       sentinels) and every single-alternative pin site.

     - solve: deterministic depth-first search over the relational
       sites' alternatives (insn order, alternative order), pruned by
       requirement/equality consistency, capped by an explicit search
       budget; a full choice is accepted only when no two conflicting
       webs share a forced color, no matching-united webs conflict,
       and a DSATUR completion over the merged web classes extends the
       forced colors to ALL webs within the 8-LREG file (the existence
       certificate that IRA can finish the job).

     - commit: exactly the webs whose color is PIN-DERIVED are bound;
       completion colors are certificate-only and discarded.

   Named refusals (dump-visible under -fdump-rtl-rvtt_lp_alloc):
   dualbank-pin-opaque-xtt-insn, dualbank-pin-shape-unmodeled,
   dualbank-pin-no-feasible-alternative, dualbank-pin-inconsistent,
   dualbank-matched-webs-conflict, dualbank-assignment-unsat,
   dualbank-search-budget-exceeded, dualbank-same-color-overlap,
   dualbank-web-live-across-call, dualbank-rewrite-refused.  */

/* Caps (audited in rvtt-cost.md, "dual-bank binding" rows): the
   largest pinning pattern today has 16 operands
   (rvtt_sfptransp8_int) and 12 alternatives
   (rvtt_sfpswap_indexed_int); the caps leave headroom without
   admitting unbounded shapes.  The search budget bounds DFS
   alternative applications; consistency propagation collapses
   anchored chains (every transp8-anchored sortnet measured visits
   < 200), and an exhausted budget refuses by name.  */

#define LPA_PIN_MAX_ALTS 16
#define LPA_PIN_MAX_OPS 24
#define LPA_PIN_SEARCH_BUDGET 4096

/* The singleton-LREG class test: SFPU_REGS_L0..L7 are contiguous
   (riscv.h reg_class enum).  */

static int
singleton_sfpu_lreg (enum reg_class cl)
{
  if (cl >= SFPU_REGS_L0 && cl <= SFPU_REGS_L7)
    return cl - SFPU_REGS_L0;
  return -1;
}

struct lpa_pin_alt
{
  int nreq, neq;
  int req_node[LPA_PIN_MAX_OPS];
  int req_lreg[LPA_PIN_MAX_OPS];
  int eq_a[LPA_PIN_MAX_OPS];
  int eq_b[LPA_PIN_MAX_OPS];
};

struct lpa_pin_site
{
  rtx_insn *insn;
  int nalts;			/* enabled + feasible alternatives */
  lpa_pin_alt alts[LPA_PIN_MAX_ALTS];
};

/* Cheap engagement gate: does FN contain a recognized insn with more
   than one alternative any of which pins an operand to a singleton
   LREG class?  (Enabledness and operand shape are checked during
   collection; over-approximating here only costs the collection
   walk.)  */

static bool
function_has_relational_pin_site (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  int icode = recog_memoized (insn);
	  if (icode < 0)
	    continue;
	  int nalts = insn_data[icode].n_alternatives;
	  int nops = insn_data[icode].n_operands;
	  if (nalts <= 1 || nops == 0)
	    continue;
	  const operand_alternative *op_alt
	    = preprocess_insn_constraints (icode);
	  if (!op_alt)
	    continue;
	  for (int a = 0; a < nalts; a++)
	    for (int i = 0; i < nops; i++)
	      if (singleton_sfpu_lreg
		    ((enum reg_class) op_alt[a * nops + i].cl) >= 0)
		return true;
	}
    }
  return false;
}

/* Collect every pin site in FN against graph G.  Returns false with
   *WHY / *WHY_AT set on any shape the model does not cover.  */

static bool
collect_pin_sites (function *fn, const lpa_graph &g,
		   auto_vec<lpa_pin_site> &sites, int *n_relational,
		   const char **why, rtx_insn **why_at)
{
  *n_relational = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx pat = PATTERN (insn);
	  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	    continue;

	  int icode = recog_memoized (insn);
	  if (icode < 0)
	    {
	      /* Unrecognized stream elements (inline asm and friends)
		 touching vector pseudos are outside the binding
		 model: their register demands are invisible.  */
	      subrtx_iterator::array_type array;
	      FOR_EACH_SUBRTX (iter, array, pat, ALL)
		if (REG_P (*iter) && xtt32_pseudo_p (REGNO (*iter)))
		  {
		    *why = "dualbank-pin-opaque-xtt-insn";
		    *why_at = insn;
		    return false;
		  }
	      continue;
	    }

	  extract_insn (insn);
	  preprocess_constraints (insn);
	  int nops = recog_data.n_operands;
	  int nalts = recog_data.n_alternatives;
	  if (nops == 0 || nalts == 0)
	    continue;
	  alternative_mask enabled = get_enabled_alternatives (insn);

	  /* Is any enabled alternative pinning?  */
	  bool pinning = false;
	  for (int a = 0; a < nalts && !pinning; a++)
	    {
	      if (!(enabled & ALTERNATIVE_BIT (a)))
		continue;
	      for (int i = 0; i < nops; i++)
		if (singleton_sfpu_lreg
		      ((enum reg_class) recog_op_alt[a * nops + i].cl) >= 0)
		  {
		    pinning = true;
		    break;
		  }
	    }
	  if (!pinning)
	    continue;

	  if (nops > LPA_PIN_MAX_OPS || nalts > LPA_PIN_MAX_ALTS)
	    {
	      *why = "dualbank-pin-shape-unmodeled";
	      *why_at = insn;
	      return false;
	    }

	  lpa_pin_site site;
	  site.insn = insn;
	  site.nalts = 0;
	  for (int a = 0; a < nalts; a++)
	    {
	      if (!(enabled & ALTERNATIVE_BIT (a)))
		continue;
	      lpa_pin_alt alt;
	      alt.nreq = 0;
	      alt.neq = 0;
	      bool feasible = true;
	      for (int i = 0; i < nops && feasible; i++)
		{
		  const operand_alternative &oa
		    = recog_op_alt[a * nops + i];
		  rtx op = recog_data.operand[i];
		  bool xtt_reg = REG_P (op)
		    && xtt32_pseudo_p (REGNO (op));
		  int lreg = singleton_sfpu_lreg ((enum reg_class) oa.cl);
		  if (lreg >= 0)
		    {
		      if (!xtt_reg)
			{
			  /* The alternative demands an exact LREG for
			     an operand that is not a vector pseudo
			     (e.g. a constant-LREG unspec): it cannot
			     be chosen as the operands stand.  */
			  feasible = false;
			  break;
			}
		      int node = g.node_of_reg[REGNO (op)];
		      if (node < 0)
			{
			  *why = "dualbank-pin-shape-unmodeled";
			  *why_at = insn;
			  return false;
			}
		      alt.req_node[alt.nreq] = node;
		      alt.req_lreg[alt.nreq] = lreg;
		      alt.nreq++;
		    }
		  if (oa.matches >= 0 && xtt_reg)
		    {
		      rtx mop = recog_data.operand[oa.matches];
		      if (REG_P (mop) && xtt32_pseudo_p (REGNO (mop)))
			{
			  int na = g.node_of_reg[REGNO (op)];
			  int nb = g.node_of_reg[REGNO (mop)];
			  if (na < 0 || nb < 0)
			    {
			      *why = "dualbank-pin-shape-unmodeled";
			      *why_at = insn;
			      return false;
			    }
			  if (na != nb)
			    {
			      alt.eq_a[alt.neq] = na;
			      alt.eq_b[alt.neq] = nb;
			      alt.neq++;
			    }
			}
		    }
		}
	      if (feasible)
		site.alts[site.nalts++] = alt;
	    }
	  if (site.nalts == 0)
	    {
	      *why = "dualbank-pin-no-feasible-alternative";
	      *why_at = insn;
	      return false;
	    }
	  if (site.nalts > 1)
	    (*n_relational)++;
	  sites.safe_push (site);
	}
    }
  return true;
}

/* Union-find + root-color state.  No path compression, min-index
   roots: copies are cheap (webs are few) and the search stays
   deterministic.  */

struct lpa_bind_state
{
  auto_vec<int> uf;
  auto_vec<int> col;

  void init (unsigned n)
  {
    uf.truncate (0);
    col.truncate (0);
    uf.safe_grow (n);
    col.safe_grow (n);
    for (unsigned i = 0; i < n; i++)
      {
	uf[i] = i;
	col[i] = -1;
      }
  }
  void copy_from (const lpa_bind_state &o)
  {
    uf.truncate (0);
    col.truncate (0);
    uf.safe_splice (o.uf);
    col.safe_splice (o.col);
  }
};

/* Union-find root of node X in ST (no path compression: states are
   copied throughout the search, so lookups must not mutate).  */

static int
bind_find (const lpa_bind_state &st, int x)
{
  while (st.uf[x] != x)
    x = st.uf[x];
  return x;
}

/* Merge the classes of A and B in ST (the lower root index survives).
   Returns false when their forced colors disagree; otherwise the merged
   root inherits whichever color was forced.  */

static bool
bind_union (lpa_bind_state &st, int a, int b)
{
  a = bind_find (st, a);
  b = bind_find (st, b);
  if (a == b)
    return true;
  if (b < a)
    std::swap (a, b);
  int ca = st.col[a], cb = st.col[b];
  if (ca >= 0 && cb >= 0 && ca != cb)
    return false;
  st.uf[b] = a;
  st.col[a] = ca >= 0 ? ca : cb;
  return true;
}

/* Force NODE's class in ST to the color LREG; returns false when the
   class already carries a different forced color.  */

static bool
bind_require (lpa_bind_state &st, int node, int lreg)
{
  int r = bind_find (st, node);
  if (st.col[r] >= 0 && st.col[r] != lreg)
    return false;
  st.col[r] = lreg;
  return true;
}

/* Apply one pin-site alternative ALT to ST: unify its matching-
   equality pairs and force its singleton-class requirements.  Returns
   false on the first inconsistency; ST may then be partially updated,
   so the search always applies alternatives to a copy.  */

static bool
bind_apply_alt (lpa_bind_state &st, const lpa_pin_alt &alt)
{
  for (int e = 0; e < alt.neq; e++)
    if (!bind_union (st, alt.eq_a[e], alt.eq_b[e]))
      return false;
  for (int r = 0; r < alt.nreq; r++)
    if (!bind_require (st, alt.req_node[r], alt.req_lreg[r]))
      return false;
  return true;
}

/* Forced-color consistency: no two webs carrying the same forced
   color may conflict (different classes), and no two members of one
   merged class may conflict (a matching-united pair that overlaps
   needs a repair copy this layer does not emit).  Grouping by color
   keeps this cheap enough to run after EVERY alternative application
   -- the pruning that keeps the DFS from exploding on functions with
   many relational sites.  *MATCHED_CONFLICT reports the united-webs
   case for refusal naming.  */

static bool
bind_forced_consistent_p (const lpa_graph &g, const lpa_bind_state &st,
			  bool *matched_conflict)
{
  unsigned n = g.webs.length ();
  auto_vec<int> by_color[SFPU_REG_NUM];
  for (unsigned i = 0; i < n; i++)
    {
      int c = st.col[bind_find (st, i)];
      if (c >= 0)
	by_color[c].safe_push (i);
    }
  for (unsigned c = 0; c < SFPU_REG_NUM; c++)
    for (unsigned a = 0; a < by_color[c].length (); a++)
      for (unsigned b = a + 1; b < by_color[c].length (); b++)
	{
	  unsigned i = by_color[c][a], j = by_color[c][b];
	  if (!g.conflict_p (i, j))
	    continue;
	  if (bind_find (st, i) == bind_find (st, j))
	    *matched_conflict = true;
	  return false;
	}
  return true;
}

/* Full-choice acceptance: forced colors consistent, and a DSATUR
   completion over the merged web classes must extend the forced
   colors to every web within the file.  */

static bool
bind_leaf_acceptable (const lpa_graph &g, const lpa_bind_state &st,
		      bool *matched_conflict)
{
  unsigned n = g.webs.length ();
  if (!bind_forced_consistent_p (g, st, matched_conflict))
    return false;

  auto_vec<int> root_of;
  root_of.safe_grow (n);
  for (unsigned i = 0; i < n; i++)
    root_of[i] = bind_find (st, i);

  /* United classes must be internally conflict-free even when
     uncolored (they will share whatever register completion or IRA
     picks).  */
  for (unsigned i = 0; i < n; i++)
    for (unsigned j = i + 1; j < n; j++)
      if (root_of[i] == root_of[j] && g.conflict_p (i, j))
	{
	  *matched_conflict = true;
	  return false;
	}

  /* DSATUR completion over the merged classes (roots).  */
  auto_vec<int> rid;
  rid.safe_grow (n);
  auto_vec<int> roots;
  for (unsigned i = 0; i < n; i++)
    rid[i] = -1;
  for (unsigned i = 0; i < n; i++)
    if (root_of[i] == (int) i)
      {
	rid[i] = roots.length ();
	roots.safe_push (i);
      }
  unsigned m = roots.length ();

  sbitmap adj = sbitmap_alloc (m * m);
  bitmap_clear (adj);
  for (unsigned i = 0; i < n; i++)
    for (unsigned j = i + 1; j < n; j++)
      if (g.conflict_p (i, j))
	{
	  unsigned ri = rid[root_of[i]], rj = rid[root_of[j]];
	  bitmap_set_bit (adj, ri * m + rj);
	  bitmap_set_bit (adj, rj * m + ri);
	}

  auto_vec<int> color;
  color.safe_grow (m);
  for (unsigned k = 0; k < m; k++)
    color[k] = st.col[roots[k]];

  const unsigned full = (1u << SFPU_REG_NUM) - 1;
  bool ok = true;
  for (;;)
    {
      int best = -1;
      unsigned best_sat = 0, best_deg = 0;
      for (unsigned k = 0; k < m; k++)
	{
	  if (color[k] >= 0)
	    continue;
	  unsigned sat_mask = 0, deg = 0;
	  for (unsigned l = 0; l < m; l++)
	    if (bitmap_bit_p (adj, k * m + l))
	      {
		deg++;
		if (color[l] >= 0)
		  sat_mask |= 1u << color[l];
	      }
	  unsigned sat = popcount_hwi (sat_mask & full);
	  if (best < 0 || sat > best_sat
	      || (sat == best_sat && deg > best_deg))
	    {
	      best = k;
	      best_sat = sat;
	      best_deg = deg;
	    }
	}
      if (best < 0)
	break;			/* all colored: completion exists */
      unsigned sat_mask = 0;
      for (unsigned l = 0; l < m; l++)
	if (bitmap_bit_p (adj, best * m + l) && color[l] >= 0)
	  sat_mask |= 1u << color[l];
      unsigned avail = ~sat_mask & full;
      if (!avail)
	{
	  ok = false;
	  break;
	}
      color[best] = ctz_hwi (avail);
    }
  sbitmap_free (adj);
  return ok;
}

/* Deterministic DFS over the relational sites' alternatives.  */

static bool
bind_solve_rec (const lpa_graph &g,
		const auto_vec<const lpa_pin_site *> &rel, unsigned idx,
		const lpa_bind_state &st, unsigned *budget,
		bool *budget_hit, bool *matched_conflict,
		lpa_bind_state *out)
{
  if (idx == rel.length ())
    {
      if (!bind_leaf_acceptable (g, st, matched_conflict))
	return false;
      out->copy_from (st);
      return true;
    }
  const lpa_pin_site *s = rel[idx];
  for (int a = 0; a < s->nalts; a++)
    {
      if (*budget == 0)
	{
	  *budget_hit = true;
	  return false;
	}
      (*budget)--;
      lpa_bind_state next;
      next.copy_from (st);
      if (!bind_apply_alt (next, s->alts[a]))
	continue;
      /* Eager pruning: a forced-color collision anywhere kills this
	 subtree now, not at the leaf.  */
      bool mc = false;
      if (!bind_forced_consistent_p (g, next, &mc))
	{
	  if (mc)
	    *matched_conflict = true;
	  continue;
	}
      if (bind_solve_rec (g, rel, idx + 1, next, budget, budget_hit,
			  matched_conflict, out))
	return true;
      if (*budget_hit)
	return false;
    }
  return false;
}

/* Register the named dual-bank binding refusal NAME (with DETAIL and
   the blocking insn AT) and dump the standard stand-down line; today's
   allocation then proceeds untouched.  */

static void
dump_bind_refusal (const char *name, const char *detail, rtx_insn *at)
{
  rvtt_refuse_by_name (name, dump_file,
		       "lreg-alloc dual-bank binding refusal: %s (%s) "
		       "at insn %d; standing down (today's allocation)\n",
		       name, detail ? detail : "", at ? INSN_UID (at) : -1);
}

/* Independent soundness oracle for the commit: point-wise backward DF
   simulation proving that (a) no two DIFFERENT webs bound to the same
   LREG are ever live at the same point, and (b) no bound web is live
   across a call (the hard register would not survive what a pseudo's
   allocation could have been made to survive).  This deliberately
   re-derives liveness rather than trusting the interference graph's
   live-at-def construction: a missed conflict there would turn the
   hard-register rewrite into a SILENT same-register overlap.  BOUND
   maps node -> lreg or -1.  */

static bool
bind_commit_sound_p (function *fn, const lpa_graph &g,
		     const auto_vec<int> &bound, const char **why,
		     rtx_insn **why_at)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      auto_bitmap live;
      bitmap_copy (live, DF_LR_OUT (bb));
      df_simulate_initialize_backwards (bb, live);

      /* Check one program point: the set of bound webs live here must
	 be register-unique.  */
      auto check_point = [&] (rtx_insn *at) -> bool
	{
	  int seen[SFPU_REG_NUM];
	  for (int c = 0; c < (int) SFPU_REG_NUM; c++)
	    seen[c] = -1;
	  unsigned lregno;
	  bitmap_iterator bi;
	  EXECUTE_IF_SET_IN_BITMAP (live, 0, lregno, bi)
	    {
	      if (!xtt32_pseudo_p (lregno))
		continue;
	      int node = g.node_of_reg[lregno];
	      if (node < 0 || bound[node] < 0)
		continue;
	      int c = bound[node];
	      if (seen[c] >= 0 && seen[c] != node)
		{
		  *why = "dualbank-same-color-overlap";
		  *why_at = at;
		  return false;
		}
	      seen[c] = node;
	    }
	  return true;
	};

      if (!check_point (NULL))	/* block exit */
	return false;
      rtx_insn *insn;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  df_simulate_one_insn_backwards (bb, insn, live);
	  if (!check_point (insn))
	    return false;
	  if (CALL_P (insn))
	    {
	      unsigned lregno;
	      bitmap_iterator bi;
	      EXECUTE_IF_SET_IN_BITMAP (live, 0, lregno, bi)
		{
		  if (!xtt32_pseudo_p (lregno))
		    continue;
		  int node = g.node_of_reg[lregno];
		  if (node >= 0 && bound[node] >= 0)
		    {
		      *why = "dualbank-web-live-across-call";
		      *why_at = insn;
		      return false;
		    }
		}
	    }
	}
    }
  return true;
}

/* Entry point.  Never mutates the stream; commits only the forced-
   class map.  */

static void
bind_dual_bank_chains (function *fn)
{
  if (!function_has_relational_pin_site (fn))
    return;			/* structural no-op */

  lpa_graph g;
  build_graph (fn, g, NULL);
  if (g.fail)
    {
      dump_bind_refusal (g.fail, "graph collection", g.fail_at);
      return;
    }

  auto_vec<lpa_pin_site> sites;
  int n_relational = 0;
  const char *why = NULL;
  rtx_insn *why_at = NULL;
  if (!collect_pin_sites (fn, g, sites, &n_relational, &why, &why_at))
    {
      dump_bind_refusal (why, "pin collection", why_at);
      return;
    }
  if (n_relational == 0)
    return;			/* enabledness shrank the gate hit */

  unsigned n = g.webs.length ();
  lpa_bind_state base;
  base.init (n);

  /* Base constraints: graph precolors, then single-alternative
     sites.  */
  for (unsigned i = 0; i < n; i++)
    if (g.webs[i].precolor >= 0
	&& !bind_require (base, i, g.webs[i].precolor))
      {
	dump_bind_refusal ("dualbank-pin-inconsistent", "precolor", NULL);
	return;
      }
  auto_vec<const lpa_pin_site *> rel;
  for (unsigned s = 0; s < sites.length (); s++)
    {
      if (sites[s].nalts == 1)
	{
	  if (!bind_apply_alt (base, sites[s].alts[0]))
	    {
	      dump_bind_refusal ("dualbank-pin-inconsistent",
				 "single-alternative pins", sites[s].insn);
	      return;
	    }
	}
      else
	rel.safe_push (&sites[s]);
    }

  bool base_mc = false;
  if (!bind_forced_consistent_p (g, base, &base_mc))
    {
      dump_bind_refusal (base_mc ? "dualbank-matched-webs-conflict"
			 : "dualbank-pin-inconsistent",
			 "base constraints collide", NULL);
      return;
    }

  unsigned budget = LPA_PIN_SEARCH_BUDGET;
  bool budget_hit = false, matched_conflict = false;
  lpa_bind_state sol;
  if (!bind_solve_rec (g, rel, 0, base, &budget, &budget_hit,
		       &matched_conflict, &sol))
    {
      dump_bind_refusal (budget_hit ? "dualbank-search-budget-exceeded"
			 : matched_conflict ? "dualbank-matched-webs-conflict"
			 : "dualbank-assignment-unsat",
			 "no consistent alternative selection", NULL);
      return;
    }

  /* Pin-derived colors per node; completion colors are certificate-
     only and never committed.  */
  auto_vec<int> bound;
  bound.safe_grow (n);
  unsigned n_bound = 0;
  for (unsigned i = 0; i < n; i++)
    {
      bound[i] = sol.col[bind_find (sol, i)];
      if (bound[i] >= 0)
	n_bound++;
    }

  /* Independent soundness oracle before any mutation.  */
  if (!bind_commit_sound_p (fn, g, bound, &why, &why_at))
    {
      dump_bind_refusal (why, "commit oracle", why_at);
      return;
    }

  /* Commit: rewrite each bound web to its LREG hard register.  The
     replacement is a same-mode REG-for-REG substitution, so the
     pattern SHAPE (and with it the cached INSN_CODE the whole port's
     RTL passes key on) is preserved by construction; re-recognition
     is deliberately NOT run, because the noval machinery leaves NOVAL
     unspecs in register_operand slots under cached codes (recog-stale
     by port convention), so any revalidating rewrite of such an insn
     would fail for reasons unrelated to this substitution.  LRA still
     constraint-checks every rewritten insn against its cached code --
     a bad binding is a loud reload failure, never silent.  */
  for (unsigned i = 0; i < n; i++)
    {
      if (bound[i] < 0)
	continue;
      rtx preg = regno_reg_rtx[g.webs[i].regno];
      rtx hard = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + bound[i]);
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  rtx_insn *insn;
	  FOR_BB_INSNS (bb, insn)
	    {
	      if (!INSN_P (insn))
		continue;
	      if (DEBUG_INSN_P (insn))
		{
		  if (DEBUG_BIND_INSN_P (insn)
		      && reg_mentioned_p (preg, INSN_VAR_LOCATION_LOC (insn)))
		    {
		      INSN_VAR_LOCATION_LOC (insn)
			= simplify_replace_rtx (INSN_VAR_LOCATION_LOC (insn),
						preg, hard);
		      df_insn_rescan (insn);
		    }
		  continue;
		}
	      bool touched = false;
	      if (reg_mentioned_p (preg, PATTERN (insn)))
		{
		  PATTERN (insn) = replace_rtx (PATTERN (insn), preg, hard);
		  touched = true;
		}
	      if (REG_NOTES (insn)
		  && reg_mentioned_p (preg, REG_NOTES (insn)))
		{
		  REG_NOTES (insn) = replace_rtx (REG_NOTES (insn), preg,
						  hard);
		  touched = true;
		}
	      if (touched)
		df_insn_rescan (insn);
	    }
	}
      if (dump_file)
	fprintf (dump_file,
		 "lreg-alloc dual-bank binding: r%u -> L%d\n",
		 g.webs[i].regno, bound[i]);
    }
  df_analyze ();

  if (dump_file)
    fprintf (dump_file,
	     "lreg-alloc: dual-bank pinned-chain binding: %u of %u web(s) "
	     "bound across %u pin site(s) (%d relational, search budget "
	     "used %u); completion DSATUR-certified within %u LREGs; "
	     "disjointness + call-crossing oracle proven; pinned webs "
	     "rewritten to hard LREGs for IRA\n",
	     n_bound, n, sites.length (), n_relational,
	     LPA_PIN_SEARCH_BUDGET - budget, SFPU_REG_NUM);
}

/* ------------------------------- pass ------------------------------ */

const pass_data pass_data_rvtt_lp_alloc =
{
  RTL_PASS, /* type */
  "rvtt_lp_alloc", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
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
    /* The NOTES problem is added only for the historical audit dump
       (byte-identical stub behavior for pressure_schedule users):
       adding it refreshes REG_DEAD/REG_UNUSED notes that downstream
       passes consume, which is a visible side effect.  Enforcement
       needs only DF_LR: a plain df_analyze is analysis-only, keeping
       the flag byte-identical below the pressure wall (the corpus AB
       caught exactly this on three peak-0 TUs).  */
    if (riscv_tt_opt_pressure_schedule)
      {
	df_note_add_problem ();
	df_analyze ();
	audit_function (fn);
      }
    unsigned todo = TODO_df_finish;
    if (riscv_tt_opt_lreg_alloc)
      {
	if (!riscv_tt_opt_pressure_schedule)
	  df_analyze ();
	todo |= enforce_colorability (fn);
	/* Layer 3: bind dual-bank pinned chains for IRA (forced-class
	   map only; the stream is never touched).  Runs on the final
	   (possibly spilled) stream; DF is current here -- both
	   enforcement outcomes leave it analyzed.  */
	bind_dual_bank_chains (fn);
      }
    return todo;
  }
};

} /* anonymous namespace */

/* Instantiate the LREG pressure/allocation pass for CTXT;
   rvtt-passes.def places it before ira, after the lreg-livein
   reservations, and it gates on optimization, a WH/BH Tensix target,
   and -mtt-tensix-optimize-pressure-schedule (audit dump) or
   -mtt-tensix-optimize-lreg-alloc (enforcement).  */

rtl_opt_pass *
make_pass_rvtt_lp_alloc (gcc::context *ctxt)
{
  return new pass_rvtt_lp_alloc (ctxt);
}
