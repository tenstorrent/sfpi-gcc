/* Tensix replay formation: record hoisting, unrolling, conversion
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* The loop-restructuring half of the replay former: counted-loop
   record hoisting with the exec-while-record first-trip peel and
   the placement lift, launch-loop unrolling, and the
   isomorphic-run launch conversion.  Split from rtl-rvtt-replay.cc;
   the algorithm essay lives there.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_MAP
#define INCLUDE_SET
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "print-rtl.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt-protos.h"
#include "rvtt-trips.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-mop-tables.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rtl-rvtt-replay-int.h"

/* The hoist pricing gate (models and constants in the two design
   comments above and in rvtt-cost.md).  Decide whether recording
   PAYLOAD (a span of BLOCK) once in PREHEADER instead of in LOOP's body
   is modeled profitable.  BODY_RERECORDS distinguishes a body that
   re-records the payload every trip (the in-block formation's shape)
   from a counted-loop capture; LAUNCH_RUN is the longest contiguous
   sibling-launch run, the execution-saturation context term.  Requires
   a provable trip count (rvtt_loop_trips) except on the structural
   runtime-trip record-hoist path, and audited reissue latencies unless
   the record-hoist discharge applies; prices through
   rvtt_dcost_replay_pricing and refuses by name below the audited
   minimum benefit.  */

static bool
hoist_profitable_p (class loop *loop, basic_block preheader,
		    replay_block const &block, replay_span payload,
		    bool body_rerecords, unsigned launch_run)
{
  bool record_hoist_mode
    = body_rerecords && riscv_tt_opt_replay_record_hoist > 0;
  /* The record-hoist measurement model normally cancels payload execution
     between the two worlds and prices only delivered words.  A
     drain-inclusive completion contract cannot make that cancellation when
     the binding resource is unknown: recording-with-execution performs the
     first payload while a hoisted record-only pass does not, and the first
     playback is serialized after that record.  Under the completion guard,
     retain the ordinary reissue audit and use the calibrated shared
     execution/delivery model below.  */
  bool record_completion_model
    = record_hoist_mode
      && riscv_tt_replay_hoist_completion_guard > 0;
  uint64_t niter;
  bool trips_proven = rvtt_loop_trips (loop, preheader, &niter);
  /* Lane FW: a runtime trip count is admitted to the record-hoist
     pricing under a structural trips >= 1 fact -- the hoisted record
     lands in the DEDICATED preheader of a single-block loop, so
     executing the record implies at least one body execution (the
     preheader's single successor is the body); a zero-trip entry never
     reaches the record.  The pricing branch below decides admission at
     the 2-trip break-even.  */
  bool runtime_trips = record_hoist_mode && !trips_proven;
  if (!trips_proven && !runtime_trips)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: loop %d trip count is not provably"
		 " constant\n", loop->num);
      return false;
    }

  HOST_WIDE_INT trips = trips_proven ? (HOST_WIDE_INT) niter : 0;
  if (trips_proven && trips < 2)
    {
      if (dump_file)
	{
	  fprintf (dump_file, "Not hoisting: loop %d runs %ld time(s)\n",
		   loop->num, (long) trips);
	  if (record_hoist_mode)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_TRIP_COUNT_UNPROVEN, dump_file,
			 "record-hoist refused:"
			 " record-hoist-trip-count-unproven\n");
	}
      return false;
    }

  HOST_WIDE_INT words = delivered_words (block, payload);
  /* Lane FW: under the record-hoist measurement flag the reissue-latency
     audit gate is discharged structurally rather than per-producer.  Its
     exec-side estimate feeds only the default model's pricing (the
     record-hoist branch below prices pure delivery: the executed word
     stream is IDENTICAL in both worlds by the fixed-encoding admission,
     so per-word execution -- audited or not -- cancels).  Its reissue
     soundness half is carried by the unhoisted world itself: every
     window here has at least two clones, so the identical word stream is
     ALREADY delivered by playback launches at expander pace in the
     unhoisted world (the always-on former's formation, a class
     witnessed good on hardware); converting the first clone from
     exec-while-record delivery to one more playback of that same stream
     adds no reissue exposure a proven latency could bound.  The gate
     stays for the default hoist model, whose pricing consumes the
     estimate, and for unproven targets (no hardware-witnessed playback
     class to carry the discharge -- QSR keeps the refusal).  */
  bool reissue_gate_discharged
    = record_hoist_mode && !record_completion_model
      && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH);
  HOST_WIDE_INT eslots = 0;
  if (!reissue_gate_discharged)
    {
      eslots = exec_interlocked_slots (block, payload);
      if (eslots < 0)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED, dump_file,
			   "Not hoisting: replay-reissue-latency-unproved: a"
			   " consumed payload producer carries no"
			   " audited result"
			   " latency (loop %d, %ld words)\n",
			   loop->num, (long) words);
	      if (record_hoist_mode)
		rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED,
			     dump_file,
			     "record-hoist refused:"
			     " replay-reissue-latency-unproved\n");
	    }
	  return false;
	}
    }

  HOST_WIDE_INT min_benefit = rvtt_dcost_replay_hoist_min_benefit ();

  /* The one replay pricing spelling (rvtt-delivery-cost-core.h
     replay_pricing).  The shape selector is the
     same flag-pair spelling the delivery-shape downstream mirror
     consumes, so the mirror can no longer drift from this gate.  */
  rvtt_delivery_cost::replay_shape shape
    = !body_rerecords
      ? rvtt_delivery_cost::SHAPE_COUNTED
      : rvtt_delivery_cost::rerecord_shape
	  (riscv_tt_opt_replay_record_hoist > 0,
	   riscv_tt_replay_hoist_completion_guard > 0,
	   !runtime_trips);
  rvtt_delivery_cost::replay_price price = rvtt_dcost_replay_pricing
    (shape, trips, words, eslots, launch_run,
     riscv_tt_replay_hoist_completion_guard > 0, min_benefit);

  /* Record-hoist measurement pricing (-mtt-tensix-optimize-replay-record-
     hoist, re-record bodies only).  The candidate window is proven
     iteration-invariant by the admission walk in hoist_preheader (every
     payload word fixed-encoding), so the EXECUTED word stream of the two
     worlds is identical: each playback launch expands to exactly the
     recorded words at the same stream positions the in-body clones held,
     and the hoisted no-exec record executes nothing (the Replay Expander
     consumes its payload in the frontend).  Execution-side terms therefore
     cancel between the worlds and the modeled delta is pure delivery: the
     in-body world re-delivers the capture word plus the payload every trip
     where the hoisted world delivers one launch word, a per-trip saving of
     `words' pushed words, bought once at the preheader record's full
     delivery plus the record-engine overhead.  This is the issue-side
     accounting (measured decomposition: the in-loop `ttreplay 0,len,1,1'
     re-delivers len words per row while a hand-scheduled reference
     kernel records once at init).  The default model's
     saturation/MAX pricing keeps the opposite verdict for this class from
     the Log-class hardware anchors (rvtt-cost.md, re-record derivation);
     this flag exists to build the hardware A/B measurement legs for this
     class, the same measurement-flag pattern as -mtt-tensix-mop-form-force --
     with the difference that every structural proof still gates admission
     and the delivery model itself is monotone: for proven trips >= 2 the
     hoisted world delivers strictly fewer words on every execution.  */
  if (body_rerecords && riscv_tt_opt_replay_record_hoist > 0
      && !record_completion_model)
    {
      /* The hoisted world converts the first clone from inline delivery
	 to one more playback launch per trip: charge that added launch
	 boundary at the audited turnaround constant.  Hardware
	 calibration measured 1.3-1.8 cycles per launch boundary on
	 serial-chain windows (boundary fits on ceil/log/rsqrt kernels)
	 -- above the 0.7-slot table constant; the under-charge
	 (~60-110 cs/trip) is absorbed by the MIN_BENEFIT
	 margin and noted in rvtt-cost.md.  */
      HOST_WIDE_INT record_once = price.record_once;
      HOST_WIDE_INT per_trip = price.per_trip;
      if (runtime_trips)
	{
	  /* Runtime trip count (rvtt-cost.md RECORD-HOIST
	     RUNTIME-TRIP derivation).  The delivery delta is monotone in
	     the realized trip count: each trip saves per_trip delivered
	     centislots, bought once at record_once.  With trips >= 1
	     structural (dedicated preheader of a single-block loop) the
	     worst realized outcome is the single-trip exposure
	     record_once - per_trip -- about one record delivery -- and
	     every trip from 2 on wins.  Admit when the 2-trip benefit
	     clears the same audited margin proven trip counts must
	     clear; refuse by name otherwise.  */
	  HOST_WIDE_INT benefit2 = price.benefit;
	  HOST_WIDE_INT exposure = price.exposure;
	  if (dump_file)
	    fprintf (dump_file,
		     "Record-hoist runtime-trip pricing (loop %d): words"
		     " %ld, per_trip %ld, record_once %ld, 2-trip benefit"
		     " %ld (min %ld), single-trip exposure %ld\n",
		     loop->num, (long) words, (long) per_trip,
		     (long) record_once, (long) benefit2,
		     (long) min_benefit, (long) exposure);
	  if (!price.profitable)
	    {
	      rvtt_refuse (RVTT_REF_RECORD_HOIST_RUNTIME_TRIPS_BREAK_EVEN,
			   dump_file,
			   "record-hoist refused:"
			   " record-hoist-runtime-trips-break-even: 2-trip"
			   " benefit %ld < %ld\n",
			   (long) benefit2, (long) min_benefit);
	      return false;
	    }
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist: runtime-trip re-record window admitted"
		     " (structural trips>=1, words %ld, 2-trip benefit %ld,"
		     " single-trip exposure %ld)\n",
		     (long) words, (long) benefit2, (long) exposure);
	  return true;
	}
      HOST_WIDE_INT benefit = price.benefit;
      if (dump_file)
	fprintf (dump_file,
		 "Record-hoist pricing (loop %d): trips %ld, words %ld,"
		 " deliver_body %ld/trip, boundary %d/trip, record_once %ld,"
		 " benefit %ld (min %ld)\n",
		 loop->num, (long) trips, (long) words,
		 (long) price.deliver_body, XTT_REPLAY_COST_TURNAROUND_X100,
		 (long) record_once, (long) benefit,
		 (long) min_benefit);
      if (!price.profitable)
	{
	  rvtt_refuse (RVTT_REF_RECORD_HOIST_BENEFIT, dump_file,
		       "Not hoisting: record-hoist-benefit: modeled issue-side"
		       " benefit %ld < %ld\n",
		       (long) benefit, (long) min_benefit);
	  return false;
	}
      if (dump_file)
	fprintf (dump_file,
		 "record-hoist: invariant re-record window admitted"
		 " (trips %ld, words %ld, benefit %ld)\n",
		 (long) trips, (long) words, (long) benefit);
      return true;
    }
  /* The re-record shapes split on which resource paces the in-loop
     record-with-execution pass (rvtt-cost.md, re-record derivation):
     execution-bound (exec >= deliver_record) exposes the record
     engine's per-pass overhead on the critical path and hides the
     hoisted preheader pass's delivery behind the loop's own execution
     backlog (Reduce-class hardware A/B); delivery-bound keeps the
     originally calibrated delivery pricing (Log/Log1p refusals) with the
     engine overhead absorbed in the per-word delivery slack.

     Both shapes -- and the counted branch, the execution-saturation
     context term, and the completion guard's full-record charge --
     are priced by the shared spelling in rvtt-delivery-cost-core.h
     (replay_pricing); PRICE above carries every term.  */
  bool exec_bound_rerecord = price.exec_bound;
  HOST_WIDE_INT record = price.record;
  HOST_WIDE_INT before = price.before;
  HOST_WIDE_INT after = price.after;
  if (exec_bound_rerecord && riscv_tt_replay_hoist_completion_guard > 0)
    {
      /* The body-throughput calibration above permits the hoisted record's
	 delivery to hide behind the loop's execution backlog.  A caller that
	 scores completion through a final Tensix drain cannot in general take
	 credit for that overlap: the record must be complete before its first
	 playback, and any remaining execution is charged at the drain.  Keep
	 the established body model as the default, but offer a completion-
	 accurate, shape-generic guard which charges the full record delivery.
	 The guarded record-hoist path uses the shared binding-resource model.
	 For every legal replay payload (at least four delivered words), its
	 execution-bound benefit is strictly no greater than the delivery-only
	 measurement benefit: their difference per trip is
	 RECORD_OVERHEAD_X100 - RISC_PUSH_X100 * words (currently at most
	 -192 centislots).  Thus the guard is a monotone
	 restriction over the admitted candidate domain, but that ordering is a
	 consequence of the cost constants and MIN_SEQUENCE, not its semantic
	 definition.
	 It keys only on the already-proven binding resource; no opcode, kernel,
	 payload length, or trip-count special case participates.  */
      if (dump_file)
	fprintf (dump_file,
		 "Replay completion guard: execution-bound re-record"
		 " charges hoisted delivery %ld (record cost %ld)\n",
		 (long) price.deliver_record, (long) record);
    }
  else if (price.hidden)
    {
      /* Execution-saturation term (delivery-bound re-record bodies
         only; hardware-witnessed on the unary-maxmin shape): when the
         body's contiguous run of sibling launches of this same buffer
         has enough execution surplus to hide the record pass's
         delivery, hoisting relieves nothing per trip.  An
         execution-bound record pass is never hidden this way: its cost
         is its own execution plus the exposed record-engine overhead,
         which no sibling surplus can absorb (Reduce-class hardware A/B,
         rvtt-cost.md).  */
      if (dump_file)
	fprintf (dump_file,
		 "Record delivery hidden: contiguous launch run %u exec"
		 " surplus %ld >= record delivery %ld\n",
		 launch_run, (long) price.surplus,
		 (long) price.deliver_record);
    }
  HOST_WIDE_INT benefit = price.benefit;

  if (dump_file)
    fprintf (dump_file,
	     "Hoist pricing (loop %d): trips %ld, words %ld,"
	     " exec_ilk %ld slots%s, deliver_body %ld,"
	     " deliver_record %ld, record %ld, before %ld, after %ld,"
	     " benefit %ld (min %ld)\n",
	     loop->num, (long) trips, (long) words, (long) eslots,
	     !body_rerecords ? ""
	     : exec_bound_rerecord ? " [re-record body, execution-bound]"
	     : " [re-record body, delivery-bound]",
	     (long) price.deliver_body, (long) price.deliver_record,
	     (long) record, (long) before, (long) after, (long) benefit,
	     (long) min_benefit);

  /* The ordinary record-hoist measurement model has a separately audited
     runtime-trip policy because its pure-delivery delta is monotone and its
     two-trip break-even bounds the single-trip exposure.  The completion
     guard deliberately routes through the shared binding-resource model
     instead.  With no proven trip count, pricing TRIPS as zero above is only
     a diagnostic witness; it is not an amortization proof.  Preserve the
     existing refusal, but name its actual cause rather than reporting a
     synthetic zero-trip profitability loss.  */
  if (runtime_trips && record_completion_model)
    {
      rvtt_refuse (RVTT_REF_RECORD_HOIST_COMPLETION_RUNTIME_TRIPS_UNPROVEN,
		   dump_file,
		   "Not hoisting:"
		   " record-hoist-completion-runtime-trips-unproven:"
		   " completion-accurate shared model requires a proven"
		   " trip count"
		   " (loop %d)\n",
		   loop->num);
      return false;
    }

  if (benefit < min_benefit)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Not hoisting: modeled benefit %ld < %ld\n",
		 (long) benefit, (long) min_benefit);
      return false;
    }

  if (dump_file)
    fprintf (dump_file, "Hoist profitable: modeled benefit %ld >= %ld\n",
	     (long) benefit, (long) min_benefit);
  return true;
}

/* Return LOOP's dedicated preheader: the unique block outside the loop
   with an edge into the header, provided that entry edge is normal and
   the block's only successor is the header (so an insn placed at its
   end executes exactly once per loop entry).  Null when the loop has
   several entry blocks or the candidate is shared with other code.  */

static basic_block
dedicated_loop_preheader (class loop *loop)
{
  basic_block preheader = nullptr;
  edge entry = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, loop->header->preds)
    if (!flow_bb_inside_loop_p (loop, e->src))
      {
	if (preheader)
	  return nullptr;
	preheader = e->src;
	entry = e;
      }

  return preheader && !(entry->flags & EDGE_ABNORMAL)
    && single_succ_p (preheader)
    ? preheader : nullptr;
}

/* A volatile store whose address is not provably outside the
   instruction-FIFO aperture can deliver ANY word -- including a REPLAY
   record that re-records hoisted slots (a fail-closed widening of
   the loop scan below; the flag-gated record-hoist path re-audits
   refused loops with the interval walk in rvtt-macro-epoch.cc, which
   also classifies the stored WORD).  Named data objects other than the
   recorded ABI anchor __instrn_buffer (crosscall precedent) and stack
   slots are provably not the FIFO; a constant address outside the
   aperture range is too; everything else refuses.  */
static bool
volatile_store_maybe_fifo_p (rtx pat)
{
  if (GET_CODE (pat) == PARALLEL)
    {
      for (int i = 0; i != XVECLEN (pat, 0); ++i)
	if (volatile_store_maybe_fifo_p (XVECEXP (pat, 0, i)))
	  return true;
      return false;
    }
  if (GET_CODE (pat) != SET)
    return false;
  rtx dest = SET_DEST (pat);
  if (!MEM_P (dest) || !MEM_VOLATILE_P (dest))
    return false;
  rtx addr = XEXP (dest, 0);
  if (CONST_INT_P (addr))
    {
      unsigned HOST_WIDE_INT a = UINTVAL (addr) & 0xffffffff;
      return a >= XTT_INSTRN_BUF_MMIO_BASE && a <= XTT_INSTRN_BUF_MMIO_LIMIT;
    }
  rtx base = addr;
  if (GET_CODE (base) == CONST)
    base = XEXP (base, 0);
  if (GET_CODE (base) == PLUS && CONST_INT_P (XEXP (base, 1)))
    base = XEXP (base, 0);
  if (GET_CODE (base) == LO_SUM)
    base = XEXP (base, 1);
  if (GET_CODE (base) == CONST)
    base = XEXP (base, 0);
  if (GET_CODE (base) == PLUS && CONST_INT_P (XEXP (base, 1)))
    base = XEXP (base, 0);
  if (GET_CODE (base) == SYMBOL_REF)
    return strcmp (XSTR (base, 0), "__instrn_buffer") == 0;
  if (REG_P (base) && REGNO (base) == STACK_POINTER_REGNUM)
    return false;
  return true;			/* unresolvable: fail closed */
}

/* A raw asm or an unknown callee can own or overwrite replay state without
   exposing that fact to this function's RTL.  Typed barriers are harmless
   here: they remain outside the payload and do not change the selected replay
   slots.  A typed owner is conservatively a boundary for this first hoisting
   implementation even though global slot accounting has already excluded its
   declared range.  Volatile stores that could target the instruction FIFO
   refuse fail-closed (they could push a REPLAY record word); see
   volatile_store_maybe_fifo_p.  */
static bool
loop_preserves_replay_p (class loop *loop)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfun)
    if (flow_bb_inside_loop_p (loop, bb))
      {
	rtx_insn *insn;
	FOR_BB_INSNS (bb, insn)
	  if (NONDEBUG_INSN_P (insn)
	      && (CALL_P (insn)
		  || asm_noperands (PATTERN (insn)) >= 0
		  || (GET_CODE (insn) == INSN
		      && recog_memoized (insn) >= 0
		      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
		  || (volatile_refs_p (PATTERN (insn))
		      && volatile_store_maybe_fifo_p (PATTERN (insn)))))
	    return false;
      }
  return true;
}

/* Exec-while-record first-trip peel
   (-mtt-tensix-optimize-record-hoist-peel, composing on
   -mtt-tensix-optimize-replay-record-hoist).

   The no-exec composition mirror below refuses every
   Dst-store re-record hoist whose preheader sits inside an outer loop:
   a STILL-NO-EXEC Dst-store capture re-ingested per outer trip with
   launches of its span in between is the hardware-refuted wedge
   (three independent device hangs), and the end-of-pass sweep would un-hoist it
   into a strict pessimization.  The refusal is exact for the no-exec
   shape -- but the SAME payload hoisted as an EXEC-WHILE-RECORD pass is
   the fleet-witnessed composition (minmax, sdpa, where, typecast, lcm
   ON-set; the dst-autoincr group guard's refuted class is keyed to
   TTREPLAY load=1 exec=0, rtl-rvtt-dst-autoincr.cc
   noexec_record_composition_p): today's in-body formation already
   re-records this very payload once per trip, exec-while-record, with
   sibling launches between re-ingestions.

   The peel therefore moves the loop's ENTIRE proven first trip to the
   dedicated preheader instead of a bare record: the capture records
   WHILE EXECUTING (exactly the words trip 1 executed in the loop), its
   sibling launches follow verbatim, every former in-body record site
   becomes one playback launch, and the proven-constant counter is
   re-initialized one step later so the loop runs trips-1 times.  The
   executed word stream is a pure peel -- no word is added, removed, or
   reordered relative to today's bytes except the removed per-trip
   record deliveries the record-hoist pricing already models -- so the
   re-ingestion cadence (one exec-record per outer-loop entry, launches
   between) stays inside the witnessed class.

   Admission (all named, fail-closed; anything unproven keeps the
   original composition refusal and today's bytes):
     record-hoist-peel-qsr-exec-record-unavailable  cannot exec while
                                        capturing on Quasar
     record-hoist-peel-multibb-loop     peel needs full body coverage
     record-hoist-peel-trips-unproven   constant trips >= 2 (a
                                        single-bb loop is do-while: with
                                        one proven trip the peeled loop
                                        would still execute once more)
     record-hoist-peel-body-foreign-insn a body insn outside the clone
                                        spans, counter step, and final
                                        jump (peel must cover the trip)
     record-hoist-peel-counter-rewrite-unproven  counter re-init not a
                                        provable single-insn constant
   The FZ downstream-fallback oracle is skipped for an admitted peel:
   it mirrors the dst-autoincr group guard's NO-EXEC-record clause, and
   an exec-while-record capture is outside that refuted composition by
   the guard's own keying (the group guard itself still audits the
   final placement at its own pass time).  The end-of-pass sweep sees
   the peeled capture through formed_noexec_captures and skips it as
   the exec-converted witnessed class.  */

/* Structural admission of the exec-while-record first-trip peel (see
   the design comment above struct peel_plan).  LOOP must be a
   single-block loop with constant proven trips >= 2 whose counter
   re-init constant is materializable in one insn at PREHEADER, and
   whose every non-debug body insn is a clone-span member of SEQ, a
   typed fixed-encoding TTINCRWC, the counter step, or the final jump.
   On success fills *PLAN (counter, re-init value, mode, trips) and
   returns true; each failing premise fires its named refusal and
   returns false.  */

static bool
peel_admissible_p (class loop *loop, basic_block preheader,
		   replay_block const &block, replay_sequence const &seq,
		   peel_plan *plan)
{
  auto refuse = [] (const char *why, int uid) -> bool
    {
      rvtt_refusal_fire_composed ("record-hoist-peel", why);
      if (dump_file)
	{
	  fprintf (dump_file, "record-hoist refused: record-hoist-peel-%s",
		   why);
	  if (uid >= 0)
	    fprintf (dump_file, ": body insn %d", uid);
	  fprintf (dump_file, "\n");
	}
      return false;
    };

  if (riscv_tt_fix_qsr_replay > 0)
    return refuse ("qsr-exec-record-unavailable", -1);
  if (loop->num_nodes != 1)
    return refuse ("multibb-loop", -1);

  uint64_t trips;
  rtx_insn *step_insn = nullptr;
  if (!rvtt_loop_trips (loop, preheader, &trips, &step_insn)
      || trips < 2)
    return refuse ("trips-unproven", -1);

  rtx step_set = single_set (step_insn);
  rtx counter = SET_DEST (step_set);
  uint64_t step = UINTVAL (XEXP (SET_SRC (step_set), 1));
  scalar_int_mode mode = as_a<scalar_int_mode> (GET_MODE (counter));
  unsigned prec = GET_MODE_PRECISION (mode);
  uint64_t mask = prec == 64 ? ~uint64_t (0) : (uint64_t (1) << prec) - 1;
  uint64_t init;
  if (!rvtt_constant_reaching_value (preheader, counter, &init))
    return refuse ("counter-rewrite-unproven", -1);
  uint64_t new_init = (init + step) & mask;
  rtx new_init_rtx = gen_int_mode (new_init, mode);
  /* Post-reload only single-insn constants may be materialized.  */
  if (!SMALL_OPERAND (INTVAL (new_init_rtx))
      && !LUI_OPERAND (INTVAL (new_init_rtx)))
    return refuse ("counter-rewrite-unproven", -1);

  /* Full body coverage: every non-debug insn of the single-block body
     is a clone-span member, a typed fixed-encoding TTINCRWC (the
     per-trip Dst step the counted-loop and launch-unroll admissions
     already type; the peel copies it verbatim in trip order), the
     counter step, or the final jump.  A word outside these classes
     would be silently dropped from the peeled first trip: refuse.  */
  hash_set<rtx_insn *> covered;
  for (auto const &clone : seq.clones)
    for (auto pos = block.data () + clone.begin,
	  end = block.data () + clone.end; pos != end; ++pos)
      covered.add (pos->insn);
  rtx_insn *jump = BB_END (loop->header);
  rtx_insn *insn;
  FOR_BB_INSNS (loop->header, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (insn == step_insn || insn == jump || covered.contains (insn))
	continue;
      if (GET_CODE (insn) == INSN
	  && recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
	  && fixed_replay_rtx_p (PATTERN (insn)))
	continue;
      return refuse ("body-foreign-insn", INSN_UID (insn));
    }

  plan->valid = true;
  plan->counter = counter;
  plan->new_init = new_init;
  plan->mode = mode;
  plan->trips = trips;
  return true;
}

/* Lane IL: record-hoist placement lift
   (-mtt-tensix-optimize-record-hoist-lift, composing on
   -mtt-tensix-optimize-replay-record-hoist).

   The FZ downstream-fallback oracle below refuses a re-record hoist
   whose no-exec record would be INGESTED within the audited
   drained-frontend window downstream of a would-be dst-autoincr
   mod-write row (the oracle's distance walk runs UPSTREAM of the
   placement; the lcm-fresh shape: the row's own mod-write loads and
   store reach the immediate preheader across the backedge in fewer
   issue words than the window).  The refused placement is only the
   INNERMOST dedicated preheader: a placement further out -- an
   enclosing loop's dedicated preheader, ultimately the function entry,
   whose every upstream path is proven separated (>= the window of
   cover, or reaches the function entry) -- is outside the mirrored
   composition class by the guard's own distance semantics, and is
   exactly the witnessed init-record discipline (the xielu/gcd/lcm
   preamble placements; the raw gcd init records its round program once
   per kernel at entry).

   The lift therefore walks OUTWARD from the refused preheader across
   enclosing loops and commits the UNCHANGED no-exec hoist at the
   outermost admissible oracle-clean placement:

     - every crossed loop must prove replay-preserving under the
       record-hoist interval walk (an in-loop replay owner, call, asm,
       or possible instruction-FIFO push could re-record the lifted
       slots between the record and a later trip's launch; the walk
       covers every intermediate block -- they all lie in some crossed
       loop's body);
     - each candidate placement must be a DEDICATED preheader, hold no
       open user recording state, and itself pass the same
       downstream-fallback oracle (a placement still within a
       mod-write's drained-frontend window walks on);
     - a failing level stops the walk (never refuses; the
       residency-walk discipline); with no oracle-clean admissible
       level the original composition refusal stands byte-identically
       (record-hoist-lift-no-admissible-level).

   Soundness is the EXISTING hoisted no-exec capture class at a
   different placement: the record still DOMINATES every launch and is
   not forward-reachable from any without re-entering the placement
   itself (preheader chain -- the expander persistence rules hold), the
   payload is storeless here (the Dst-store mirror above refuses those
   payloads before the oracle ever runs; rule 1 of the end-of-pass
   sweep is keyed to Dst-store payloads, and storeless no-exec captures
   are the hardware-witnessed-good celu/eqz class), a placement still inside an
   outer loop re-ingests the SAME fixed-encoding words once per that
   loop's trip (idempotent; invariance is the record-hoist
   fixed-encoding admission, checked before the oracle), and the
   end-of-pass sweep's rule 2 re-audits the final placement's
   mod-write distance with the same predicate.  The record-hoist
   delivery pricing runs unchanged on the immediate loop: the lifted
   record is delivered at most as often as the modeled
   immediate-preheader record, so the modeled benefit is a floor.  */

/* The placement lift's outward walk (see the block comment above
   hoist_lift_plan): starting at the oracle-refused PREHEADER, cross
   enclosing loops as long as each proves replay-preserving, has a
   dedicated preheader, and holds no open recording state (DIRTY_BBS);
   remember the outermost crossed preheader the mod-write oracle
   accepts.  On success fills *LIFT with that placement and its level
   count and returns true; otherwise fires the named refusal
   record-hoist-lift-no-admissible-level and returns false.  */

static bool
hoist_lift_admit (basic_block preheader, bitmap dirty_bbs,
		  hoist_lift_plan *lift)
{
  hash_set<rtx_insn *> pass_launches;
  for (rtx_insn *launch : formed_playback_launches)
    pass_launches.add (launch);

  basic_block best = nullptr;
  unsigned best_levels = 0;
  unsigned levels = 0;
  basic_block ph = preheader;
  for (class loop *l = ph->loop_father; l && l->num != 0;)
    {
      basic_block *body = get_loop_body (l);
      rtx_insn *refusal_insn = nullptr;
      const char *refusal = rvtt_macro_epoch_loop_replay_preserved_p
	(cfun, body, l->num_nodes, l->header, pass_launches, &refusal_insn);
      free (body);
      if (refusal)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d"
		     " (%s, insn %d)\n",
		     l->num, refusal,
		     refusal_insn ? INSN_UID (refusal_insn) : -1);
	  break;
	}
      basic_block up = dedicated_loop_preheader (l);
      if (!up)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d"
		     " (no dedicated preheader)\n", l->num);
	  break;
	}
      if (bitmap_bit_p (dirty_bbs, up->index))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "record-hoist-lift: level stop at loop %d (preheader"
		     " bb %d may hold open recording state)\n",
		     l->num, up->index);
	  break;
	}
      ++levels;
      unsigned dist = 0;
      if (!rvtt_dst_autoincr_hoist_capture_composition_p (up, &dist))
	{
	  best = up;
	  best_levels = levels;
	}
      else if (dump_file)
	fprintf (dump_file,
		 "record-hoist-lift: level %u placement bb %d still within"
		 " a mod-write drained-frontend window (distance %u);"
		 " walking on\n", levels, up->index, dist);
      ph = up;
      l = ph->loop_father;
    }

  if (!best)
    {
      rvtt_refuse (RVTT_REF_RECORD_HOIST_LIFT_NO_ADMISSIBLE_LEVEL, dump_file,
		   "record-hoist-lift refused:"
		   " record-hoist-lift-no-admissible-level:"
		   " no oracle-clean admissible placement (walked %u"
		   " level(s))\n", levels);
      return false;
    }

  lift->valid = true;
  lift->placement = best;
  lift->levels = best_levels;
  if (dump_file)
    fprintf (dump_file,
	     "record-hoist-lift: lifted placement to bb %d (%u level(s)"
	     " out; oracle-clean, crossed loops replay-preserving)\n",
	     best->index, best_levels);
  return true;
}

/* Full hoist admission for candidate SEQ (its clones live in BLOCK).
   Returns the loop's dedicated preheader when every proof holds -- loop
   shape (single-block header, or latch-dominating capture under the
   record-hoist flag), loop replay preservation, no open recording state
   (DIRTY_BBS), fixed-encoding payload, the Dst-store composition mirror
   (rescuable by an admitted first-trip peel into *PEEL), the
   downstream-fallback oracle (rescuable by an admitted placement lift
   into *LIFT), and the pricing gate -- and nullptr otherwise, leaving
   the in-block formation to proceed.  Refusals are named; see the
   design comments above hoist_profitable_p, peel_admissible_p and
   hoist_lift_admit.  */

basic_block
hoist_preheader (replay_sequence const &seq, replay_block const &block,
		 bitmap dirty_bbs, peel_plan *peel, hoist_lift_plan *lift)
{
  basic_block bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return nullptr;
  bool record_hoist = riscv_tt_opt_replay_record_hoist > 0;
  if (loop->num_nodes != 1 || loop->header != bb)
    {
      /* Lane FW: under the record-hoist flag a MULTI-BLOCK loop admits
	 when the capture bb dominates the loop latch -- the capture
	 (and so its clone deliveries) executes on every completed trip,
	 which is the fact the per-trip pricing consumes; the
	 replay-preservation audit below walks EVERY block of the loop,
	 so slot liveness needs no single-block shape.  Real measured
	 vehicles are exactly this shape: the profiler zone code splits
	 the tile loop into several blocks (buffer-management branches
	 around an always-executed body).  A capture bb that does NOT
	 dominate the latch executes conditionally -- its per-trip
	 delivery saving is unpriced -- and refuses by the same name.  */
      bool multi_bb_ok = false;
      if (record_hoist && loop->latch)
	{
	  bool free_dom = !dom_info_available_p (CDI_DOMINATORS);
	  calculate_dominance_info (CDI_DOMINATORS);
	  multi_bb_ok = dominated_by_p (CDI_DOMINATORS, loop->latch, bb);
	  if (free_dom)
	    free_dominance_info (CDI_DOMINATORS);
	}
      if (!multi_bb_ok)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Not hoisting: candidate bb %d is not a single-bb loop"
		       " header\n",
		       bb->index);
	      if (record_hoist)
		rvtt_refuse (RVTT_REF_RECORD_HOIST_LOOP_SHAPE, dump_file,
			     "record-hoist refused: record-hoist-loop-shape:"
			     " capture bb %d does not dominate the latch"
			     " (conditional per-trip execution unpriced)\n",
			     bb->index);
	    }
	  return nullptr;
	}
      if (dump_file && record_hoist)
	fprintf (dump_file,
		 "record-hoist: multi-block loop %d admitted (capture bb %d"
		 " dominates latch bb %d)\n",
		 loop->num, bb->index, loop->latch->index);
    }
  if (!loop_preserves_replay_p (loop))
    {
      /* Lane FW: under the record-hoist flag, re-audit the refused loop
	 with the interval-resolving replay-preservation walk (LLK tile
	 loops always carry raw sync words and computed FIFO pushes; the
	 walk proves them unable to modify replay-buffer state, admits
	 this pass's own playback launches -- the multi-record calendar
	 -- and keeps everything unresolvable refused by name).  The
	 walk covers every block of the loop (multi-block tile loops
	 admit under the latch-dominance shape check above).  */
      const char *audit_refusal = nullptr;
      rtx_insn *audit_insn = nullptr;
      if (record_hoist)
	{
	  hash_set<rtx_insn *> pass_launches;
	  for (rtx_insn *launch : formed_playback_launches)
	    pass_launches.add (launch);
	  basic_block *body = get_loop_body (loop);
	  audit_refusal = rvtt_macro_epoch_loop_replay_preserved_p
	    (cfun, body, loop->num_nodes, loop->header, pass_launches,
	     &audit_insn);
	  free (body);
	}
      if (!record_hoist || audit_refusal)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "Not hoisting: loop contains call, opaque asm, or replay"
		       " owner\n");
	      /* For the record-hoist this is also the in-loop slot-liveness
		 proof: an in-loop replay owner (or an asm/call that could hide
		 one) could re-record the hoisted capture's slots between the
		 preheader record and a later trip's launch.  Every other
		 window this pass forms lives entirely inside one basic block
		 (record to last launch), the loop is single-block, and
		 persistent-slot marking excludes the hoisted range from all
		 later formation, so this refusal closes the only re-record
		 path into the hoisted slots.  */
	      if (record_hoist)
		rvtt_refuse (RVTT_REF_RECORD_HOIST_LOOP_OPAQUE, dump_file,
			     "record-hoist refused: record-hoist-loop-opaque:"
			     " %s (insn %d)\n", audit_refusal,
			     audit_insn ? INSN_UID (audit_insn) : -1);
	    }
	  return nullptr;
	}
      if (dump_file)
	fprintf (dump_file,
		 "record-hoist: loop %d replay-state audit admitted"
		 " (every body word proven replay-preserving)\n",
		 loop->num);
    }

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    {
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Not hoisting: loop has no dedicated preheader\n");
	  if (record_hoist)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_NO_DEDICATED_PREHEADER,
			 dump_file,
			 "record-hoist refused:"
			 " record-hoist-no-dedicated-preheader\n");
	}
      return nullptr;
    }
  if (bitmap_bit_p (dirty_bbs, preheader->index))
    {
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Not hoisting: preheader bb %d may hold open recording"
		   " state\n", preheader->index);
	  if (record_hoist)
	    rvtt_refuse (RVTT_REF_RECORD_HOIST_PREHEADER_RECORDING_OPEN,
			 dump_file,
			 "record-hoist refused:"
			 " record-hoist-preheader-recording-open\n");
	}
      return nullptr;
    }

  /* Record-hoist invariance admission (before pricing): every payload
     word must be fixed-encoding -- hard LREGs, constants, and compiler
     scratch only.  A GPR or MEM operand means the delivered instruction
     word is composed at run time; its value at the preheader is not
     proven equal to its value at the original in-body record point, so
     the recorded program could differ from what each trip's launch must
     replay.  No rebase model exists for such words in this first
     increment: refuse by name.  (The flag-off hoist path keeps its
     original post-pricing check below, byte-identically.)  */
  if (record_hoist)
    for (auto pos = block.data () + seq.clones.front ().begin,
	  end = block.data () + seq.clones.front ().end;
	 pos != end; ++pos)
      if (!pos->empty && !fixed_replay_rtx_p (PATTERN (pos->insn)))
	{
	  rvtt_refuse (RVTT_REF_RECORD_HOIST_VARIANT_ENCODING, dump_file,
		       "record-hoist refused: record-hoist-variant-encoding:"
		       " payload insn %d is a run-time-composed word\n",
		       INSN_UID (pos->insn));
	  return nullptr;
	}

  /* Admission-side mirror of the fail-closed re-record sweep's rule 1:
     a Dst-store payload whose no-exec record would land in a
     preheader that itself sits inside a natural loop is EXACTLY the
     shape unhoist_hazard_rerecords un-hoists at the end of transform
     (noexec-rerecord-dststore-composition-unaudited) -- and the
     un-hoist's identity restoration is relative to the HOISTED world
     (every launch becomes an inline payload copy), a strict delivery
     pessimization against never having hoisted.  Forming a provably
     doomed hoist is a known-losing transform: refuse it here by the
     sweep's own name and keep the in-body formation byte-identically.
     (The dominating loop-free-preheader Dst-store class stays admitted:
     the sweep's rule 3 keeps it -- the witnessed init-record class.)  */
  if (record_hoist)
    {
      class loop *ph_loop = preheader->loop_father;
      if (ph_loop && ph_loop->num != 0)
	for (auto pos = block.data () + seq.clones.front ().begin,
	      end = block.data () + seq.clones.front ().end;
	     pos != end; ++pos)
	  if (!pos->empty
	      && (recog_memoized (pos->insn) == CODE_FOR_rvtt_sfpstore_int
		  || recog_memoized (pos->insn)
		     == CODE_FOR_rvtt_sfpstoresrcs_int))
	    {
	      /* Lane GQ: the exec-while-record first-trip peel rescues
		 this exact refusal class (see the block comment above
		 peel_admissible_p); an admitted peel never leaves a
		 no-exec Dst-store capture behind, so the composition
		 this mirror refuses is never formed.  */
	      /* Lane IH (reform_mode): the peel RELOCATES one trip's
		 payload executions into the preheader.  For a CARRIED
		 payload the relocated executions advance the owned
		 ADDR_MOD walk at a new program point, across the
		 configuration program's placement -- the walk-order proof
		 for that relocation is not in this increment, so the
		 launch-arithmetic guard refuses the peel by name and lets
		 the mirror refusal below stand (the candidate falls back
		 to in-block formation, which is stream-identity sound).  */
	      if (reform_mode
		  && payload_contains_carried_p (block, seq.clones.front ()))
		{
		  rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_PEEL_LAUNCH_ARITHMETIC_UNPROVEN, dump_file,
			       "record-hoist refused:"
			       " post-autoincr-window-carried-peel-"
			       "launch-arithmetic-unproven:"
			       " carried payload, first-trip peel would"
			       " relocate carried executions to preheader"
			       " bb %d (walk-order proof not in this"
			       " increment)\n",
			       preheader->index);
		}
	      else if (riscv_tt_opt_record_hoist_peel > 0
		       && peel_admissible_p (loop, preheader, block, seq,
					     peel))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "record-hoist-peel: dststore composition"
			     " rescued by exec-while-record first-trip peel"
			     " (preheader bb %d inside loop %d; trips %lu"
			     " -> %lu)\n",
			     preheader->index, ph_loop->num,
			     (unsigned long) peel->trips,
			     (unsigned long) (peel->trips - 1));
		  break;
		}
	      rvtt_refuse
		(RVTT_REF_NOEXEC_RERECORD_DSTSTORE_COMPOSITION_UNAUDITED,
		 dump_file,
		 "record-hoist refused:"
		 " noexec-rerecord-dststore-composition-unaudited:"
		 " Dst-store payload, preheader bb %d inside loop %d"
		 " (the re-record sweep would un-hoist)\n",
		 preheader->index, ph_loop->num);
	      return nullptr;
	    }
    }

  /* Downstream-fallback composition pricing (rvtt-cost.md
     "RECORD-HOIST x MOD-WRITE COMPOSITION").  The record-hoist pricing
     below is licensed by the streams-identical premise: the hoisted and
     unhoisted worlds EXECUTE the same word stream, so the modeled delta
     is pure delivery.  A no-exec record hoisted to within the audited
     drained-frontend window of a row the dst-autoincr pass would
     otherwise transform into a mod-write voids that premise: the
     dst-autoincr group guard is certain to refuse the group (the
     hardware-refuted no-exec-record x mod-write composition, fail-closed
     and correct), so the hoisted world executes the explicit-increment
     fallback while the unhoisted world executes the mod-write form --
     different executed streams whose delta the delivery-only model
     cannot price.  The one hardware measurement on the composed shape
     found it NET NEGATIVE (+6.0 cycles/tile on the lcm-fresh kernel
     against the unhoisted+mod-write world; rvtt-cost.md entry), so a hoist that
     induces the fallback refuses by name and keeps today's bytes.  The
     oracle mirrors the group guard's own distance semantics and audited
     window (single source, rtl-rvtt-dst-autoincr.cc); no distance an
     admitted hoist leaves behind can flip the guard.  Gated on the
     dst-autoincr pass actually running: with it disabled both worlds
     keep the explicit increments and the premise holds.  */
  /* Lane GQ: an admitted peel hoists an EXEC-WHILE-RECORD pass; the
     oracle below mirrors the dst-autoincr group guard's NO-EXEC-record
     clause (its refuted composition is keyed to TTREPLAY load=1 exec=0),
     so the peel is outside the mirrored class by the guard's own keying
     -- the guard itself still audits the final placement.  */
  if (record_hoist && !peel->valid
      && TARGET_XTT_TENSIX && riscv_tt_opt_dst_autoincr > 0)
    {
      unsigned dist = 0;
      if (rvtt_dst_autoincr_hoist_capture_composition_p (preheader, &dist))
	{
	  /* Lane IL: the placement lift (see the block comment above
	     hoist_lift_plan).  An outer oracle-clean dedicated
	     preheader is outside the mirrored composition class by the
	     guard's own distance semantics; when the outward walk
	     admits one, continue to the unchanged record-hoist pricing
	     and commit the unchanged no-exec hoist there.  Any lift
	     refusal keeps the composition refusal (and today's bytes)
	     verbatim below.  */
	  if (!(riscv_tt_opt_record_hoist_lift > 0
		&& hoist_lift_admit (preheader, dirty_bbs, lift)))
	    {
	      rvtt_refuse
		(RVTT_REF_RECORD_HOIST_DOWNSTREAM_FALLBACK_UNPROFITABLE,
		 dump_file,
		 "record-hoist refused:"
		 " record-hoist-downstream-fallback-unprofitable:"
		 " hoisted no-exec record within the drained-frontend"
		 " window of a would-be dst-autoincr mod-write row"
		 " (distance %u < %u, preheader bb %d; the group guard"
		 " would refuse and the mod-write falls back)\n",
		 dist, rvtt_modwrite_drained_frontend_window (),
		 preheader->index);
	      return nullptr;
	    }
	}
    }

  if (!hoist_profitable_p (loop, preheader, block, seq.clones.front (),
			   /*body_rerecords=*/true,
			   max_contiguous_launch_run (seq, block)))
    return nullptr;

  for (auto pos = block.data () + seq.clones.front ().begin,
	end = block.data () + seq.clones.front ().end;
       pos != end; ++pos)
    if (!pos->empty && !fixed_replay_rtx_p (PATTERN (pos->insn)))
      {
	if (dump_file)
	  fprintf (dump_file,
		   "Not hoisting: payload insn %d is not fixed encoding\n",
		   INSN_UID (pos->insn));
	return nullptr;
      }

  return preheader;
}

/* No-exec captures THIS PASS hoisted into preheaders this function, for
   the fail-closed re-record sweep at the end of transform ()
   (see unhoist_hazard_rerecords).  User-authored records
   are never entered here and stay untouched.  */
std::vector<rtx_insn *> formed_noexec_captures;

/* Commit an admitted hoist of SEQ: emit a no-exec capture followed by a
   copy of the payload's non-empty insns at the end of PREHEADER (before
   its jump if it has one), recording into slots [REPLAY_START,
   +length); then replace EVERY clone -- the first included -- with one
   playback launch and delete the clone's insns.  Registers the capture
   and launches for the end-of-pass sweeps.  Returns the recorded
   length.  */

unsigned
replace_hoisted_sequence (replay_sequence &seq, replay_block &block,
			  unsigned replay_start, basic_block preheader)
{
  unsigned length = seq.length;
  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
     const0_rtx, GEN_INT (1));

  start_sequence ();
  emit_insn (capture);
  for (auto pos = block.data () + seq.clones.front ().begin,
	end = block.data () + seq.clones.front ().end;
       pos != end; ++pos)
    if (!pos->empty)
      emit_insn (copy_insn (PATTERN (pos->insn)));
  rtx_insn *recording = get_insns ();
  end_sequence ();

  rtx_insn *anchor = BB_END (preheader);
  if (JUMP_P (anchor))
    emit_insn_before (recording, anchor);
  else
    emit_insn_after (recording, anchor);

  /* The first insn of the emitted sequence is the no-exec capture:
     register it for the fail-closed re-record sweep.  */
  formed_noexec_captures.push_back (recording);

  for (auto const &clone : seq.clones)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
      rtx_insn *launch
	= emit_insn_after (replay, block[clone.end - 1].insn);
      formed_playback_launches.push_back (launch);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	SET_INSN_DELETED (pos->insn);
    }

  if (dump_file)
    fprintf (dump_file,
	     "Hoisted no-exec capture [%u,+%u) to preheader bb %d;"
	     " %u playbacks\n\n",
	     replay_start, length, preheader->index,
	     unsigned (seq.clones.size ()));
  return length;
}

/* Lane GQ: commit an admitted exec-while-record first-trip peel (see
   the block comment above peel_admissible_p).  The preheader receives,
   in trip order: the counter re-init (scalar, commutes with every
   Tensix word), the exec-while-record capture, the payload (executing
   exactly as trip 1 executed it), and the first trip's sibling
   launches.  Every in-body clone -- including the former record site --
   becomes one playback launch.  */

unsigned
replace_hoisted_sequence_peel (replay_sequence &seq, replay_block &block,
			       unsigned replay_start, basic_block preheader,
			       peel_plan const &plan)
{
  unsigned length = seq.length;
  basic_block body_bb = BLOCK_FOR_INSN (block[seq.clones.front ().begin].insn);
  rtx_insn *body_jump = BB_END (body_bb);

  /* Classify body insns for the verbatim in-order trip walk: which
     clone (if any) each member belongs to, and each clone's first
     member.  */
  hash_map<rtx_insn *, unsigned> member_clone;
  hash_set<rtx_insn *> clone_first;
  for (unsigned ix = 0; ix != seq.clones.size (); ++ix)
    {
      auto const &clone = seq.clones[ix];
      clone_first.add (block[clone.begin].insn);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	member_clone.put (pos->insn, ix);
    }

  auto gen_launch = [&] () -> rtx
    {
      return gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
    };

  start_sequence ();
  emit_insn (gen_rtx_SET (plan.counter,
			  gen_int_mode (plan.new_init, plan.mode)));
  unsigned peeled_launches = 0, peeled_steps = 0;
  {
    rtx_insn *insn;
    FOR_BB_INSNS (body_bb, insn)
      {
	if (!NONDEBUG_INSN_P (insn) || insn == body_jump)
	  continue;
	if (unsigned *cl = member_clone.get (insn))
	  {
	    if (*cl == 0)
	      {
		if (clone_first.contains (insn))
		  emit_insn (gen_rvtt_ttreplay_int
			     (const0_rtx, const0_rtx, const0_rtx,
			      GEN_INT (length),
			      rvtt_gen_rtx_noval (XTT32SImode),
			      GEN_INT (replay_start),
			      GEN_INT (1), GEN_INT (1)));
		/* Copy the member itself (mirror the hoist path: only
		   non-empty members deliver a word).  */
		for (auto pos = block.data () + seq.clones.front ().begin,
		      end = block.data () + seq.clones.front ().end;
		     pos != end; ++pos)
		  if (pos->insn == insn)
		    {
		      if (!pos->empty)
			emit_insn (copy_insn (PATTERN (insn)));
		      break;
		    }
	      }
	    else if (clone_first.contains (insn))
	      {
		emit_insn (gen_launch ());
		++peeled_launches;
	      }
	    continue;
	  }
	if (GET_CODE (insn) == INSN
	    && recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc)
	  {
	    emit_insn (copy_insn (PATTERN (insn)));
	    ++peeled_steps;
	    continue;
	  }
	/* The counter step: handled by the re-init.  */
      }
  }
  rtx_insn *recording = get_insns ();
  end_sequence ();

  rtx_insn *anchor = BB_END (preheader);
  if (JUMP_P (anchor))
    emit_insn_before (recording, anchor);
  else
    emit_insn_after (recording, anchor);

  /* Register the capture for the end-of-pass sweep (it skips the
     exec-converted witnessed class by the exec operand) and the peeled
     launches for the loop replay-preservation audit.  */
  bool saw_capture = false;
  for (rtx_insn *insn = recording; insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn) && GET_CODE (insn) == INSN
	  && recog_memoized (insn) == CODE_FOR_rvtt_ttreplay_int)
	{
	  if (!saw_capture)
	    {
	      formed_noexec_captures.push_back (insn);
	      saw_capture = true;
	    }
	  else
	    formed_playback_launches.push_back (insn);
	}
      if (insn == BB_END (preheader))
	break;
    }

  for (auto const &clone : seq.clones)
    {
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (replay_start),
	 const0_rtx, const0_rtx);
      rtx_insn *launch
	= emit_insn_after (replay, block[clone.end - 1].insn);
      formed_playback_launches.push_back (launch);
      for (auto pos = block.data () + clone.begin,
	    end = block.data () + clone.end; pos != end; ++pos)
	SET_INSN_DELETED (pos->insn);
    }

  if (dump_file)
    fprintf (dump_file,
	     "record-hoist-peel: exec-recorded [%u,+%u) in preheader bb %d"
	     " with %u peeled sibling launches + %u peeled Dst steps;"
	     " %u body clones -> launches; trips %lu -> %lu;"
	     " counter reinit " HOST_WIDE_INT_PRINT_DEC "\n\n",
	     replay_start, length, preheader->index,
	     peeled_launches, peeled_steps,
	     unsigned (seq.clones.size ()),
	     (unsigned long) plan.trips, (unsigned long) (plan.trips - 1),
	     (HOST_WIDE_INT) plan.new_init);
  return length;
}

/* A counted-loop capture must be one fixed, uninterrupted SFPU run.  Scalar
   loop-control instructions may surround it.  A single typed TTINCRWC may
   follow the run: it remains explicit after the playback and therefore
   preserves the per-iteration Dst boundary used by semantic SFPI loops.
   Counter operations before or inside the run, ordinary memory, calls, opaque
   asm, configuration operations, dynamic instruction words, and explicit
   replay ownership make the whole loop ineligible.  */
static bool
counted_loop_payload (class loop *loop, replay_block &info,
		      replay_sequence &seq)
{
  if (loop->num_nodes != 1 || loop->header != loop->latch
      || !loop_preserves_replay_p (loop))
    return false;

  bool saw_safe = false;
  bool saw_trailing_increment = false;
  rtx_insn *insn;
  FOR_BB_INSNS (loop->header, insn)
    if (NONDEBUG_INSN_P (insn))
      {
	if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0
	    || contains_mem_rtx_p (PATTERN (insn)))
	  return false;
	if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	    && get_attr_type (insn) == TYPE_TENSIX)
	  {
	    if (get_attr_xtt_replay (insn) == XTT_REPLAY_SAFE
		&& fixed_replay_rtx_p (PATTERN (insn)))
	      {
		if (saw_trailing_increment)
		  return false;
		saw_safe = true;
	      }
	    else if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
		     && saw_safe && !saw_trailing_increment)
	      saw_trailing_increment = true;
	    else
	      return false;
	  }
      }

  if (!scan_insns (info, loop->header))
    return false;

  unsigned length = 0;
  for (unsigned ix = 0; ix != info.size (); ++ix)
    {
      if (ix + 1 != info.size () && info[ix].must_end)
	return false;
      if (!info[ix].empty)
	++length;
    }
  if (length < MIN_SEQUENCE)
    return false;

  seq = replay_sequence (0, 0, length);
  seq.clones.emplace_back (0, info.size ());
  return true;
}

/* Lane IO: counted-loop capture exec-while-record peel pricing
   (-mtt-tensix-optimize-counted-capture-peel; rvtt-cost.md
   "COUNTED-CAPTURE PEEL").  The plain counted-loop hoist pays the full
   preheader record delivery (deliver_record + RECORD_OVERHEAD) and
   refuses when trips * (before - after) cannot amortize it.  The
   PEELED shape never re-delivers the payload: the loop's proven first
   trip moves verbatim to the dedicated preheader and executes WHILE
   recording, so the record pass costs only the capture word plus the
   record-engine overhead beyond the payload delivery the baseline
   first trip already paid; trips - 1 playback launches remain in the
   loop.

     benefit = (trips - 1) * (before - after)
	       - (RISC_PUSH + RECORD_OVERHEAD)	 ; >= MIN_BENEFIT

   with before/after the counted-loop capture terms unchanged
   (before = max(deliver_body, exec); after = max(PUSH, exec +
   TURNAROUND)).  The executed word stream is a pure peel of the rolled
   loop -- payload instances 1 + (trips-1) = trips, in trip order at
   the same stream positions (the preheader immediately precedes the
   loop) -- so this inherits the GQ peel's stream-identity argument
   with a weaker premise: there is no former in-body record site at
   all.  An unpriceable payload keeps the reissue-latency refusal by
   name; trips must be proven >= 2 (the peel executes one trip in the
   preheader).  Purely structural: no operation identity, opcode
   calendar, coefficient value, or instruction-word fingerprint
   participates.  */

static bool
counted_peel_profitable_p (class loop *loop, basic_block preheader,
			   replay_block const &block, replay_span payload)
{
  uint64_t niter;
  if (!rvtt_loop_trips (loop, preheader, &niter) || niter < 2)
    {
      rvtt_refuse (RVTT_REF_COUNTED_CAPTURE_PEEL_TRIPS_UNPROVEN, dump_file,
		   "counted-capture-peel refused:"
		   " counted-capture-peel-trips-unproven (loop %d)\n",
		   loop->num);
      return false;
    }
  HOST_WIDE_INT trips = (HOST_WIDE_INT) niter;
  HOST_WIDE_INT eslots = exec_interlocked_slots (block, payload);
  if (eslots < 0)
    {
      rvtt_refuse (RVTT_REF_REPLAY_REISSUE_LATENCY_UNPROVED, dump_file,
		   "counted-capture-peel refused:"
		   " replay-reissue-latency-unproved (loop %d)\n",
		   loop->num);
      return false;
    }
  HOST_WIDE_INT words = delivered_words (block, payload);
  HOST_WIDE_INT min_benefit = rvtt_dcost_replay_hoist_min_benefit ();
  /* The one replay pricing spelling (rvtt-delivery-cost-core.h,
     SHAPE_COUNTED_PEEL).  */
  rvtt_delivery_cost::replay_price price = rvtt_dcost_replay_pricing
    (rvtt_delivery_cost::SHAPE_COUNTED_PEEL, trips, words, eslots,
     /*launch_run=*/1, false, min_benefit);
  HOST_WIDE_INT before = price.before;
  HOST_WIDE_INT after = price.after;
  HOST_WIDE_INT peel_cost = price.record;
  HOST_WIDE_INT benefit = price.benefit;
  if (dump_file)
    fprintf (dump_file,
	     "Counted-peel pricing (loop %d): trips %ld, words %ld,"
	     " exec_ilk %ld slots, before %ld, after %ld, peel_cost %ld,"
	     " benefit %ld (min %ld)\n",
	     loop->num, (long) trips, (long) words, (long) eslots,
	     (long) before, (long) after, (long) peel_cost,
	     (long) benefit, (long) min_benefit);
  if (!price.profitable)
    {
      rvtt_refuse (RVTT_REF_COUNTED_CAPTURE_PEEL_BENEFIT, dump_file,
		   "counted-capture-peel refused:"
		   " counted-capture-peel-benefit: modeled benefit %ld < %ld\n",
		   (long) benefit, (long) min_benefit);
      return false;
    }
  return true;
}

/* Hoist every eligible counted loop of CFN: a single-block loop whose
   body is one uninterrupted fixed-encoding SFPU run
   (counted_loop_payload) becomes a no-exec capture in its dedicated
   preheader plus one playback launch per trip -- or, when the plain
   pricing refuses and the peel flag admits, an exec-while-record
   first-trip peel.  Slots come from REPLAY_SPANS minus
   PERSISTENT_SLOTS; a committed capture marks its slots persistent so
   the later in-block formation never reuses them.  DIRTY_BBS excludes
   blocks with possibly-open user recording state; STICKY is the
   function's shadow-coupling possibility for the companion checks.  */

void
hoist_counted_loops (function *cfn,
		     std::vector<replay_span> const &replay_spans,
		     std::vector<bool> &persistent_slots,
		     bitmap dirty_bbs, bool sticky)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;
      if (bitmap_bit_p (dirty_bbs, bb->index))
	/* Recording state may be open here (unprovable user epoch).  */
	continue;

      replay_block info;
      replay_sequence seq;
      if (!counted_loop_payload (loop, info, seq))
	continue;
      if (!span_companion_sound_p (info, seq.clones.front (), sticky))
	continue;
      /* Reform-mode carried-payload launch-arithmetic audit (single
	 clone: one launch per trip delivers the trip's own words).  */
      if (reform_mode
	  && payload_contains_carried_p (info, seq.clones.front ())
	  && !reform_carried_launch_arithmetic_ok (info, seq))
	continue;

      basic_block preheader = dedicated_loop_preheader (loop);
      if (!preheader)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Not hoisting: loop has no dedicated preheader\n");
	  continue;
	}
      if (bitmap_bit_p (dirty_bbs, preheader->index))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Not hoisting: preheader bb %d may hold open recording"
		     " state\n", preheader->index);
	  continue;
	}
      /* The counted-loop payload is its own single clone; across trips the
         launch is always separated from the next by the loop-control
         delivery, so the contiguous launch run is 1.  */
      peel_plan peel;
      if (!hoist_profitable_p (loop, preheader, info, seq.clones.front (),
			       /*body_rerecords=*/false,
			       /*launch_run=*/1))
	{
	  /* Lane IO: a benefit-refused counted capture may still admit
	     as the exec-while-record first-trip peel (see
	     counted_peel_profitable_p above); every refusal below keeps
	     the plain refusal's bytes.  A reform-mode carried payload
	     refuses the peel by the reform-mode name: the peel RELOCATES one
	     trip's carried executions into the preheader, across the
	     configuration program's placement, and the walk-order proof
	     for that relocation is not in this increment.  */
	  if (!(riscv_tt_opt_counted_capture_peel > 0))
	    continue;
	  if (reform_mode
	      && payload_contains_carried_p (info, seq.clones.front ()))
	    {
	      rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_PEEL_LAUNCH_ARITHMETIC_UNPROVEN,
			   dump_file,
			   "counted-capture-peel refused:"
			   " post-autoincr-window-carried-peel-"
			   "launch-arithmetic-unproven:"
			   " carried payload, first-trip peel would relocate"
			   " carried executions to the preheader (loop %d)\n",
			   loop->num);
	      continue;
	    }
	  if (!counted_peel_profitable_p (loop, preheader, info,
					  seq.clones.front ())
	      || !peel_admissible_p (loop, preheader, info, seq, &peel))
	    continue;
	}

      auto spans = available_replay_spans (replay_spans, persistent_slots);
      auto slot = std::find_if (spans.begin (), spans.end (),
				[&seq] (replay_span span)
				{ return span.end >= seq.length; });
      if (slot == spans.end ())
	continue;

      unsigned length
	= peel.valid
	  ? replace_hoisted_sequence_peel (seq, info, slot->begin, preheader,
					   peel)
	  : replace_hoisted_sequence (seq, info, slot->begin, preheader);
      std::fill (persistent_slots.begin () + slot->begin,
		 persistent_slots.begin () + slot->begin + length, true);
      if (dump_file && peel.valid)
	fprintf (dump_file,
		 "counted-capture-peel admitted: counted-loop bb %d peeled"
		 " exec-while-record (trips %lu -> %lu)\n",
		 bb->index, (unsigned long) peel.trips,
		 (unsigned long) (peel.trips - 1));
      if (dump_file)
	fprintf (dump_file,
		 "Counted-loop replay payload bb %d length %u captured at %u\n",
		 bb->index, length, slot->begin);
    }
}

/* ---- Complete unroll of proven-trip replay-launch loops ----

   After hoisting, a counted loop's body can be reduced to pure replay
   delivery: playback launches of an already-recorded capture plus typed
   Dst-counter steps, with only the induction-variable update and the
   conditional branch as per-trip work.  Driving that loop control through
   the RISC costs two delivered scalar words per trip and separates
   consecutive launches in the final instruction stream.  When the trip
   count is provable (the same rvtt_loop_trips discipline the hoist
   itself uses -- estimated or profile counts refuse), the body replicates
   textually: emit TRIPS copies of the per-trip delivery back to back,
   materialize the counter's proven final value once (later passes delete it
   when dead), and remove the loop control entirely.  This is the
   no-source-pragma counterpart of the accepted replay-aware complete unroll:
   the gimple-side unroll request needs the payload before recording, while
   this shape only exists after replay formation has hoisted the capture.

   Admission is purely structural.  Every non-debug insn in the single-block
   body must be one of:
     (a) a fixed-encoding TTREPLAY playback launch,
     (b) a typed TTINCRWC with constant operands (the per-trip Dst step the
         counted-loop capture leaves explicit; the Dst auto-increment pass
         runs later and sees every launch site with equivalent RWC coverage),
     (c) the loop's single counter-step insn, or
     (d) the final conditional jump.
   Anything else -- another scalar insn, a recording, a non-playback replay
   owner, an asm, a call, memory, USE/CLOBBER markers -- refuses and leaves
   the loop byte-identical.  No operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.

   Cost: the per-trip benefit is the two removed loop-control words (positive
   for every proven trips >= 2); the only cost is straight-line code size,
   bounded by the cost-table constant XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS on
   the total delivered words of the unrolled run.

   Interaction with the hoist's execution-saturation context term: the
   contiguous launch run this unroll creates exists only in the hoisted
   world, so it never re-prices the hoist decision.  The LAUNCH_RUN input of
   hoist_profitable_p measures sibling launches present in the body
   independently of the hoist under evaluation; a run manufactured by a
   post-hoist delivery optimization is a consequence of the decision, not
   context for it (see rvtt-cost.md).  */

static bool
unroll_launch_loop (class loop *loop, bitmap dirty_bbs)
{
  basic_block header = loop->header;
  if (loop->num_nodes != 1 || loop->header != loop->latch)
    return false;
  if (bitmap_bit_p (dirty_bbs, header->index))
    /* Recording state may be open here (unprovable user epoch).  */
    return false;

  /* Pragma scope.  "#pragma GCC unroll" governs payload duplication: the
     gimple replay-unroll REQUEST defers to it and an annotated payload is
     never replicated.  This unroll is a delivery transformation on the
     residual launch loop the (equally pragma-blind, post-reload) hoist
     leaves behind: the capture stays recorded once and only delivered
     launch words replicate -- the same class of rewrite as the hoist
     itself, which has always fired on annotated loops.  Loop structures
     are rebuilt after reload, so loop->unroll is normally cleared here;
     honor it if it ever survives (a preserved "#pragma GCC unroll 1" must
     keep even its launch loop).  */
  if (loop->unroll)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d carries an explicit user"
		 " unroll request\n", header->index);
      return false;
    }

  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    return false;

  rtx_insn *jump = BB_END (header);
  if (!JUMP_P (jump) || !any_condjump_p (jump) || !onlyjump_p (jump)
      || EDGE_COUNT (header->succs) != 2)
    return false;

  edge e_branch = BRANCH_EDGE (header);
  edge e_fall = FALLTHRU_EDGE (header);
  /* A fallthrough cannot re-enter its own block, so the backedge must be
     the taken branch and the fallthrough the unique exit.  */
  if (e_branch->dest != header || e_fall->dest == header
      || (e_branch->flags & EDGE_ABNORMAL) || (e_fall->flags & EDGE_ABNORMAL))
    return false;

  /* A loop without a playback launch is silently out of scope; refusal
     diagnostics below are only meaningful for launch-carrying bodies.  */
  bool has_playback = false;
  rtx_insn *insn;
  FOR_BB_INSNS (header, insn)
    if (NONDEBUG_INSN_P (insn) && GET_CODE (insn) == INSN
	&& recog_memoized (insn) >= 0
	&& get_attr_type (insn) == TYPE_TENSIX)
      {
	replay_span span;
	if (is_replay_insn (span, insn) == REPLAY_playback)
	  {
	    has_playback = true;
	    break;
	  }
      }
  if (!has_playback)
    return false;

  /* Classify the body.  DELIVERY collects the per-trip delivered words in
     program order; STEP is the single scalar counter update.  */
  std::vector<rtx_insn *> delivery;
  rtx_insn *step = nullptr;
  unsigned launches = 0;
  FOR_BB_INSNS (header, insn)
    {
      if (!NONDEBUG_INSN_P (insn) || insn == jump)
	continue;
      if (CALL_P (insn) || GET_CODE (insn) != INSN)
	return false;
      rtx pattern = PATTERN (insn);
      if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER
	  || asm_noperands (pattern) >= 0)
	return false;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  replay_span span;
	  if (is_replay_insn (span, insn) == REPLAY_playback
	      && fixed_replay_rtx_p (pattern))
	    {
	      ++launches;
	      delivery.push_back (insn);
	      continue;
	    }
	  if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc
	      && fixed_replay_rtx_p (pattern))
	    {
	      delivery.push_back (insn);
	      continue;
	    }
	  if (dump_file)
	    fprintf (dump_file,
		     "Launch-loop unroll refused: bb %d body insn %d is not"
		     " a playback launch or typed Dst step\n",
		     header->index, INSN_UID (insn));
	  return false;
	}
      if (step)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Launch-loop unroll refused: bb %d has a second scalar"
		     " insn %d beyond the counter step\n",
		     header->index, INSN_UID (insn));
	  return false;
	}
      step = insn;
    }
  if (!launches || !step)
    return false;

  uint64_t trips, final_value;
  rtx_insn *counter_step;
  if (!rvtt_loop_trips (loop, preheader, &trips, &counter_step,
				&final_value))
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d trip count is not"
		 " provably constant\n", header->index);
      return false;
    }
  /* The one scalar insn must be exactly the proven counter step; any other
     scalar state would be silently frozen by removing the loop.  */
  if (counter_step != step || trips < 2)
    return false;

  rtx step_set = single_set (step);
  rtx counter = SET_DEST (step_set);
  machine_mode counter_mode = GET_MODE (counter);
  rtx final_rtx = gen_int_mode (final_value, counter_mode);
  /* The counter's proven exit value replaces the removed per-trip updates.
     Post-reload only single-insn constants may be materialized directly.  */
  if (!SMALL_OPERAND (INTVAL (final_rtx)) && !LUI_OPERAND (INTVAL (final_rtx)))
    return false;

  unsigned trip_words = 0;
  for (rtx_insn *d : delivery)
    trip_words += get_attr_length (d) / 4;
  if ((uint64_t) trip_words * trips > XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Launch-loop unroll refused: bb %d unrolled size %lu words"
		 " exceeds %d\n", header->index,
		 (unsigned long) ((uint64_t) trip_words * trips),
		 XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS);
      return false;
    }

  /* ---- Execute-while-recording increment ----

     The hoisted capture in the dedicated preheader records without
     executing, so the first trip's launch re-delivers work the recording
     already streamed through the buffer.  When the structural conditions
     below hold, flip the capture to execute-while-loading and drop the
     first trip's launch: the payload's first execution happens at the
     record itself (the same semantics the planner CC path emits, and the
     in-place replace_sequence has always used).  Conditions, all
     refusing by leaving the plain unroll behavior:
       - not Quasar (cannot exec while capturing there; the same guard
         replace_sequence applies);
       - the first delivered word of the trip is a playback launch of
         exactly the capture's span (payload execution moves from that
         launch site to the record site);
       - the preheder's only Tensix content after the capture is the
         capture's own payload: scalar insns cannot interact with Tensix
         state, so crossing them preserves the payload's CC, Dst, and RWC
         context; any other Tensix word between record and first launch
         refuses.
     The typed Dst auto-increment pass models an executing capture as a
     row of its own (ROW_CAPTURE_EXEC), so its later ownership placement
     sees the record-time execution site like any other row.  */
  rtx_insn *exec_capture = nullptr;
  rtx_insn *exec_payload_end = nullptr;
  bool drop_first_launch = false;
  if (!(riscv_tt_fix_qsr_replay > 0) && riscv_tt_opt_replay_exec_record > 0)
    {
      replay_span lead_span;
      if (is_replay_insn (lead_span, delivery.front ()) == REPLAY_playback)
	{
	  /* Find the capture of this span in the preheader and prove it is
	     the last Tensix content (past its own payload words).  */
	  rtx_insn *pinsn;
	  rtx_insn *cap = nullptr;
	  unsigned payload_left = 0;
	  bool clean = true;
	  FOR_BB_INSNS (preheader, pinsn)
	    {
	      if (!NONDEBUG_INSN_P (pinsn) || GET_CODE (pinsn) != INSN)
		continue;
	      rtx pat = PATTERN (pinsn);
	      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
		continue;
	      if (asm_noperands (pat) >= 0 || recog_memoized (pinsn) < 0
		  || get_attr_type (pinsn) != TYPE_TENSIX)
		{
		  if (asm_noperands (pat) >= 0)
		    {
		      /* An empty-template asm (the compiler memory-barrier
		         idiom) emits nothing; every real asm is an
		         unclassified word.  Position decides: the
		         transformation moves the
		         payload's execution from the first launch back to
		         the record, so only words BETWEEN the record and
		         that launch are crossed -- a word before the
		         record is outside the motion window, exactly like
		         the typed Tensix words the branch below already
		         admits (the LLK per-tile wrapper's raw
		         TTI_STALLWAIT word sits there on every
		         llk_math_eltwise_sfpu_common.h tile loop).  A raw
		         word inside the payload span corrupts the typed
		         slot count, and one after the payload is crossed
		         by the motion: both keep refusing.  */
		      const char *tmpl
			= GET_CODE (pat) == ASM_OPERANDS
			  ? ASM_OPERANDS_TEMPLATE (pat)
			  : GET_CODE (pat) == PARALLEL
			      && GET_CODE (XVECEXP (pat, 0, 0)) == ASM_OPERANDS
			    ? ASM_OPERANDS_TEMPLATE (XVECEXP (pat, 0, 0))
			    : nullptr;
		      while (tmpl && (*tmpl == ' ' || *tmpl == '\t'))
			++tmpl;
		      if ((!tmpl || *tmpl) && (cap || payload_left))
			{
			  clean = false;
			  if (dump_file)
			    fprintf (dump_file,
				     "Exec-while-record refused: preheader"
				     " insn %d is a non-empty asm after"
				     " the record\n", INSN_UID (pinsn));
			  break;
			}
		    }
		  /* scalar work / empty barrier / pre-record raw word */
		  continue;
		}
	      if (payload_left)
		{
		  unsigned words = get_attr_length (pinsn) / 4;
		  if (words > payload_left)
		    {
		      clean = false;
		      break;
		    }
		  payload_left -= words;
		  if (!payload_left)
		    exec_payload_end = pinsn;
		  continue;
		}
	      replay_span span;
	      auto type = is_replay_insn (span, pinsn);
	      /* is_replay_insn's raw span carries {begin = start slot,
	         end = length}.  */
	      if (type == REPLAY_fixed_capture && !cap
		  && span.begin == lead_span.begin
		  && span.end == lead_span.end
		  && XVECEXP (pat, 0, 6) == const0_rtx)
		{
		  cap = pinsn;
		  payload_left = span.end;
		  continue;
		}
	      if (!cap)
		/* Tensix work BEFORE the record retires before it and is
		   unaffected by executing the payload at the record point.  */
		continue;
	      if (!get_attr_length (pinsn))
		/* Zero-length architectural markers deliver no word.  */
		continue;
	      /* A Tensix word between the capture's payload and the first
	         launch: refuse (the payload's execution would cross it).  */
	      clean = false;
	      if (dump_file)
		fprintf (dump_file,
			 "Exec-while-record refused: preheader insn %d is a"
			 " Tensix word between record and first launch\n",
			 INSN_UID (pinsn));
	      break;
	    }
	  if (clean && cap && !payload_left)
	    {
	      exec_capture = cap;
	      drop_first_launch = true;
	    }
	  else if (dump_file && clean)
	    fprintf (dump_file,
		     "Exec-while-record refused: no matching record-only"
		     " capture terminates the dedicated preheader\n");
	}
      else if (dump_file)
	fprintf (dump_file,
		 "Exec-while-record refused: the trip's first delivered"
		 " word is not the playback launch\n");
    }

  /* Commit.  Replicate the per-trip delivery TRIPS-1 further times in body
     order (the scalar counter step commutes with every delivered word: it
     touches only the counter register), set the counter's proven final
     value, and remove the loop control.  The loop structure loses its
     backedge; record the pending fixup before mutating the CFG.  */
  loops_state_set (LOOPS_NEED_FIXUP);

  if (drop_first_launch)
    {
      /* Flip the record to execute-while-loading, drop the first trip's
         now-redundant launch, and emit the WHOLE unrolled delivery run in
         the preheader directly after the executed payload: trip 1's
         remaining typed Dst steps followed by trips 2..N.  Everything
         crossed is scalar work (proven above), and the later Dst
         auto-increment ownership pass then sees the record-time execution
         row and every launch row in ONE block -- the same shared-placement
         shape an in-place capture produces.  */
      XVECEXP (PATTERN (exec_capture), 0, 6) = const1_rtx;
      INSN_CODE (exec_capture) = -1;
      rtx_insn *anchor = exec_payload_end;
      for (rtx_insn *d : delivery)
	if (d != delivery.front ())
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      for (uint64_t trip = 1; trip != trips; ++trip)
	for (rtx_insn *d : delivery)
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      emit_insn_after (gen_rtx_SET (counter, final_rtx), anchor);
      for (rtx_insn *d : delivery)
	delete_insn (d);
      if (dump_file)
	fprintf (dump_file,
		 "Exec-while-record: capture insn %d executes trip 1;"
		 " first launch removed; %lu-trip delivery run emitted at"
		 " the record\n", INSN_UID (exec_capture),
		 (unsigned long) trips);
    }
  else
    {
      rtx_insn *anchor = delivery.back ();
      for (uint64_t trip = 1; trip != trips; ++trip)
	for (rtx_insn *d : delivery)
	  anchor = emit_insn_after (copy_insn (PATTERN (d)), anchor);
      emit_insn_after (gen_rtx_SET (counter, final_rtx), anchor);
    }

  delete_insn (step);
  remove_edge (e_branch);
  delete_insn (jump);

  if (dump_file)
    fprintf (dump_file,
	     "Unrolled launch loop bb %d: %lu trips x %u delivered words,"
	     " backedge removed\n", header->index, (unsigned long) trips,
	     trip_words);
  return true;
}

/* Apply the complete launch-loop unroll (unroll_launch_loop and the
   design comment above it) to every loop header block of CFN.
   DIRTY_BBS excludes blocks where user recording state may be open.  */

void
unroll_launch_loops (function *cfn, bitmap dirty_bbs)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      class loop *loop = bb->loop_father;
      if (!loop || loop->num == 0 || loop->header != bb)
	continue;
      unroll_launch_loop (loop, dirty_bbs);
    }
}

/* ---- Launch conversion of isomorphic instruction runs ----

   After formation, a payload recorded in the replay buffer may have further
   executions that were not textually identical to the recorded sequence --
   typically the final copy of a completely unrolled counted loop, whose
   separate register allocation chose different temporaries (and may clobber
   registers the recorded rows preserve).  When such a run is
   effect-isomorphic to a payload under a register value map, executing it as
   one more launch is equivalent provided every register whose final contents
   differ between the two worlds is dead after the run.

   Matching is purely structural: identical instruction codes and
   non-register operands in lockstep, with register operands related by an
   evolving value map (a use of a run-local definition must correspond to the
   matched definition; a live-in use must be the identical hard register).
   No operation names, opcode calendars, immediate fingerprints, or raw
   encodings participate in any decision.

   Refusals, all leaving the function byte-identical:
     - any lockstep mismatch (code, immediate, structure, or value map);
     - a register of a differing definition pair live after the run;
     - a run whose trailing Dst-advance context differs from the uniform
       trailing context of the payload's other execution sites: the typed Dst
       auto-increment ownership pass runs later and must see every execution
       site with equivalent RWC coverage, so a conversion may not create the
       only uncovered site;
     - payloads whose recorded contents, buffer span, or execution sites are
       ambiguous, and runs not dominated by their recording.  */

struct conv_capture
{
  rtx_insn *insn = nullptr;
  basic_block bb = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;
  bool valid = true;
  std::vector<rtx_insn *> members; /* slot-occupying payload insns, in order */
  rtx_insn *shadow_end = nullptr;  /* last member */
  unsigned sites = 0;
  int trailing = -2;               /* uniform site context; -2 unset, -1 none */
};

struct conv_launch
{
  rtx_insn *insn;
  unsigned begin;
  unsigned len;
  conv_capture *payload = nullptr;
};

/* A typed TTINCRWC advancing only Dst by a constant stride, mirroring the
   Dst auto-increment pass's row separator test.  */

static bool
conv_pure_dst_increment_p (rtx_insn *insn, HOST_WIDE_INT *stride)
{
  if (GET_CODE (insn) != INSN
      || recog_memoized (insn) != CODE_FOR_rvtt_ttincrwc)
    return false;
  rtx pattern = PATTERN (insn);
  rtx cr = XVECEXP (pattern, 0, 0);
  rtx d = XVECEXP (pattern, 0, 1);
  rtx b = XVECEXP (pattern, 0, 2);
  rtx a = XVECEXP (pattern, 0, 3);
  if (!CONST_INT_P (cr) || !CONST_INT_P (d) || !CONST_INT_P (b)
      || !CONST_INT_P (a))
    return false;
  if (INTVAL (cr) != 0 || INTVAL (b) != 0 || INTVAL (a) != 0)
    return false;
  *stride = INTVAL (d);
  return *stride > 0 && *stride <= 15;
}

/* The trailing Dst-advance context of an execution site whose last issued
   instruction is LAST: the stride of an immediately following pure typed Dst
   TTINCRWC, or -1.  */

static int
conv_trailing_context (rtx_insn *last)
{
  basic_block bb = BLOCK_FOR_INSN (last);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (rtx_insn *cur = NEXT_INSN (last); cur && cur != end;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      HOST_WIDE_INT stride;
      if (conv_pure_dst_increment_p (cur, &stride))
	return (int) stride;
      return -1;
    }
  return -1;
}

/* An insn eligible to appear in a matched run: replay-safe, slot-occupying,
   fixed-encoding Tensix work.  */

bool
conv_run_insn_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX
      || get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || !get_attr_length (insn))
    return false;
  return fixed_replay_rtx_p (PATTERN (insn));
}

/* Is hard register REGNO consumed by a real instruction on any path from
   after FROM (in BB) before being fully redefined?  Mirrors the load-macro
   pass's lifetime test (reg_referenced_p before reg_set_p, a later full
   definition ends the old value's lifetime).  The exit block is not a
   consumer: SFPU register state is not an implicit cross-function
   interface in this programming model -- an explicit hand-off is an
   ordinary instruction definition or use (sfpwritelreg/sfpreadlreg), the
   ABI's blanket call-saved marking otherwise has no residual-contents
   contract (kernels clobber the file without saving; accepted-risk
   precedent from the region-scoped ownership review), and calls are
   conservatively treated as consumers.  Declared asm register operands
   appear as pattern references and are honored; undeclared asm dependence
   on residual register contents has no contract.  */

/* Architectural LREG interface markers (the typed variable-LREG read and
   write patterns) observe or pin a specific physical register out of band:
   a read is modeled as a fresh definition whose value is architecturally
   the register's current contents.  Any such marker is conservatively a
   consumer.  */

static bool
conv_mentions_varlreg_p (const_rtx x)
{
  if (GET_CODE (x) == UNSPEC_VOLATILE && XINT (x, 1) == UNSPECV_SFPVARLREG)
    return true;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
    if (fmt[i] == 'e')
      {
	if (conv_mentions_varlreg_p (XEXP (x, i)))
	  return true;
      }
    else if (fmt[i] == 'E')
      for (int j = XVECLEN (x, i); j--;)
	if (conv_mentions_varlreg_p (XVECEXP (x, i, j)))
	  return true;
  return false;
}

/* The lifetime test described above (the comment preceding
   conv_mentions_varlreg_p): is hard register REGNO's value at FROM
   possibly consumed later?  Scans the remainder of BB after FROM, then
   every successor path, returning true at the first call, variable-LREG
   interface marker, or pattern reference to REGNO, and pruning a path
   at a full redefinition.  The exit block is never a consumer.  */

bool
conv_reg_consumed_after_p (unsigned regno, rtx_insn *from, basic_block bb)
{
  rtx reg = regno_reg_rtx[regno];
  rtx_insn *stop = NEXT_INSN (BB_END (bb));
  for (rtx_insn *cur = NEXT_INSN (from); cur && cur != stop;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      if (CALL_P (cur) || conv_mentions_varlreg_p (PATTERN (cur))
	  || reg_referenced_p (reg, PATTERN (cur)))
	return true;
      if (reg_set_p (reg, cur))
	return false;
    }

  auto_bitmap visited;
  std::vector<basic_block> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    work.push_back (e->dest);
  while (!work.empty ())
    {
      basic_block cur_bb = work.back ();
      work.pop_back ();
      if (cur_bb == EXIT_BLOCK_PTR_FOR_FN (cfun)
	  || !bitmap_set_bit (visited, cur_bb->index))
	continue;
      bool killed = false;
      rtx_insn *insn;
      FOR_BB_INSNS (cur_bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn) || conv_mentions_varlreg_p (PATTERN (insn))
	      || reg_referenced_p (reg, PATTERN (insn)))
	    return true;
	  if (reg_set_p (reg, insn))
	    {
	      killed = true;
	      break;
	    }
	}
      if (!killed)
	FOR_EACH_EDGE (e, ei, cur_bb->succs)
	  work.push_back (e->dest);
    }
  return false;
}

/* Structural isomorphism of one payload/run instruction pair under the
   evolving value map.  DEFINED_P/DEFINED_R track registers defined so far in
   the payload and run; P2R/R2P is the current correspondence; PAIRS collects
   every definition pair for the liveness proof.  */

struct conv_map
{
  std::map<unsigned, unsigned> p2r, r2p;
  std::vector<std::pair<unsigned, unsigned>> pairs;
  std::map<unsigned, bool> defined_p, defined_r;
};

/* Recursive structural comparison of payload rtx A against run rtx B.
   IN_DEF marks definition context (SET_DEST, CLOBBER).  SFPU register
   pairs met in definition context are deferred into PENDING_DEFS --
   conv_match_insn commits them into MAP only after the whole insn
   matches; register uses must agree with MAP's current correspondence,
   and a live-in use (no definition seen yet) must be the identical hard
   register, unshadowed by a run-local definition.  Everything else --
   codes, modes, constants, unspec numbers, vector shapes -- must match
   exactly; SCRATCH matches SCRATCH.  */

static bool
conv_match_rtx (rtx a, rtx b, bool in_def, conv_map &map,
		std::vector<std::pair<unsigned, unsigned>> &pending_defs)
{
  if (GET_CODE (a) != GET_CODE (b) || GET_MODE (a) != GET_MODE (b))
    return false;
  switch (GET_CODE (a))
    {
    case REG:
      {
	if (REG_NREGS (a) != 1 || REG_NREGS (b) != 1)
	  return false;
	unsigned pa = REGNO (a), rb = REGNO (b);
	if (!SFPU_REG_P (pa) || !SFPU_REG_P (rb))
	  return false;
	if (in_def)
	  {
	    pending_defs.emplace_back (pa, rb);
	    return true;
	  }
	if (map.defined_p.count (pa))
	  return map.p2r.count (pa) && map.p2r[pa] == rb
		 && map.r2p.count (rb) && map.r2p[rb] == pa;
	/* Live-in use: the identical register, not shadowed by a run-local
	   definition.  */
	return pa == rb && !map.defined_r.count (rb);
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return conv_match_rtx (SET_SRC (a), SET_SRC (b), false, map,
			     pending_defs)
	     && conv_match_rtx (SET_DEST (a), SET_DEST (b), true, map,
				pending_defs);

    case CLOBBER:
      return conv_match_rtx (XEXP (a, 0), XEXP (b, 0), true, map,
			     pending_defs);

    case USE:
      return conv_match_rtx (XEXP (a, 0), XEXP (b, 0), false, map,
			     pending_defs);

    case UNSPEC:
    case UNSPEC_VOLATILE:
      if (XINT (a, 1) != XINT (b, 1))
	return false;
      /* FALLTHROUGH */
    case PARALLEL:
      {
	if (XVECLEN (a, 0) != XVECLEN (b, 0))
	  return false;
	for (int ix = 0; ix != XVECLEN (a, 0); ++ix)
	  if (!conv_match_rtx (XVECEXP (a, 0, ix), XVECEXP (b, 0, ix),
			       in_def, map, pending_defs))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Lockstep-match payload insn P against run insn R under MAP: same insn
   code, patterns structurally isomorphic via conv_match_rtx.  Only on a
   whole-insn match are the pair's register definitions committed into
   MAP (p2r/r2p, the defined sets, and the PAIRS list the liveness proof
   consumes) -- definitions take effect after all of the instruction's
   uses.  */

static bool
conv_match_insn (rtx_insn *p, rtx_insn *r, conv_map &map)
{
  if (recog_memoized (p) != recog_memoized (r))
    return false;
  std::vector<std::pair<unsigned, unsigned>> pending_defs;
  if (!conv_match_rtx (PATTERN (p), PATTERN (r), false, map, pending_defs))
    return false;
  /* Definitions take effect after all of the instruction's uses.  */
  for (auto const &def : pending_defs)
    {
      map.p2r[def.first] = def.second;
      map.r2p[def.second] = def.first;
      map.defined_p[def.first] = true;
      map.defined_r[def.second] = true;
      map.pairs.push_back (def);
    }
  return true;
}

/* Driver of the isomorphic-run launch conversion (section comment
   above): structurally rediscover CFN's captures and launches, group
   launch-led instruction runs that are register-isomorphic to a
   recorded capture, and convert each proven run into a launch of that
   capture.  Blocks whose contents changed are marked in DIRTY_BBS for
   the caller's df rescan.  Bails wholesale on any unparseable capture
   ownership.  */

void
convert_isomorphic_runs (function *cfn, bitmap dirty_bbs)
{
  /* Rediscover captures and launches structurally.  */
  std::vector<conv_capture *> captures;
  std::vector<conv_launch> launches;
  std::set<rtx_insn *> shadow;
  bool bail = false;

  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (bail)
	    break;
	  if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN)
	    continue;
	  if (recog_memoized (insn) != CODE_FOR_rvtt_ttreplay_int)
	    continue;
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx begin = XVECEXP (pattern, 0, 5);
	  rtx exec = XVECEXP (pattern, 0, 6);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (begin)
	      || !CONST_INT_P (exec) || !CONST_INT_P (load))
	    {
	      bail = true; /* variable replay: buffer contents unprovable */
	      break;
	    }
	  if (INTVAL (load) == 0)
	    {
	      launches.push_back ({ insn, (unsigned) UINTVAL (begin),
				    (unsigned) UINTVAL (len), nullptr });
	      continue;
	    }
	  conv_capture *cap = new conv_capture;
	  cap->insn = insn;
	  cap->bb = bb;
	  if (bitmap_bit_p (dirty_bbs, bb->index))
	    /* Recording state may already be open around this capture.  */
	    cap->valid = false;
	  cap->begin = UINTVAL (begin);
	  cap->len = UINTVAL (len);
	  cap->exec = INTVAL (exec) != 0;
	  unsigned remaining = cap->len;
	  rtx_insn *cur = insn;
	  rtx_insn *bb_end = NEXT_INSN (BB_END (bb));
	  while (remaining)
	    {
	      cur = NEXT_INSN (cur);
	      if (!cur || cur == bb_end)
		{
		  cap->valid = false;
		  break;
		}
	      if (!NONDEBUG_INSN_P (cur))
		continue;
	      shadow.insert (cur);
	      /* Anything in the shadow that does not occupy a slot (or that
	         this conversion could not itself have matched) makes the
	         recorded contents unsuitable.  */
	      if (!conv_run_insn_p (cur))
		{
		  cap->valid = false;
		  continue;
		}
	      cap->members.push_back (cur);
	      cap->shadow_end = cur;
	      --remaining;
	    }
	  if (cap->valid && cap->members.size () != cap->len)
	    cap->valid = false;
	  captures.push_back (cap);
	  insn = (cur && cur != bb_end) ? cur : BB_END (bb);
	}
      if (bail)
	break;
    }

  if (!bail)
    {
      /* Buffer-span ambiguity: overlapping spans invalidate all parties;
         each launch must resolve to exactly one capture.  */
      auto overlap = [] (unsigned b0, unsigned l0, unsigned b1, unsigned l1)
      { return b0 < b1 + l1 && b1 < b0 + l0; };
      for (conv_capture *cap : captures)
	for (conv_capture *other : captures)
	  if (other != cap
	      && overlap (cap->begin, cap->len, other->begin, other->len))
	    cap->valid = false;
      for (conv_launch &launch : launches)
	{
	  for (conv_capture *cap : captures)
	    if (cap->begin == launch.begin && cap->len == launch.len)
	      launch.payload = launch.payload ? nullptr : cap;
	  for (conv_capture *cap : captures)
	    if (overlap (cap->begin, cap->len, launch.begin, launch.len)
		&& !(cap->begin == launch.begin && cap->len == launch.len))
	      cap->valid = false;
	}

      /* Uniform trailing Dst-advance context across every execution site.  */
      for (conv_capture *cap : captures)
	if (cap->valid)
	  {
	    if (cap->exec)
	      {
		cap->trailing = conv_trailing_context (cap->shadow_end);
		++cap->sites;
	      }
	    for (conv_launch &launch : launches)
	      if (launch.payload == cap)
		{
		  int ctx = conv_trailing_context (launch.insn);
		  if (cap->trailing == -2)
		    cap->trailing = ctx;
		  else if (cap->trailing != ctx)
		    cap->valid = false;
		  ++cap->sites;
		}
	    if (cap->sites == 0)
	      cap->valid = false;
	  }
    }

  bool any_valid = false;
  for (conv_capture *cap : captures)
    any_valid |= cap->valid;

  if (!bail && any_valid)
    {
      calculate_dominance_info (CDI_DOMINATORS);

      FOR_EACH_BB_FN (bb, cfn)
	{
	  if (bitmap_bit_p (dirty_bbs, bb->index))
	    /* Recording state may be open here (unprovable user epoch).  */
	    continue;
	  rtx_insn *stop = NEXT_INSN (BB_END (bb));
	  rtx_insn *insn = BB_HEAD (bb);
	  while (insn && insn != stop)
	    {
	      rtx_insn *next = NEXT_INSN (insn);
	      if (!NONDEBUG_INSN_P (insn) || shadow.count (insn)
		  || !conv_run_insn_p (insn))
		{
		  insn = next;
		  continue;
		}

	      conv_capture *matched = nullptr;
	      conv_map map;
	      rtx_insn *run_last = nullptr;
	      for (conv_capture *cap : captures)
		{
		  if (!cap->valid)
		    continue;
		  /* The recording must reach this run on every path.  */
		  if (cap->bb == bb)
		    {
		      /* Same block: the shadow must precede the run.  */
		      bool before = false;
		      for (rtx_insn *probe = cap->shadow_end; probe;
			   probe = NEXT_INSN (probe))
			{
			  if (probe == insn)
			    {
			      before = true;
			      break;
			    }
			  if (probe == BB_END (bb))
			    break;
			}
		      if (!before)
			continue;
		    }
		  else if (!dominated_by_p (CDI_DOMINATORS, bb, cap->bb))
		    continue;

		  conv_map trial;
		  rtx_insn *cur = insn;
		  rtx_insn *bb_end = NEXT_INSN (BB_END (bb));
		  unsigned matched_len = 0;
		  while (matched_len != cap->len)
		    {
		      if (!cur || cur == bb_end)
			break;
		      if (!NONDEBUG_INSN_P (cur))
			{
			  cur = NEXT_INSN (cur);
			  continue;
			}
		      if (shadow.count (cur) || !conv_run_insn_p (cur)
			  || !conv_match_insn (cap->members[matched_len],
					       cur, trial))
			break;
		      ++matched_len;
		      if (matched_len == cap->len)
			{
			  run_last = cur;
			  break;
			}
		      cur = NEXT_INSN (cur);
		    }
		  if (matched_len == cap->len)
		    {
		      matched = cap;
		      map = trial;
		      break;
		    }
		}

	      if (!matched)
		{
		  insn = next;
		  continue;
		}

	      /* Lane IH (reform_mode): the conversion's delivered words are
		 the RECORDED words under a register-renaming proof, not the
		 site's own words.  For a CARRIED access (positional-state
		 Dst walk) the renamed delivery is unaudited in this
		 increment: refuse by name and keep the run inline.  */
	      if (reform_mode)
		{
		  bool carried = false;
		  for (rtx_insn *m : matched->members)
		    if (rvtt_dst_autoincr_carried_access_p (m))
		      {
			carried = true;
			break;
		      }
		  if (!carried)
		    for (rtx_insn *cur = insn; cur; cur = NEXT_INSN (cur))
		      {
			if (NONDEBUG_INSN_P (cur)
			    && rvtt_dst_autoincr_carried_access_p (cur))
			  {
			    carried = true;
			    break;
			  }
			if (cur == run_last)
			  break;
		      }
		  if (carried)
		    {
		      rvtt_refuse (RVTT_REF_POST_AUTOINCR_WINDOW_CARRIED_ISOMORPHIC_CONVERSION_UNPROVEN, dump_file,
				   "Not converting isomorphic run at insn %d:"
				   " post-autoincr-window-carried-isomorphic-"
				   "conversion-unproven (renamed delivery of a"
				   " carried access)\n", INSN_UID (insn));
		      insn = next;
		      continue;
		    }
		}

	      /* Trailing Dst-advance context parity with the other sites.  */
	      if (conv_trailing_context (run_last) != matched->trailing)
		{
		  if (dump_file)
		    fprintf (dump_file, "Not converting isomorphic run at "
			     "insn %d: trailing Dst-advance context differs "
			     "from the payload's other execution sites\n",
			     INSN_UID (insn));
		  insn = next;
		  continue;
		}

	      /* Every register of a differing definition pair must be dead
	         after the run: the launch clobbers the payload's registers
	         and no longer writes the run's.  */
	      bool live_conflict = false;
	      for (auto const &pair : map.pairs)
		if (pair.first != pair.second
		    && (conv_reg_consumed_after_p (pair.first, run_last, bb)
			|| conv_reg_consumed_after_p (pair.second, run_last,
						      bb)))
		  live_conflict = true;
	      if (live_conflict)
		{
		  if (dump_file)
		    fprintf (dump_file, "Not converting isomorphic run at "
			     "insn %d: renamed register consumed after the "
			     "run\n", INSN_UID (insn));
		  insn = next;
		  continue;
		}

	      /* Convert: one launch replaces the whole run.  */
	      rtx replay = gen_rvtt_ttreplay_int
		(const0_rtx, const0_rtx, const0_rtx, GEN_INT (matched->len),
		 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (matched->begin),
		 const0_rtx, const0_rtx);
	      emit_insn_before (replay, insn);
	      if (dump_file)
		{
		  fprintf (dump_file, "Converted isomorphic run of %u insns "
			   "(bb %d) to launch [%u,+%u); renamed pairs:",
			   matched->len, bb->index, matched->begin,
			   matched->len);
		  bool any = false;
		  for (auto const &pair : map.pairs)
		    if (pair.first != pair.second)
		      {
			fprintf (dump_file, " %u->%u", pair.first,
				 pair.second);
			any = true;
		      }
		  fprintf (dump_file, any ? "\n" : " none\n");
		}
	      rtx_insn *cur = insn;
	      next = NEXT_INSN (run_last);
	      while (cur != run_last)
		{
		  rtx_insn *after = NEXT_INSN (cur);
		  if (NONDEBUG_INSN_P (cur))
		    SET_INSN_DELETED (cur);
		  cur = after;
		}
	      SET_INSN_DELETED (run_last);
	      insn = next;
	    }
	}
      free_dominance_info (CDI_DOMINATORS);
    }

  for (conv_capture *cap : captures)
    delete cap;
}
