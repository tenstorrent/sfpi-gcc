/* Pass to prove Dst auto-increment ownership and absorb per-row TTINCRWC.
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

#define INCLUDE_ALGORITHM
#define INCLUDE_MAP
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
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"

/* Semantic SFPI code performs every logical Dst access through the target
   no-increment address modifier and advances the Dst RWC with an explicit
   typed TTINCRWC after each row.  When a straight-line unrolled row sequence
   or a counted loop advances Dst by a constant stride between rows this way,
   the hardware can instead perform the advance implicitly: an address
   modifier slot programmed with a Dst increment makes the row's final Dst
   access post-increment the RWC, and the explicit per-row TTINCRWC becomes
   dead.

   This pass performs that replacement generically:

     - Rows are discovered from typed-insn dataflow only: a typed TTINCRWC
       with a pure constant Dst stride, whose architecturally preceding Dst
       access is a typed, statically-encoded access through the no-increment
       modifier.  The access may be explicit, or it may be the final access
       of a replay payload executed by a preceding typed TTREPLAY launch or
       an executing capture.  No operation names, coefficient fingerprints,
       raw instruction words, or fixed calendars participate in any decision.

     - Ownership of the Dst address-modifier configuration is proven on every
       path between the configuration point and the last transformed row:
       any call, opaque asm, or unclassified Tensix instruction in that
       region refuses.  For loop-shaped regions the configuration is placed
       in the dedicated preheader and the whole loop body must be proven
       clean, because every iteration is a path from the configuration to a
       row.

     - The modifier slot written is the target's compiler-owned scratch slot
       (see the capability table below).  Its three configuration registers
       are fully programmed (Src, Dst+fidelity, bias) so arbitrary incoming
       state and reset state are equivalent.  The no-increment slot that
       SFPI's programming model contract guarantees is never written, so all
       untransformed accesses keep their architectural no-op behavior.

     - The Dst RWC value is preserved exactly at every program point outside
       the half-open windows (terminator access, removed TTINCRWC]: each
       removed explicit increment is replaced by exactly one implicit
       increment of the same stride at the terminator access, and nothing
       between the two points consumes Dst.  Replay payloads are only
       rewritten when EVERY execution site of the payload is a transformed
       row; otherwise the RWC state at the uncovered site would be
       unrestorably changed and the pass refuses.

     - Profitability compares the configuration cost against the number of
       dynamically removed per-row increments, both in frontend issue slots
       per execution of the configuration program, and the cost model splits
       by PLACEMENT (hardware-measured, bracketed in both directions):

         A PREHEADER program executes once per loop entry inside the same
         pre-steady-state window the once-per-entry drain residual already
         prices (the covered hardware witnesses measured that whole window,
         three-word program included, at ~2 cycles/entry), so its words
         price at their word count, plus the residual through a live
         crossing -- the original pricing.  Charging more there is refuted
         by hardware: the lcm row loop (692423 -> 694979) and the relu hand
         rolled loop (45744 -> 49330) are preheader 8x1 groups measured
         BETTER fired.

         A NON-PREHEADER program re-executes on every execution of its
         region, entered through scalar control that drains the frontend:
         each SETC16 word occupies the audited two-cycle configuration
         issue class (rvtt-cost.md, rvtt_issue_cfg -- the same audited
         resource the distance guard below stands on) while each removed
         TTINCRWC frees one single-cycle slot, and the program pays the
         once-per-entry drain residual (min_config_distance) on every
         execution.  Silicon witness (binopscalar-fresh, pin 35): an
         eight-row straight-line callee re-invoked 512 times per kernel
         measured 21929 vs 21164 cycles (+3.61%, ~1.5 cycles/invocation)
         under the old 3-word-vs-8-rows admission -- the per-execution
         slot pricing refuses it at 8 <= 3*2 + 2.

       Groups sharing a rewritten capture payload are priced as one FAMILY
       (they live or die together under payload coverage; see the
       profitability block).  Loop regions use the same trip-count estimate
       as replay hoisting.  No numeric row thresholds appear anywhere;
       break-evens fall out of the model.

     - Mod-write backedge-crossing price.  The transform replaces an explicit
       TTINCRWC -- an audited latency-0 issue-time RWC counter update
       (rvtt-cost.md row step: [ISA] pure counter update, [SIM] applied at
       issue, [HAND] TTINCRWC->SFPLOAD back-to-back in every hardware-proven
       counted production row) -- with a positional-state side effect of the
       terminator access itself, executed inside the vector unit.  The
       audited latency table DELIBERATELY REFUSES an entry for the
       auto-incrementing access modes (rvtt-cost.md: "positional Dst/RWC
       state"), so the mod-write's retirement distance is an unaudited
       quantity.  Consumers inside a continuous Tensix word stream are
       covered by hand witnesses (production unrolled and replay-windowed
       kernels issue live-modifier stores back to back with dependent
       accesses); a consumer reached ACROSS A LOOP BACKEDGE is not: the
       scalar loop control drains the frontend and the next iteration's
       first Dst access issues onto an empty pipe a few slots after the
       mod-write, inside the unaudited retirement window.  Five whole-ELF
       hardware witnesses now bracket that window from both sides: SKINNY
       5-slot iterations stall 1.38-1.57 cycles per crossing (absint32
       hand 16.950 -> 18.853, unaryshift-fresh semantic 16.962 -> 19.631,
       bitwisenot hand 16.950 -> 18.853), while FAT 10/12-slot iterations
       stall ~0.06 per crossing (threshold-fresh, hardshrink-fresh:
       refusing them measured +26.95/+27.06 against hand-scheduled
       references, the
       counterexample); the same transform in eight-row-per-iteration and
       straight-line bodies stays a measured win.  Fitting
       stall = max(0, W - iteration_slots) gives W ~= 6.4..6.6 from the
       skinny class, <= 10 from the fat class; the audited constant takes
       the conservative 7 (rvtt-cost.md).  Lane EE's whole-row closure
       model (within ~3% on all 14 anatomized cells) independently
       calibrates the word counts: the ~1.3-1.8-cycle measured per-launch
       boundary cost makes the one-slot credit for a launch word in the
       covering walk a conservative floor.

       The pricing term charges each loop-iteration crossing the part of
       the audited drained-frontend retirement window
       (drained_frontend_window, rvtt-cost.md: fit from five whole-ELF
       hardware witnesses bracketing both regimes) that the iteration's OWN
       slot-occupying words do not cover.  Consecutive backedge-crossing
       mod-writes serialize at the window: the covering distance per
       crossing is the whole iteration's issue-slot word count -- Tensix
       words at their audited slot counts, launch words at the one-word
       conservative floor, and SCALAR words included, because they occupy
       the same frontend issue slots that elapse while the mod-write
       retires (the original walk counted only the
       tail-after-terminator and consume-prefix words and ignored the
       iteration body, implying a 64-slot/tile cost on a shape hardware
       measures at ~2 cycles/tile TOTAL).  An audited issue-time RWC
       writer (a surviving explicit TTINCRWC or a typed face advance)
       standing between the last terminator and the backedge re-anchors
       the crossing and clears the charge.  A group whose per-iteration
       rows cannot pay the charge refuses by name
       (mod-write-dominates-rolled-body); otherwise the charge is deducted
       from the dynamically removed increments before the
       configuration-cost comparison, and the once-per-loop-entry drain
       residual (the audited min_config_distance guard the FIRST crossing
       pays before the pipeline reaches steady state -- the ~2-cycle/tile
       total the covered witnesses still measure) is added to the
       configuration-cost side, never per iteration.  No trip-count or
       body-length thresholds appear; the break-even falls out of the
       audited window.

   All refusals leave the function byte-identical.  */

namespace {

/* Target capability table for Dst auto-increment ownership.

   Register addresses are the architectural SETC16 configuration addresses of
   the address-modifier sections, from the per-target configuration space
   (cfg_defines.h): ADDR_MOD_AB_SEC<k>_SrcAIncr_ADDR32,
   ADDR_MOD_DST_SEC<k>_DestIncr_ADDR32 (fidelity shares this register) and
   ADDR_MOD_BIAS_SEC<k>_BiasIncr_ADDR32.

   Blackhole: SFPLOAD/SFPSTORE carry a three-bit modifier field selecting
   physical slot k directly; SrcA/B 12+k, Dst 28+k, bias 47+k.  The SFPI
   contract reserves slot 7 as the architectural no-op modifier and the
   platform reserves slot 6 as the compiler-owned auto-increment scratch slot
   (the same slot the macro formation contract owns).

   Wormhole: the modifier field is two bits wide and the bank base bit
   (ADDR_MOD_SET_Base, thread configuration address 2) selects physical slot
   m or m+4.  The SFPU platform contract pins the base to 1 while compiled
   SFPI code executes: every LLK SFPU entry sequence sets the base before
   the kernel body and clears it after (tt-llk wormhole_b0 cmath_common.h
   set_addr_mod_base/clear_addr_mod_base, invoked from
   _llk_math_eltwise_sfpu_start_/_done_ and peers), and the LLK states the
   aliasing invariant outright ("with addr_mod_base=1, insn ADDR_MOD_3 ->
   phys ADDR_MOD_7 (SFPU invariant, incr=0)", ckernel_sfpu_topk.h).  The
   pass's own premise already stands on that contract: modifier 3 is only
   an architectural no-op through the base-1 alias to physical slot 7 --
   under base 0 it would name physical slot 3, which LLK FPU code programs
   with live increments (llk_math_matmul.h Dst+8/bias+1).  So exactly one
   physical slot is compiler-owned: scratch modifier 2 under base 1 =
   physical slot 6, SrcA/B 19, Dst 29, bias 54.

   The base-0 bank must never be written: physical slot 2 (SrcA/B 11,
   Dst 25, bias 50) is LLK's ADDR_MOD_2, consumed with live strides by the
   base-0 FPU/datacopy path (llk_math_eltwise_unary_datacopy.h MOV_8_ROWS,
   Dst+8).  A historical dual-slot emission that also programmed slot 2
   clobbered that state and corrupted every tile after the first on the
   WH simulator (FINDING-wh-dst-autoincr-fresh-maxmin.md); the failure was
   adjudicated as this miscompile, not a simulator gap.  The SFPI no-op
   modifier is 3.

   QSR has no capability entry and therefore refuses.  */

struct autoincr_slot
{
  unsigned src_reg;
  unsigned dst_reg;   /* Dst increment; fidelity shares the register.  */
  unsigned bias_reg;
};

struct autoincr_caps
{
  bool available;
  unsigned noinc_mode;   /* SFPI no-increment modifier value.  */
  unsigned scratch_mode; /* compiler-owned modifier value to retarget to.  */
  unsigned nslots;       /* physical slots behind scratch_mode.  */
  /* SETC16-to-consume distance guard: the minimum number of slot-occupying
     Tensix instruction words that must issue strictly between the final
     word of the slot program and the first access consuming the scratch
     modifier.  A scratch-mode access applies every field of the modifier
     slot, so the guard is measured from the last configuration word, not
     from the field a particular access appears to need.

     Architectural basis: SETC16 retires through the configuration issue
     class, which the target issue model (rvtt-cost.md, rvtt_issue_cfg)
     and the reference simulator's tensix_rtl_issue_class_for_inst both model as a
     two-cycle resource, one cycle longer than the single-cycle math/SFPU
     classes.  Two intervening issued words therefore guarantee the
     configuration write has retired before the consumer issues in that
     model.  Replay-shaped rows satisfy this structurally (the launch word
     plus the payload prefix precede the terminator access); tight
     explicit-row shapes must either prove the distance by anchoring the
     program earlier or refuse (independent-review carry-forward for
     promoting explicit-row shapes).  */
  unsigned min_config_distance;
  /* Drained-frontend retirement window for the mod-write backedge
     crossing, in frontend issue-slot words (rvtt-cost.md audited entry,
     the covered-crossing fit): the number of issue slots a backedge-crossing
     mod-write needs before the next crossing's consumer may issue
     stall-free.  Fit W ~= 6.4..6.6 from the uncovered witness class
     (absint32-hand 1.38, unaryshift-sem 1.57, bitwisenot-hand 1.38
     cycles/crossing, all 5-slot iterations), bounded <= 10 by the covered
     class (threshold 0.064, hardshrink 0.061 cycles/crossing, 10/12-slot
     iterations); the audited value takes the CONSERVATIVE 7, which
     preserves every witness verdict on both sides.  The Wormhole entry
     carries the Blackhole-fit value as the same-frontend-class
     conservative adoption (no WH hardware witness; larger W only widens
     refusal).  */
  unsigned drained_frontend_window;
  /* Frontend issue-slot occupancy of one SETC16 configuration word: the
     configuration issue class is an audited two-cycle resource
     (rvtt-cost.md rvtt_issue_cfg; the reference simulator's tensix_rtl_issue_class_for_inst
     models the same), one cycle longer than the single-cycle class a
     removed TTINCRWC occupies.  The profitability comparison prices the
     slot program in these units so both sides are frontend issue slots.  */
  unsigned config_issue_slots;
  autoincr_slot slots[2];
  /* Refuse-only watched configuration rows for the cross-call ADDR_MOD
     contract: thread-configuration registers whose rewrite
     would re-target the scratch modifier WITHOUT writing the owned slot
     registers.  Wormhole: the two-bit modifier field reaches physical
     slot 6 only through the ADDR_MOD_SET_Base bank-select bit (thread
     configuration address 2, tt-isa-documentation WormholeB0 RWCs.md);
     a SETC16 flipping it re-aliases the scratch modifier to the base-0
     bank LLK programs with live strides.  Blackhole's three-bit
     modifier field selects the physical slot directly -- no watch
     row.  (ExtraAddrModBit is immaterial under the base-1 platform
     contract: the RWCs.md index OR already takes the +4 bank.)  */
  unsigned n_watch;
  unsigned watch_reg;
};

/* The current target's auto-increment capability entry: modifier
   encodings, owned slot registers, and the audited frontend distance
   and occupancy quantities (WH and BH only; QSR returns available ==
   false, refusing the whole pass).  */

static autoincr_caps
target_autoincr_caps ()
{
  /* Named-member construction (the delivery-cost API): the audited
     frontend quantities come from the rvtt-cost.md define_constants
     they document (single-sourced at build time); the per-target
     modifier encodings and slot registers are the ISA facts the file
     comment above adjudicates.  QSR has no capability entry and
     refuses (every field zero).  */
  autoincr_caps caps = autoincr_caps ();
  if (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH)
    {
      caps.available = true;
      caps.nslots = 1;
      caps.min_config_distance = XTT_AUTOINCR_MIN_CONFIG_DISTANCE;
      caps.drained_frontend_window = XTT_MODWRITE_DRAINED_FRONTEND_WINDOW;
      caps.config_issue_slots = XTT_CONFIG_ISSUE_SLOTS;
      if (TARGET_XTT_TENSIX_BH)
	{
	  caps.noinc_mode = 7;
	  caps.scratch_mode = 6;
	  caps.slots[0].src_reg = 18;
	  caps.slots[0].dst_reg = 34;
	  caps.slots[0].bias_reg = 53;
	}
      else
	{
	  caps.noinc_mode = 3;
	  caps.scratch_mode = 2;
	  caps.slots[0].src_reg = 19;
	  caps.slots[0].dst_reg = 29;
	  caps.slots[0].bias_reg = 54;
	  caps.n_watch = 1;
	  caps.watch_reg = 2;
	}
    }
  return caps;
}

/* Classification of one instruction by architectural effect, derived from
   typed instruction identity and machine attributes only.  */

enum autoincr_class
{
  AIC_NEUTRAL,  /* provably no Dst-RWC or modifier-slot effect */
  AIC_ACCESS,   /* typed Dst access through an address modifier */
  AIC_INCRWC,   /* typed TTINCRWC */
  AIC_RWC_STEP, /* typed pure RWC counter step (Dst face advance): no
		   modifier-slot or LREG effect, but RWC state changes, so
		   it separates rows and never absorbs an increment */
  AIC_REPLAY,   /* typed TTREPLAY capture or launch */
  AIC_FOREIGN,  /* call, opaque asm, or unclassified effect: refuses */
};

struct access_info
{
  unsigned mode = 0;        /* constant address-modifier operand */
  int mode_opno = -1;
  bool retargetable = false; /* statically encoded: operand is authoritative */
};

/* Typed Dst accesses whose address-modifier operand is an explicit RTL
   operand.  The operand indexes come from the machine description.  The
   dynamically-addressed alternatives synthesize their base opcode word at
   expand time, so their modifier operand cannot be retargeted; they remain
   classified accesses (their no-increment behavior is unchanged) but can
   never carry the implicit advance.  */

static bool
classify_access (rtx_insn *insn, int code, access_info *acc)
{
  int mode_opno;
  int static_opno; /* mem_or_0 operand: const0 selects the static form.  */
  switch (code)
    {
    case CODE_FOR_rvtt_sfpstore_int:
      mode_opno = 6;
      static_opno = 0;
      break;
    case CODE_FOR_rvtt_sfpload_lv_int:
      mode_opno = 8;
      static_opno = 1;
      break;
    default:
      return false;
    }
  extract_insn (insn);
  rtx mode = recog_data.operand[mode_opno];
  if (!CONST_INT_P (mode))
    return false;
  acc->mode = UINTVAL (mode);
  acc->mode_opno = mode_opno;
  acc->retargetable = recog_data.operand[static_opno] == const0_rtx;
  return true;
}

/* Classify INSN into the autoincr_class vocabulary, filling *ACC for a
   typed Dst access.  Calls, opaque asm, unrecognized insns, scalar or
   Dst-accessing code with unprovable effects, and Tensix instructions
   without the replay-safe attribute contract all take the refusing
   AIC_FOREIGN default; raw .ttinsn words are admitted only when the
   audited decode proves the pure Dst/RWC counter class.  */

static autoincr_class
classify_insn (rtx_insn *insn, access_info *acc)
{
  if (CALL_P (insn))
    return AIC_FOREIGN;
  if (asm_noperands (PATTERN (insn)) >= 0)
    {
      /* Raw `.ttinsn' constant words: the audited architectural decode
	 (rvtt-raw-boundary.cc) proves the pure Dst/RWC counter class --
	 the same class as the typed face advance below: no
	 modifier-slot or LREG effect, but RWC state changes, so it
	 separates rows and never absorbs an increment.  Every other
	 asm keeps the refusing default.  */
      xtt_rwc_effect_t rwc;
      if (rvtt_raw_pure_dst_rwc (insn, &rwc))
	return AIC_RWC_STEP;
      return AIC_FOREIGN;
    }
  if (JUMP_P (insn))
    /* Branches have no Dst-RWC or configuration effect.  They bound the
       block, so they can only trail a region.  */
    return contains_mem_rtx_p (PATTERN (insn)) ? AIC_FOREIGN : AIC_NEUTRAL;
  if (GET_CODE (insn) != INSN)
    return AIC_FOREIGN;

  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return AIC_NEUTRAL;

  int code = recog_memoized (insn);
  if (code < 0)
    return AIC_FOREIGN;

  if (get_attr_type (insn) != TYPE_TENSIX)
    /* Scalar work cannot touch Tensix state, but a memory access could be a
       synthesized instruction-buffer issue.  */
    return contains_mem_rtx_p (pattern) ? AIC_FOREIGN : AIC_NEUTRAL;

  if (code == CODE_FOR_rvtt_ttincrwc)
    return AIC_INCRWC;
  if (code == CODE_FOR_rvtt_ttdstface_wh_bh)
    /* Typed Dst/RWC face advance: advances RWC counters only; the
       address-modifier configuration slots are untouched by identity of
       the typed pattern.  (Raw `.ttinsn' words of the same architectural
       class are admitted above through the audited field decode.)  */
    return AIC_RWC_STEP;
  if (code == CODE_FOR_rvtt_ttreplay_int)
    return AIC_REPLAY;
  if (classify_access (insn, code, acc))
    return AIC_ACCESS;
  if (code == CODE_FOR_rvtt_sfploaddiscard_int
      || code == CODE_FOR_rvtt_sfploadmacro_int)
    /* Dst accesses whose modifier is baked into an opaque encoding.  */
    return AIC_FOREIGN;

  /* Machine-described replay-safe instructions have no hidden CC, Dst, RWC,
     template, or replay ownership effects (rvtt.md attribute contract), so
     the remaining LREG-only compute is neutral here.  Everything else keeps
     the refusing default.  */
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_SAFE)
    return AIC_NEUTRAL;
  return AIC_FOREIGN;
}

/* A typed TTINCRWC advancing only Dst by a constant stride.  */

static bool
pure_dst_increment_p (rtx_insn *insn, HOST_WIDE_INT *stride)
{
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
  /* The architectural field is four bits; zero advances nothing.  */
  return *stride > 0 && *stride <= 15;
}

/* Replay bookkeeping.  Captures record the instructions in their shadow;
   launches execute the payload recorded for their exact buffer span.  */

struct capture_rec
{
  rtx_insn *insn = nullptr;
  basic_block bb = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;   /* capture also executes the payload */
  bool valid = true;

  std::vector<rtx_insn *> members;
  std::vector<autoincr_class> member_cls;
  std::vector<access_info> member_acc;

  /* Derived payload facts.  */
  bool payload_ok = false;
  rtx_insn *terminator = nullptr;
  access_info terminator_acc;

  unsigned exec_sites = 0;    /* executions: launches + executing captures */
  unsigned covered_sites = 0; /* executions that are candidate rows */
};

struct launch_rec
{
  rtx_insn *insn;
  unsigned begin;
  unsigned len;
  capture_rec *payload; /* resolved capture, or null */
};

/* One linearized block element.  Payload members of a capture are folded
   into the capture's record and do not appear as items.  */

struct bb_item
{
  rtx_insn *insn;
  autoincr_class cls;
  access_info acc;
  capture_rec *cap = nullptr;   /* AIC_REPLAY capture */
  launch_rec *launch = nullptr; /* AIC_REPLAY launch */
};

enum row_kind { ROW_EXPLICIT, ROW_LAUNCH, ROW_CAPTURE_EXEC };

struct candidate
{
  row_kind kind;
  unsigned lead_item;      /* item index where the row's execution starts */
  unsigned incr_item;      /* item index of the typed TTINCRWC */
  rtx_insn *increment;     /* the TTINCRWC insn */
  HOST_WIDE_INT stride;
  rtx_insn *terminator;    /* access insn carrying the implicit advance */
  access_info terminator_acc;
  capture_rec *payload;    /* non-null for ROW_LAUNCH / ROW_CAPTURE_EXEC */
  /* Slot-occupying Tensix words issued strictly between the row's lead
     position and the terminator access, for the distance guard: 0 for an
     explicit row, the launch word plus the payload prefix for replay
     rows.  */
  unsigned consume_prefix = 0;
  bool dropped = false;
};

struct bb_scan
{
  basic_block bb;
  std::vector<bb_item> items;
  std::vector<candidate> candidates;
};

struct function_scan
{
  std::vector<capture_rec *> captures;
  std::vector<launch_rec *> launches;
  std::vector<bb_scan> blocks;
  bool bail = false;
  const char *bail_reason = nullptr;
  /* Replay captures recorded WITHOUT execution (TTREPLAY load=1 exec=0)
     seen anywhere in the function.  Composing the store-side mod-write
     with a no-exec recording window that begins ingesting while a
     group's mod-write is still retiring is hardware-refuted (rvtt-cost.md,
     "no-exec record composition"; see noexec_record_composition_p for
     the audited-window guard and its witnesses).  */
  std::vector<capture_rec *> noexec_captures;
};

/* Load-carrier word counting (riscv_tt_opt_dst_autoincr_load_carrier,
   the load-carrier extension): a canonical single-constant `.ttinsn' asm (the TTI_ macro
   shape the LLK library issues its raw boundary words in) is by
   construction exactly one 32-bit Tensix word in the issue stream, so
   it occupies exactly one replay slot and one frontend issue slot.
   This is a COUNTING fact only, taken from the audited extraction
   (rvtt_raw_ttinsn_word, rvtt-raw-boundary.cc); no classification
   happens here, and the words keep every refusing default elsewhere:
   classify_insn still answers AIC_FOREIGN (or the audited pure-RWC
   class), so raw words never become rewritable payload members,
   gap-legal items, or configuration-window-legal items.

   The prior behavior this knob replaces counted raw words as ZERO
   slots, so a replay recording whose shadow is raw words (every LLK
   datacopy envelope record) overran its block and refused the whole
   function ("replay capture crosses block") -- the adjudicated blocker
   of the load-carrier class (a microbenchmark probe: unit-stride
   `load dst_reg[0]; dst_reg += 1' walks emitted 32 raw TTINCRWC with
   zero capture while the very same rows fire in a record-free
   function).  Knob off preserves that behavior byte-identically.  */

static bool
raw_ttinsn_slot_word_p (rtx_insn *insn)
{
  if (!riscv_tt_opt_dst_autoincr_load_carrier)
    return false;
  uint32_t word;
  return rvtt_raw_ttinsn_word (insn, &word);
}

/* Non-empty Tensix instructions occupy replay slots; everything else is
   transparent to the recording shadow.  Under the load-carrier knob,
   audited raw `.ttinsn' constant words count too (see above).  */

static bool
occupies_replay_slot_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return raw_ttinsn_slot_word_p (insn);
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  return get_attr_type (insn) == TYPE_TENSIX && get_attr_length (insn) != 0;
}

/* Slot-occupying Tensix words issued strictly between a replay row's lead
   position (the launch or executing capture) and the execution of the
   payload terminator: the launch word itself plus the payload prefix.  */

static unsigned
payload_consume_prefix (const capture_rec *cap)
{
  unsigned words = 1;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    {
      if (cap->members[ix] == cap->terminator)
	break;
      if (occupies_replay_slot_p (cap->members[ix]))
	++words;
    }
  return words;
}

/* Vet a capture's payload for carrying the implicit advance: every member
   must be neutral or a statically-encoded no-increment access, and the last
   access is the terminator.  */

static void
vet_payload (capture_rec *cap, const autoincr_caps &caps)
{
  cap->payload_ok = false;
  if (!cap->valid)
    return;
  int last_access = -1;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    switch (cap->member_cls[ix])
      {
      case AIC_NEUTRAL:
	break;
      case AIC_ACCESS:
	if (cap->member_acc[ix].mode != caps.noinc_mode)
	  return;
	last_access = ix;
	break;
      default:
	return;
      }
  if (last_access < 0)
    return;
  if (!cap->member_acc[last_access].retargetable)
    return;
  cap->payload_ok = true;
  cap->terminator = cap->members[last_access];
  cap->terminator_acc = cap->member_acc[last_access];
}

/* Linearize one block: classify instructions, fold capture shadows, resolve
   launches, and record candidate rows.  */

static void
scan_block (function_scan &fn, basic_block bb, const autoincr_caps &caps)
{
  fn.blocks.emplace_back ();
  bb_scan &scan = fn.blocks.back ();
  scan.bb = bb;

  rtx_insn *insn = BB_HEAD (bb);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (; insn != end; insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      access_info acc;
      autoincr_class cls = classify_insn (insn, &acc);

      if (cls == AIC_REPLAY)
	{
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx begin = XVECEXP (pattern, 0, 5);
	  rtx exec = XVECEXP (pattern, 0, 6);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (begin)
	      || !CONST_INT_P (exec) || !CONST_INT_P (load))
	    {
	      /* A variable capture makes buffer contents unprovable.  */
	      fn.bail = true;
	      fn.bail_reason = "variable replay capture";
	      return;
	    }
	  if (INTVAL (load) != 0)
	    {
	      /* Capture: fold the recording shadow.  */
	      capture_rec *cap = new capture_rec;
	      cap->insn = insn;
	      cap->bb = bb;
	      cap->begin = UINTVAL (begin);
	      cap->len = UINTVAL (len);
	      cap->exec = INTVAL (exec) != 0;
	      if (!cap->exec)
		fn.noexec_captures.push_back (cap);
	      unsigned remaining = cap->len;
	      unsigned raw_words = 0;
	      while (remaining)
		{
		  insn = NEXT_INSN (insn);
		  if (!insn || BLOCK_FOR_INSN (insn) != bb)
		    {
		      cap->valid = false;
		      fn.bail = true;
		      fn.bail_reason = "replay capture crosses block";
		      fn.captures.push_back (cap);
		      return;
		    }
		  if (!NONDEBUG_INSN_P (insn))
		    continue;
		  access_info macc;
		  autoincr_class mcls = classify_insn (insn, &macc);
		  cap->members.push_back (insn);
		  cap->member_cls.push_back (mcls);
		  cap->member_acc.push_back (macc);
		  if (raw_ttinsn_slot_word_p (insn))
		    ++raw_words;
		  if (occupies_replay_slot_p (insn))
		    --remaining;
		}
	      if (raw_words && dump_file)
		fprintf (dump_file, "Dst-autoincr: raw-word capture shadow "
			 "counted (%u raw words of %u, bb %d; load-carrier)\n",
			 raw_words, cap->len, bb->index);
	      vet_payload (cap, caps);
	      fn.captures.push_back (cap);
	      bb_item item;
	      item.insn = cap->insn;
	      item.cls = AIC_REPLAY;
	      item.cap = cap;
	      scan.items.push_back (item);
	      continue;
	    }
	  launch_rec *launch = new launch_rec;
	  launch->insn = insn;
	  launch->begin = UINTVAL (begin);
	  launch->len = UINTVAL (len);
	  launch->payload = nullptr;
	  fn.launches.push_back (launch);
	  bb_item item;
	  item.insn = insn;
	  item.cls = AIC_REPLAY;
	  item.launch = launch;
	  scan.items.push_back (item);
	  continue;
	}

      bb_item item;
      item.insn = insn;
      item.cls = cls;
      item.acc = acc;
      scan.items.push_back (item);
    }
}

/* Resolve every launch to the unique capture recording its exact span, and
   invalidate payloads with overlapping-but-different uses.  */

static void
resolve_replay (function_scan &fn)
{
  for (launch_rec *launch : fn.launches)
    {
      capture_rec *found = nullptr;
      for (capture_rec *cap : fn.captures)
	if (cap->begin == launch->begin && cap->len == launch->len)
	  {
	    if (found)
	      {
		found = nullptr; /* ambiguous */
		break;
	      }
	    found = cap;
	  }
      launch->payload = found;
    }

  /* Overlapping spans make buffer contents unprovable for both parties.  */
  auto overlap = [] (unsigned b0, unsigned l0, unsigned b1, unsigned l1)
  { return b0 < b1 + l1 && b1 < b0 + l0; };
  for (capture_rec *cap : fn.captures)
    {
      for (capture_rec *other : fn.captures)
	if (other != cap
	    && overlap (cap->begin, cap->len, other->begin, other->len))
	  cap->payload_ok = false;
      for (launch_rec *launch : fn.launches)
	if (overlap (cap->begin, cap->len, launch->begin, launch->len)
	    && !(launch->begin == cap->begin && launch->len == cap->len))
	  cap->payload_ok = false;
    }

  for (capture_rec *cap : fn.captures)
    {
      cap->exec_sites = cap->exec ? 1 : 0;
      for (launch_rec *launch : fn.launches)
	if (launch->payload == cap)
	  ++cap->exec_sites;
    }
}

/* Find candidate rows in one linearized block.  */

static void
find_candidates (bb_scan &scan, const autoincr_caps &caps)
{
  for (unsigned ix = 0; ix != scan.items.size (); ++ix)
    {
      bb_item &item = scan.items[ix];
      if (item.cls != AIC_INCRWC)
	continue;
      HOST_WIDE_INT stride;
      if (!pure_dst_increment_p (item.insn, &stride))
	continue;

      /* The architecturally preceding execution.  Neutral instructions
	 cannot consume Dst; skip them.  */
      int jx = ix;
      while (--jx >= 0 && scan.items[jx].cls == AIC_NEUTRAL)
	continue;
      if (jx < 0)
	continue;
      bb_item &lead = scan.items[jx];

      candidate cand;
      cand.lead_item = jx;
      cand.incr_item = ix;
      cand.increment = item.insn;
      cand.stride = stride;
      cand.payload = nullptr;

      if (lead.cls == AIC_ACCESS)
	{
	  if (lead.acc.mode != caps.noinc_mode || !lead.acc.retargetable)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_EXPLICIT;
	  cand.terminator = lead.insn;
	  cand.terminator_acc = lead.acc;
	}
      else if (lead.cls == AIC_REPLAY && lead.launch)
	{
	  capture_rec *cap = lead.launch->payload;
	  if (!cap || !cap->payload_ok)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_LAUNCH;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	  cand.consume_prefix = payload_consume_prefix (cap);
	}
      else if (lead.cls == AIC_REPLAY && lead.cap)
	{
	  capture_rec *cap = lead.cap;
	  if (!cap->exec || !cap->payload_ok)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_CAPTURE_EXEC;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	  cand.consume_prefix = payload_consume_prefix (cap);
	}
      else
	{
	  if (dump_file)
	    fprintf (dump_file, "Dst-autoincr: not a candidate row "
		     "(no owned terminator access before increment, "
		     "bb %d)\n", scan.bb->index);
	  continue;
	}

      scan.candidates.push_back (cand);
    }
}

/* Can GAP items sit between the configuration point and a later row without
   breaking ownership or RWC-state equivalence?  Neutral instructions and
   untransformed accesses through modifiers other than the scratch modifier
   qualify: their behavior is bitwise identical in both worlds and they
   cannot write modifier slots.  */

static bool
gap_item_ok (const bb_item &item, const autoincr_caps &caps)
{
  switch (item.cls)
    {
    case AIC_NEUTRAL:
      return true;
    case AIC_ACCESS:
      return item.acc.mode != caps.scratch_mode;
    default:
      return false;
    }
}

/* Configuration-window legality: may ITEM sit between an already-programmed
   slot configuration and a later consuming row without invalidating the
   program?  This is weaker than gap legality: the item only needs to be
   provably unable to write the scratch slot's configuration registers or
   consume the scratch modifier.  A typed TTINCRWC advances RWC counters,
   not address-modifier configuration; replay recordings and launches are
   legal when their payload contents are known and themselves legal.  Any
   call, opaque asm, or unclassified Tensix effect (including any foreign
   TTSETC16) keeps the refusing default.  */

static bool
capture_members_config_ok (const capture_rec *cap, const autoincr_caps &caps)
{
  if (!cap->valid)
    return false;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    switch (cap->member_cls[ix])
      {
      case AIC_NEUTRAL:
      case AIC_INCRWC:
	break;
      case AIC_ACCESS:
	if (cap->member_acc[ix].mode == caps.scratch_mode)
	  return false;
	break;
      default:
	return false;
      }
  return true;
}

/* Configuration-window legality of one scanned ITEM (see the comment
   above capture_members_config_ok): neutral items, RWC counter steps,
   and accesses through modifiers other than CAPS's scratch modifier
   qualify; replay recordings and launches qualify when their payload is
   known and its members do.  Everything else refuses.  */

static bool
config_window_item_ok (const bb_item &item, const autoincr_caps &caps)
{
  switch (item.cls)
    {
    case AIC_NEUTRAL:
    case AIC_INCRWC:
    case AIC_RWC_STEP:
      /* RWC counter steps (per-row increments, the typed face advance)
	 cannot write address-modifier configuration.  They do change RWC
	 state, so they are only window-legal, never gap-legal.  */
      return true;
    case AIC_ACCESS:
      return item.acc.mode != caps.scratch_mode;
    case AIC_REPLAY:
      if (item.cap)
	/* A recording only executes its members when it is an executing
	   capture.  */
	return !item.cap->exec || capture_members_config_ok (item.cap, caps);
      if (item.launch)
	return item.launch->payload
	       && capture_members_config_ok (item.launch->payload, caps);
      return false;
    default:
      return false;
    }
}

/* Slot-occupying Tensix words issued by ITEM (recordings issue their
   members; a launch conservatively counts only its own word).  */

static unsigned
item_issue_words (const bb_item &item)
{
  unsigned words = occupies_replay_slot_p (item.insn) ? 1 : 0;
  if (item.cap)
    for (rtx_insn *member : item.cap->members)
      if (occupies_replay_slot_p (member))
	++words;
  return words;
}

/* An audited issue-time RWC writer (explicit TTINCRWC, typed face
   advance) standing between CAND's terminator and the end of the block
   re-anchors the backedge crossing: it is the last RWC writer the
   backedge sees and its own producer adjacency is in-stream (continuous
   words, hand-witnessed).  */

static bool
crossing_reanchored_p (const bb_scan &scan, const candidate &cand)
{
  for (unsigned ix = cand.incr_item + 1; ix != scan.items.size (); ++ix)
    {
      const bb_item &item = scan.items[ix];
      if (item.cls == AIC_INCRWC || item.cls == AIC_RWC_STEP)
	return true;
    }
  return false;
}

/* Frontend issue-slot words of the scalar INSN, zero for Tensix
   instructions (those are counted by the audited slot-word side) and for
   anything unrecognized (undercounting the covering distance only widens
   the charge -- conservative).  Scalar words occupy the same frontend
   issue slots that elapse while a mod-write retires, so they cover
   crossing distance exactly like Tensix words do (the covered-crossing fit:
   the five-witness fit is over whole-iteration slot counts with scalar
   included).  */

static unsigned
scalar_issue_words (rtx_insn *insn)
{
  if (JUMP_P (insn))
    /* One word: the pass runs before branch shortening, where jump
       lengths are worst-case layout maxima (far-branch expansions), not
       issue counts.  The floor stays conservative -- undercounting the
       covering distance only widens the charge -- and matches the
       witnesses' loop control (one compare-and-branch word).  */
    return 1;
  if (GET_CODE (insn) != INSN)
    return 0;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return 0;
  if (recog_memoized (insn) < 0)
    return 0;
  if (get_attr_type (insn) == TYPE_TENSIX)
    return 0;
  return get_attr_length (insn) / 4;
}

/* Frontend issue-slot cover of one raw INSN: the ONE cover spelling
   (the delivery-cost API) behind both the per-item view below and the
   raw-insn block walks: slot words occupy frontend issue
   slots, and scalar words cover crossing distance exactly like Tensix
   words do (the covered-crossing fit).  */

static unsigned
insn_frontend_cover_words (rtx_insn *insn)
{
  return (occupies_replay_slot_p (insn) ? 1 : 0) + scalar_issue_words (insn);
}

/* Frontend issue-slot words of ITEM: the Tensix slot words (recordings
   issue their members; a launch keeps the audited one-word conservative
   floor of the measured 1.3-1.8-cycle launch boundary) plus the scalar
   words of the item and of any recording's scalar members -- the item
   sum of insn_frontend_cover_words over the item's own insn and any
   recording's members, so the per-item and per-raw-insn views price
   the same cover by construction, not by comment.  */

static unsigned
item_frontend_words (const bb_item &item)
{
  unsigned words = insn_frontend_cover_words (item.insn);
  if (item.cap)
    for (rtx_insn *member : item.cap->members)
      words += insn_frontend_cover_words (member);
  return words;
}

struct group
{
  bb_scan *scan;
  std::vector<unsigned> cand_ix; /* indexes into scan->candidates */
  HOST_WIDE_INT stride;
  bool use_preheader = false;
  basic_block preheader = nullptr;
  HOST_WIDE_INT dynamic_rows = 0; /* estimated removed increments */
  /* Straight-line placement: item index the slot program is emitted
     before.  Defaults to the first row's lead and may be anchored earlier
     to satisfy the distance guard.  */
  unsigned anchor_item = 0;
  /* False when a dominating same-program group's configuration reaches
     this group on every path, so this group emits nothing.  */
  bool emit_config = true;
  /* Straight-line shared-placement set this group belongs to, or -1.
     Profitability is evaluated per set: one program serves all rows.  */
  int shared_set = -1;
  /* Set when no placement satisfying the distance guard exists.  */
  bool guard_refused = false;
  /* Mod-write backedge-crossing charge, issue slots per execution of the
     block (see the file comment).  */
  unsigned crossing_charge = 0;
  /* Set when the group's final terminator carries a live (not
     re-anchored) mod-write across a loop backedge, covered or not: the
     loop entry's first crossing pays the once-per-entry drain residual
     on the configuration-cost side.  */
  bool live_crossing = false;
  /* Set when the group's slot program is provided by the cross-call
     ADDR_MOD contract: the program is hoisted to the proven
     caller's loop entry and the group emits nothing, at zero per-call
     configuration cost.  */
  bool contract = false;
};

/* Mirror of the replay pass's dedicated preheader discovery.  */

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

/* Locate the linearized scan of BB.  */

static bb_scan *
find_scan (function_scan &fn, basic_block bb)
{
  for (bb_scan &scan : fn.blocks)
    if (scan.bb == bb)
      return &scan;
  return nullptr;
}

/* Whole-iteration covering distance for GRP's block, in frontend
   issue-slot words as the transformed loop will issue them: every
   slot-occupying word of the block, less the explicit increment word
   each surviving candidate row's transform deletes.  Consecutive
   backedge-crossing mod-writes serialize at the drained-frontend window,
   so the iteration's own words are the covering distance per crossing.
   Multi-block loop bodies count the candidate's block only: any path
   from the block's terminator back to itself traverses at least the
   block's own words, so this is a sound minimum of the inter-crossing
   distance.  */

static unsigned
iteration_cover_words (const group &grp)
{
  unsigned words = 0;
  for (const bb_item &item : grp.scan->items)
    words += item_frontend_words (item);
  for (const candidate &cand : grp.scan->candidates)
    if (!cand.dropped && words)
      --words;
  return words;
}

/* The mod-write backedge-crossing charge for GRP, in issue slots per
   execution of its block (see the file comment).  Zero when the block is
   not inside a loop, when GRP does not hold the block's final surviving
   candidate (an untransformed later row's explicit increment, or a later
   group's rows, stand between GRP and the backedge), when an audited
   issue-time RWC writer re-anchors the crossing, or when the iteration's
   own slot-occupying words already cover the audited drained-frontend
   window.  Multi-block loop bodies are charged per block-end crossing:
   every scalar redirect between Tensix words is a frontend drain point.
   *LIVE_CROSSING is set whenever a non-re-anchored crossing exists,
   covered or not (the loop entry's first crossing pays the
   once-per-entry drain residual on the configuration-cost side).  */

static unsigned
crossing_penalty (const group &grp, const autoincr_caps &caps,
		  bool *live_crossing)
{
  *live_crossing = false;
  basic_block bb = grp.scan->bb;
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return 0;

  /* The block's final surviving candidate carries the crossing.  */
  int last = -1;
  for (unsigned cx = 0; cx != grp.scan->candidates.size (); ++cx)
    if (!grp.scan->candidates[cx].dropped)
      last = cx;
  if (last < 0
      || std::find (grp.cand_ix.begin (), grp.cand_ix.end (),
		    (unsigned) last) == grp.cand_ix.end ())
    return 0;

  if (crossing_reanchored_p (*grp.scan, grp.scan->candidates[last]))
    return 0;

  *live_crossing = true;
  unsigned cover = iteration_cover_words (grp);
  if (cover >= caps.drained_frontend_window)
    {
      if (dump_file)
	fprintf (dump_file, "Dst-autoincr: mod-write backedge crossing "
		 "covered (rows %u, iteration slot words %u >= drain "
		 "window %u, bb %d)\n", unsigned (grp.cand_ix.size ()),
		 cover, caps.drained_frontend_window, bb->index);
      return 0;
    }
  return caps.drained_frontend_window - cover;
}

/* Silicon-refuted composition guard (rvtt-cost.md, "no-exec record
   composition"): a replay capture recorded WITHOUT
   execution (TTREPLAY load=1 exec=0) may not begin ingesting while a
   mod-write of GRP is still inside its unaudited positional-state
   retirement window.  The device A/B on the lcm-fresh kernel
   hangs Tensix (TENSIX TIMED OUT, reset required) when the record
   re-executes two Tensix words after the previous face group's final
   mod-write store, while every composition whose record is separated
   from the stores by at least the audited drained-frontend window --
   the per-tile LLK wrapper records behind the chunk-boundary
   synchronization (celu/eqz-class ON-set rows), the loop-free preamble
   records (xielu-fresh), records unreachable from any store (gcd/lcm
   run_kernel init) -- passes on hardware.  So the guard prices the SAME
   audited quantity the crossing charge does: the minimum issue-slot
   word distance, over CFG paths, from GRP's block to the capture.
   Unreachable or covered (>= drained_frontend_window) admits; anything
   nearer refuses by name.  BlackholeA0 has no REPLAY functional model
   in tt-isa-documentation to audit anything finer.

   The frontend-word distance is an audited retirement proxy ONLY for
   issue-parity delivery: an explicit row's mod-write store is itself a
   frontend word, so N subsequent frontend words bound the store's
   retirement distance from below (the W_drain fit's witnesses are all
   explicit-row shapes, and the celu/eqz-class chunk-boundary
   compositions -- explicit mod-write rows with a reachable in-loop
   no-exec wrapper record behind >= W_drain words -- are hardware-good
   across many pins).  A REPLAY-DELIVERED row breaks that premise: the
   launch issues ONE frontend word while the expander delivers the
   payload's mod-write asynchronously, so no frontend word count after
   the launch bounds the store's retirement, and admission decays to
   runtime pacing the model cannot see.  The refuting hardware witness:
   a sparse_k_filter Int32/dest-acc composition -- 32-launch group, its
   own no-exec record re-ingesting the mod-write payload one block
   earlier in the tile loop, admitted covered at 20+ frontend words --
   wedges Tensix at runtime trip count 32 and passes at trip 8 on
   BYTE-IDENTICAL code (reproduced twice on device, flush-verified),
   while the reference simulator passes both (frontend/RWC retirement
   timing unmodeled).  So for groups with any replay-delivered row, a
   same-function no-exec capture refuses at ANY distance -- widened
   from forward-reachable to any same-function capture, because the
   Replay Expander buffer persists across function/kernel-invocation
   boundaries (hardware-established persistence model) so a sibling-arm
   capture is armed by a prior caller-loop invocation.  The window rule
   applies to issue-parity (explicit-row) groups only.  */

/* True when any surviving row of GRP delivers its terminator through
   the replay expander (a launch or an executing capture) instead of as
   an inline frontend word.  */

static bool
group_replay_delivered_p (const group &grp)
{
  for (unsigned cx : grp.cand_ix)
    if (grp.scan->candidates[cx].payload)
      return true;
  return false;
}

/* Unpruned successor reachability: can execution starting at FROM's
   exits reach TO?  (FROM itself only counts via a cycle back to it;
   the same-block case is handled separately, fail-closed.)  */

static bool
block_reachable_p (basic_block from, basic_block to)
{
  hash_set<basic_block> seen;
  std::vector<basic_block> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, from->succs)
    work.push_back (e->dest);
  while (!work.empty ())
    {
      basic_block bb = work.back ();
      work.pop_back ();
      if (bb == to)
	return true;
      if (seen.add (bb))
	continue;
      FOR_EACH_EDGE (e, ei, bb->succs)
	work.push_back (e->dest);
    }
  return false;
}

/* Total frontend issue-slot words of BB per FN's scan; an unscanned
   block counts zero (zero cover is the refusing direction).  */

static unsigned
block_frontend_words (const function_scan &fn, basic_block bb)
{
  for (const bb_scan &scan : fn.blocks)
    if (scan.bb == bb)
      {
	unsigned words = 0;
	for (const bb_item &item : scan.items)
	  words += item_frontend_words (item);
	return words;
      }
  return 0; /* Unscanned block: zero cover is the refusing direction.  */
}

/* Frontend issue-slot words issued in CAP's block strictly before the
   capture insn itself, from FN's scan; zero for an unscanned block (the
   refusing direction for the distance search).  */

static unsigned
words_before_capture (const function_scan &fn, const capture_rec *cap)
{
  for (const bb_scan &scan : fn.blocks)
    if (scan.bb == cap->bb)
      {
	unsigned words = 0;
	for (const bb_item &item : scan.items)
	  {
	    if (item.insn == cap->insn)
	      return words;
	    words += item_frontend_words (item);
	  }
	return words;
      }
  return 0;
}

/* The silicon-refuted composition guard: return true (setting *HAZARD
   to the capture and *DETAIL to the refusal text) when a no-exec replay
   capture in FN could begin ingesting while GRP's final mod-write is
   still retiring.  Same-block captures refuse outright; for a
   replay-delivered group any same-function no-exec capture that is
   forward-reachable or does not dominate the group refuses; otherwise a
   minimum frontend-word distance search from the group's block exit,
   pruned at CAPS's drained-frontend window, decides.  */

static bool
noexec_record_composition_p (const function_scan &fn, const group &grp,
			     const autoincr_caps &caps,
			     const capture_rec **hazard,
			     const char **detail)
{
  *hazard = nullptr;
  *detail = "no-exec replay capture within the drained-frontend window "
	    "of the group's stores";
  if (fn.noexec_captures.empty ())
    return false;
  unsigned window = caps.drained_frontend_window;
  bool replay_delivered = group_replay_delivered_p (grp);

  for (capture_rec *cap : fn.noexec_captures)
    {
      /* Same-block capture: intra-block ordering against every row is
	 not modeled; refuse (fail-closed).  */
      if (cap->bb == grp.scan->bb)
	{
	  *hazard = cap;
	  return true;
	}

      /* Replay-delivered rows break the issue-parity premise of the
	 frontend-word distance audit (see the block comment above for
	 the refuting hardware witness): a reachable no-exec
	 capture refuses at any distance.  The per-thread Replay Expander
	 buffer PERSISTS across function and kernel-invocation boundaries
	 (hardware-established persistence model, undocumented in the BH
	 REPLAY ISA text), so the successor-reachability relation is not
	 the true consumer relation.  A capture that DOMINATES the group is
	 its legitimate deliverer -- it executes (records) before every launch
	 in the same invocation, the witnessed-good record-hoist mechanism
	 (dst-autoincr-loop-bh); admit it.  A capture that does NOT dominate
	 the group is a SIBLING-arm (or forward-only) record -- the forward
	 case already refused above; the sibling case is this walk's blind
	 spot (FP delta-audit probe pfj1): a previous caller-loop invocation
	 of this function arms it while the group's mod-write runs on the
	 next, reassembling the exact hardware-refuted trio the intra-function
	 walk cannot see.  For a replay-delivered group, therefore, refuse any
	 same-function no-exec capture that does not dominate the group.  (FP
	 filed this widening as analytically zero-delta on the mapped corpus --
	 witnessed-good celu/eqz records are explicit-row, not replay-
	 delivered; skf converts to exec-record -- and the persistence model
	 is the missing justification to take it.)  */
      if (replay_delivered)
	{
	  /* The legitimate deliverer of a replay-delivered group is a no-exec
	     record that DOMINATES the group and is NOT re-ingested inside a
	     loop the group also lives in -- it records once, before every
	     launch of the same invocation (record-hoist preheader,
	     dst-autoincr-loop-bh).  Two shapes are hazards: (a) the capture
	     is forward-reachable from the group -- an in-loop re-record whose
	     replay-delivered payload retires asynchronously (the wedge
	     witness, block_reachable_p); (b) the capture does NOT
	     dominate the group -- a sibling-arm record armed by a prior
	     caller-loop invocation once the Replay buffer persists
	     (the persistence case).  A dominating, non-reachable capture is the
	     deliverer and admits.  */
	  bool reachable = block_reachable_p (grp.scan->bb, cap->bb);
	  bool cap_dominates_group
	    = dom_info_available_p (CDI_DOMINATORS)
	      && dominated_by_p (CDI_DOMINATORS, grp.scan->bb, cap->bb);
	  if (reachable || !cap_dominates_group)
	    {
	      *hazard = cap;
	      *detail
		= reachable
		    ? "replay-delivered mod-write, no-exec replay capture "
		      "reachable from the group"
		    : "replay-delivered mod-write, no-exec replay capture in "
		      "the same function (persistent replay slot, cross-"
		      "invocation reassembly)";
	      return true;
	    }
	}

      /* Dijkstra-style minimum issue-word distance from the exit of
	 GRP's block to CAP, pruned at WINDOW (any path carrying >=
	 WINDOW words is covered).  The block tail after the group's
	 last store is credited zero -- the refusing direction.  */
      hash_map<basic_block, unsigned> best;
      std::vector<std::pair<unsigned, basic_block>> work;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, grp.scan->bb->succs)
	work.emplace_back (0u, e->dest);
      bool refuse = false;
      while (!work.empty () && !refuse)
	{
	  auto it = std::min_element (work.begin (), work.end ());
	  unsigned cost = it->first;
	  basic_block bb = it->second;
	  work.erase (it);
	  if (cost >= window)
	    continue;
	  unsigned *seen = best.get (bb);
	  if (seen && *seen <= cost)
	    continue;
	  best.put (bb, cost);
	  if (bb == cap->bb)
	    {
	      if (cost + words_before_capture (fn, cap) < window)
		refuse = true;
	      continue;
	    }
	  unsigned out = cost + block_frontend_words (fn, bb);
	  if (out >= window)
	    continue;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    work.emplace_back (out, e->dest);
	}
      if (refuse)
	{
	  *hazard = cap;
	  return true;
	}
    }
  return false;
}

/* Ownership of a dominating placement over LOOP for MEMBERS: every
   instruction of every block of the loop must be a member group's row or
   increment, or configuration-window legal.  Every iteration is a path
   from the preheader program to a row terminator, so the whole body
   participates in the ownership window; any call, opaque asm, or possible
   configuration writer on any path refuses.  */

static bool
loop_config_owned_p (class loop *loop, function_scan &fn,
		     const std::vector<group *> &members,
		     const autoincr_caps &caps)
{
  basic_block *bbs = get_loop_body (loop);
  bool ok = true;
  for (unsigned ix = 0; ok && ix != loop->num_nodes; ++ix)
    {
      bb_scan *scan = find_scan (fn, bbs[ix]);
      if (!scan)
	{
	  ok = false;
	  break;
	}
      std::vector<bool> owned (scan->items.size (), false);
      for (group *grp : members)
	if (grp->scan == scan)
	  for (unsigned cx : grp->cand_ix)
	    {
	      owned[scan->candidates[cx].lead_item] = true;
	      owned[scan->candidates[cx].incr_item] = true;
	    }
      for (unsigned jx = 0; ok && jx != scan->items.size (); ++jx)
	if (!owned[jx] && !config_window_item_ok (scan->items[jx], caps))
	  ok = false;
    }
  free (bbs);
  return ok;
}

/* Distance from a program placed at the head of GRP's block (or in its
   loop preheader) to the group's first consumer, in slot-occupying words
   within the block.  Paths through preceding blocks only add words, so
   this is a sound minimum.  */

static unsigned
block_prefix_distance (const group &grp)
{
  const candidate &first = grp.scan->candidates[grp.cand_ix.front ()];
  unsigned words = first.consume_prefix;
  for (unsigned ix = 0; ix != first.lead_item; ++ix)
    words += item_issue_words (grp.scan->items[ix]);
  return words;
}

/* Enforce the SETC16-to-consume distance guard on GRP's straight-line
   placement: starting from the first row's lead, anchor the program
   earlier over configuration-window-legal items (never before FLOOR, which
   bounds a preceding group's rows) until the guard is met.  Sets
   guard_refused when no legal anchor exists.  */

static void
adjust_anchor_for_guard (group &grp, unsigned floor,
			 const autoincr_caps &caps)
{
  bb_scan &scan = *grp.scan;
  const candidate &first = scan.candidates[grp.cand_ix.front ()];
  unsigned anchor = first.lead_item;
  unsigned dist = first.consume_prefix;
  while (dist < caps.min_config_distance && anchor > floor
	 && config_window_item_ok (scan.items[anchor - 1], caps))
    {
      --anchor;
      dist += item_issue_words (scan.items[anchor]);
    }
  grp.anchor_item = anchor;
  if (dist < caps.min_config_distance)
    {
      grp.guard_refused = true;
      rvtt_refuse (RVTT_REF_CONFIGURATION_TO_CONSUME, dump_file,
		   "Dst-autoincr refusal: configuration-to-consume "
		   "distance %u below guard %u (bb %d)\n", dist,
		   caps.min_config_distance, scan.bb->index);
    }
}

/* Decide configuration placement for the surviving GROUPS.

   The scratch modifier slot is global machine state, so a program placed
   at a dominating point is only valid when no different program can be
   alive on any path through it: dominating placements are attempted only
   when every surviving group in the function requires the identical slot
   program (single stride; the capability table fixes the other fields).

   Placements, in decreasing preference:

     - Loop-dominating: all groups inside one loop with a dedicated
       preheader and a wholly-owned body: the program is emitted once in
       the preheader.  This mirrors the handwritten practice of programming
       an invariant address modifier once at an enclosing scope.

     - Straight-line shared: several groups in one block whose intervening
       items are configuration-window legal share the earliest group's
       program, which dominates the rest of the block.

     - Per-group: the program is emitted immediately before each group's
       first row (anchored earlier only to satisfy the distance guard).

   Failed proofs fall back to the next placement, never to unsoundness; the
   distance guard applies to every placement and refuses the group when it
   cannot be met.  */

static void
place_groups (function_scan &fn, std::vector<group> &groups,
	      const autoincr_caps &caps)
{
  if (groups.empty ())
    return;

  bool single_program = true;
  for (const group &grp : groups)
    if (grp.stride != groups.front ().stride)
      single_program = false;

  for (group &grp : groups)
    {
      grp.use_preheader = false;
      grp.preheader = nullptr;
      grp.emit_config = true;
      grp.shared_set = -1;
      grp.guard_refused = false;
      grp.crossing_charge = 0;
      grp.live_crossing = false;
      grp.contract = false;
      grp.dynamic_rows = grp.cand_ix.size ();
      grp.anchor_item
	= grp.scan->candidates[grp.cand_ix.front ()].lead_item;
    }

  if (single_program)
    {
      /* Loop-dominating placement, per innermost loop hosting groups.
	 Because every surviving group requires the identical program, a
	 member group's own program emitted inside another placement's
	 window rewrites the same values and is harmless; ownership only
	 has to exclude foreign writers.  */
      int next_set = 0;
      std::vector<class loop *> loops;
      for (const group &grp : groups)
	{
	  class loop *loop = grp.scan->bb->loop_father;
	  if (loop && loop->num != 0
	      && std::find (loops.begin (), loops.end (), loop)
		 == loops.end ())
	    loops.push_back (loop);
	}
      for (class loop *loop : loops)
	{
	  std::vector<group *> members;
	  for (group &grp : groups)
	    if (grp.scan->bb->loop_father == loop && !grp.use_preheader)
	      members.push_back (&grp);
	  if (members.empty ())
	    continue;
	  basic_block preheader = dedicated_loop_preheader (loop);
	  if (preheader && !loop_config_owned_p (loop, fn, members, caps))
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: dominating placement "
			 "refused: foreign effect on a path (loop %d)\n",
			 loop->num);
	      preheader = nullptr;
	    }
	  if (!preheader)
	    continue;
	  gcov_type iterations
	    = expected_loop_iterations_unbounded (loop) + 1;
	  if (iterations < 2)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr refusal: unknown trip "
			 "count for loop group (bb %d)\n",
			 members.front ()->scan->bb->index);
	      continue;
	    }
	  unsigned dist = ~0u;
	  for (const group *grp : members)
	    dist = std::min (dist, block_prefix_distance (*grp));
	  if (dist < caps.min_config_distance)
	    {
	      rvtt_refuse (RVTT_REF_CONFIGURATION_TO_CONSUME, dump_file,
			   "Dst-autoincr: dominating placement "
			   "refused: configuration-to-consume distance %u "
			   "below guard %u (loop %d)\n", dist,
			   caps.min_config_distance, loop->num);
	      continue;
	    }
	  bool first = true;
	  for (group *grp : members)
	    {
	      grp->use_preheader = true;
	      grp->preheader = preheader;
	      grp->emit_config = first;
	      grp->shared_set = next_set;
	      grp->dynamic_rows
		= (HOST_WIDE_INT) iterations * grp->cand_ix.size ();
	      first = false;
	    }
	  ++next_set;
	}

      /* Straight-line shared placement for the remaining groups when they
	 all sit in one block.  */
      std::vector<group *> rest;
      for (group &grp : groups)
	if (!grp.use_preheader)
	  rest.push_back (&grp);
      bool same_block = rest.size () > 1;
      for (const group *grp : rest)
	if (grp->scan != rest.front ()->scan)
	  same_block = false;
      if (same_block)
	{
	  bb_scan &scan = *rest.front ()->scan;
	  std::vector<bool> owned (scan.items.size (), false);
	  unsigned first_lead = ~0u, last_incr = 0;
	  for (const group *grp : rest)
	    for (unsigned cx : grp->cand_ix)
	      {
		const candidate &cand = scan.candidates[cx];
		owned[cand.lead_item] = true;
		owned[cand.incr_item] = true;
		first_lead = std::min (first_lead, cand.lead_item);
		last_incr = std::max (last_incr, cand.incr_item);
	      }
	  bool ok = true;
	  for (unsigned ix = first_lead; ok && ix <= last_incr; ++ix)
	    if (!owned[ix] && !config_window_item_ok (scan.items[ix], caps))
	      ok = false;
	  if (ok)
	    {
	      /* The earliest group carries the program for all.  */
	      group *lead = rest.front ();
	      for (group *grp : rest)
		if (grp->cand_ix.front () < lead->cand_ix.front ())
		  lead = grp;
	      adjust_anchor_for_guard (*lead, 0, caps);
	      if (!lead->guard_refused)
		{
		  for (group *grp : rest)
		    {
		      grp->shared_set = next_set;
		      grp->emit_config = grp == lead;
		    }
		  return;
		}
	      lead->guard_refused = false;
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: shared placement refused "
			 "by the distance guard (bb %d)\n", scan.bb->index);
	    }
	  else if (dump_file)
	    fprintf (dump_file, "Dst-autoincr: shared placement refused: "
		     "foreign effect between groups (bb %d)\n",
		     scan.bb->index);
	}
    }

  /* Per-group placement with the distance guard for everything not yet
     placed.  The anchor may move earlier only over
     configuration-window-legal items and never across a preceding group's
     rows: a different program must not enter a window that is still
     consuming.  */
  for (group &grp : groups)
    {
      if (grp.use_preheader || grp.shared_set >= 0)
	continue;
      unsigned floor = 0;
      for (const group &other : groups)
	if (other.scan == grp.scan && &other != &grp
	    && grp.scan->candidates[other.cand_ix.back ()].incr_item
	       < grp.scan->candidates[grp.cand_ix.front ()].lead_item)
	  floor = std::max (floor,
			    grp.scan->candidates[other.cand_ix.back ()]
			      .incr_item + 1);
      adjust_anchor_for_guard (grp, floor, caps);
    }
}

/* Emit the owned scratch-slot programming: every consumed field of every
   physical slot behind the scratch modifier, so reset state and arbitrary
   incoming state are equivalent.  */

static void
emit_owned_config (const autoincr_caps &caps, HOST_WIDE_INT stride,
		   rtx_insn *before, rtx_insn *after)
{
  start_sequence ();
  for (unsigned sx = 0; sx != caps.nslots; ++sx)
    {
      const autoincr_slot &slot = caps.slots[sx];
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.src_reg),
					const0_rtx));
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.dst_reg),
					GEN_INT (stride)));
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.bias_reg),
					const0_rtx));
    }
  rtx_insn *seq = get_insns ();
  end_sequence ();
  if (before)
    emit_insn_before (seq, before);
  else
    emit_insn_after (seq, after);
}

/* Rewrite one proven group GRP: emit the owned scratch-slot program at
   its placement (loop preheader or in-block anchor) unless a dominating
   same-program group or the cross-call contract already provides it,
   retarget every row's terminator access to the scratch modifier per
   CAPS, and delete the rows' explicit TTINCRWC increments.  */

static void
transform_group (const group &grp, const autoincr_caps &caps)
{
  bb_scan &scan = *grp.scan;

  /* Configuration placement.  A group whose slot program is provided by a
     dominating same-program group emits nothing.  */
  if (grp.emit_config)
    {
      if (grp.use_preheader)
	{
	  rtx_insn *end = BB_END (grp.preheader);
	  if (end && JUMP_P (end))
	    emit_owned_config (caps, grp.stride, end, nullptr);
	  else
	    emit_owned_config (caps, grp.stride, nullptr, end);
	}
      else
	emit_owned_config (caps, grp.stride,
			   scan.items[grp.anchor_item].insn, nullptr);
    }

  /* Retarget each terminator once and delete the explicit increments.  */
  for (unsigned cx : grp.cand_ix)
    {
      candidate &cand = scan.candidates[cx];
      extract_insn (cand.terminator);
      rtx *loc = recog_data.operand_loc[cand.terminator_acc.mode_opno];
      if (UINTVAL (*loc) != caps.scratch_mode)
	{
	  bool ok = validate_change (cand.terminator, loc,
				     GEN_INT (caps.scratch_mode), false);
	  gcc_assert (ok);
	}
      delete_insn (cand.increment);
    }

  if (dump_file)
    {
      if (grp.emit_config)
	fprintf (dump_file,
		 "Dst-autoincr group: bb %d rows %u stride "
		 HOST_WIDE_INT_PRINT_DEC " config %u words%s\n",
		 scan.bb->index, unsigned (grp.cand_ix.size ()),
		 grp.stride, caps.nslots * 3,
		 grp.use_preheader ? " (preheader)" : "");
      else
	fprintf (dump_file,
		 "Dst-autoincr group: bb %d rows %u stride "
		 HOST_WIDE_INT_PRINT_DEC " %s%s\n",
		 scan.bb->index, unsigned (grp.cand_ix.size ()),
		 grp.stride,
		 grp.contract ? "crosscall contract config" : "shared config",
		 grp.use_preheader ? " (preheader)" : "");
    }
}

/* Cross-call ADDR_MOD contract (flag-gated by
   -mtt-tensix-optimize-crosscall-addrmod): a straight-line callee whose
   groups ALL refuse by the per-execution configuration pricing may
   instead have its slot program hoisted, once, into the proven caller's
   loop entry (gimple-rvtt-crosscall.cc, rvtt_crosscall_addrmod_hoist:
   the init-hoist machinery with the residency walk) --
   the hand kernel's once-per-kernel ADDR_MOD discipline.  The groups
   then fire with the program omitted entirely: the hoisted program is
   preheader-class (the placement split -- it executes once per
   caller-loop entry inside the entry window the drain residual already
   prices), so the per-call configuration cost is ZERO.

   Callee-side admission, every condition fail-closed (a failure keeps
   today's unprofitable refusal byte-identically):

     - ALL surviving groups refused by profitability, non-preheader,
       unshared, single stride, explicit rows only (a fired sibling or a
       second stride would leave a per-call slot program in the callee
       that clobbers the contract between calls; replay-delivered rows
       keep the issue-parity scope bound);

     - WHOLE-CALLEE slot-clobber census: every instruction of every
       block is a contract row/increment or configuration-window legal
       (config_window_item_ok) -- this covers the callee TAIL too, where
       a foreign word after the last row would clobber the slot for the
       NEXT call (the ISA adjudication: only same-thread SETC16 words
       can write the ThreadConfig ADDR_MOD rows; anything unaudited
       refuses);

     - entry distance: the words ahead of each group's first consumer
       satisfy the SETC16-to-consume guard from the function entry (the
       hoisted program retires at latest at the caller's loop entry,
       strictly earlier than any callee word -- counting only the
       callee-local prefix is the conservative floor);

     - call-boundary crossing charge: the block-final live mod-write's
       consumer is the NEXT invocation's first Dst access, reached
       through frontend-draining scalar return/call control -- the same
       audited drained-frontend window the loop-backedge pricing
       charges.  Cover counts only the callee's own words after the
       final increment (caller-side words credited zero, the refusing
       direction); total rows must exceed the summed charge or the
       contract refuses by name (mod-write-dominates-crosscall-body).

   The caller-side proofs (epoch scan at every lifted level, MOP
   template audit, watched bank-select row, preheader occupancy) live in
   the service.  On success every group fires with emit_config clear; on
   any refusal the pricing verdicts stand untouched.  */

static void
attempt_addrmod_contract (function_scan &fn, std::vector<group> &groups,
			  const autoincr_caps &caps,
			  std::vector<bool> *refuse)
{
  const char *why = nullptr;
  HOST_WIDE_INT total_rows = 0;
  for (unsigned gx = 0; !why && gx != groups.size (); ++gx)
    {
      group &grp = groups[gx];
      if (grp.use_preheader || grp.shared_set >= 0)
	why = "preheader-placement";
      else if (grp.stride != groups.front ().stride)
	why = "stride-plural";
      else
	for (unsigned cx : grp.cand_ix)
	  if (grp.scan->candidates[cx].payload)
	    {
	      why = "replay-delivered-row";
	      break;
	    }
      total_rows += grp.cand_ix.size ();
    }

  /* Whole-callee slot-clobber census.  */
  if (!why)
    for (bb_scan &scan : fn.blocks)
      {
	std::vector<bool> owned (scan.items.size (), false);
	for (group &grp : groups)
	  if (grp.scan == &scan)
	    for (unsigned cx : grp.cand_ix)
	      {
		owned[scan.candidates[cx].lead_item] = true;
		owned[scan.candidates[cx].incr_item] = true;
	      }
	for (unsigned jx = 0; !why && jx != scan.items.size (); ++jx)
	  if (!owned[jx] && !config_window_item_ok (scan.items[jx], caps))
	    why = "callee-slot-clobber";
	if (why)
	  break;
      }

  /* Entry distance guard (callee-local conservative floor).  */
  if (!why)
    for (group &grp : groups)
      if (block_prefix_distance (grp) < caps.min_config_distance)
	{
	  why = "entry-distance";
	  break;
	}

  /* Call-boundary crossing charge.  */
  unsigned boundary_charge = 0;
  if (!why)
    for (bb_scan &scan : fn.blocks)
      {
	int last = -1;
	for (unsigned cx = 0; cx != scan.candidates.size (); ++cx)
	  if (!scan.candidates[cx].dropped)
	    last = cx;
	if (last < 0)
	  continue;
	const candidate &cand = scan.candidates[last];
	if (crossing_reanchored_p (scan, cand))
	  continue;
	unsigned cover = 0;
	for (unsigned ix = cand.incr_item + 1; ix != scan.items.size ();
	     ++ix)
	  cover += item_frontend_words (scan.items[ix]);
	if (cover < caps.drained_frontend_window)
	  boundary_charge += caps.drained_frontend_window - cover;
      }
  if (!why && total_rows <= (HOST_WIDE_INT) boundary_charge)
    {
      if (dump_file)
	fprintf (dump_file, "Dst-autoincr refusal: "
		 "mod-write-dominates-crosscall-body (rows "
		 HOST_WIDE_INT_PRINT_DEC ", uncovered boundary slots %u)\n",
		 total_rows, boundary_charge);
      return;
    }
  if (why)
    {
      rvtt_refuse (RVTT_REF_CROSSCALL_ADDRMOD_UNPROVEN, dump_file,
		   "Dst-autoincr refusal: "
		   "crosscall-addrmod-unproven (%s)\n", why);
      return;
    }

  rvtt_addrmod_hoist_program prog;
  memset (&prog, 0, sizeof (prog));
  for (unsigned sx = 0; sx != caps.nslots; ++sx)
    {
      const autoincr_slot &slot = caps.slots[sx];
      prog.setc16[prog.n_setc16].reg = slot.src_reg;
      prog.setc16[prog.n_setc16++].value = 0;
      prog.setc16[prog.n_setc16].reg = slot.dst_reg;
      prog.setc16[prog.n_setc16++].value = (unsigned) groups.front ().stride;
      prog.setc16[prog.n_setc16].reg = slot.bias_reg;
      prog.setc16[prog.n_setc16++].value = 0;
    }
  prog.n_watch = caps.n_watch;
  if (caps.n_watch)
    prog.watch[0] = caps.watch_reg;

  const char *res = rvtt_crosscall_addrmod_hoist (cfun, &prog);
  if (res)
    {
      rvtt_refuse (RVTT_REF_CROSSCALL_ADDRMOD_UNPROVEN, dump_file,
		   "Dst-autoincr refusal: "
		   "crosscall-addrmod-unproven (%s)\n", res);
      return;
    }

  for (unsigned gx = 0; gx != groups.size (); ++gx)
    {
      (*refuse)[gx] = false;
      groups[gx].emit_config = false;
      groups[gx].contract = true;
    }
  if (dump_file)
    fprintf (dump_file, "Dst-autoincr crosscall-addrmod contract: rows "
	     HOST_WIDE_INT_PRINT_DEC " stride " HOST_WIDE_INT_PRINT_DEC
	     " boundary charge %u, program hoisted to caller entry "
	     "(lifted %u levels)\n", total_rows, groups.front ().stride,
	     boundary_charge, prog.lift_levels);
}

/* Pass body over CFN.  Scan every block, refuse whole on a pre-existing
   scratch-modifier access, resolve replay captures/launches, then
   iterate to a fixed point: form maximal same-stride groups of
   candidate rows, drop groups refused by the no-exec record-composition
   guard, place slot programs (dominating/shared/per-group under the
   distance guard), price the mod-write backedge crossing, and apply the
   configuration-cost profitability test; a dropped group's candidates
   restart the iteration.  When all survivors refused on pricing, the
   cross-call ADDR_MOD contract may still admit them.  Finally rewrite
   each surviving group (see transform_group).  */

static void
transform (function *cfn)
{
  autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available)
    return;

  function_scan fn;
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      scan_block (fn, bb, caps);
      if (fn.bail)
	break;
    }

  bool preexisting_scratch = false;
  if (!fn.bail)
    {
      /* A pre-existing access through the scratch modifier means foreign
	 code observes the slot: refuse the whole function.  */
      auto check_scratch = [&] (const access_info &acc, autoincr_class cls)
      {
	if (cls == AIC_ACCESS && acc.mode == caps.scratch_mode)
	  preexisting_scratch = true;
      };
      for (bb_scan &scan : fn.blocks)
	for (bb_item &item : scan.items)
	  check_scratch (item.acc, item.cls);
      for (capture_rec *cap : fn.captures)
	for (unsigned ix = 0; ix != cap->members.size (); ++ix)
	  check_scratch (cap->member_acc[ix], cap->member_cls[ix]);
      if (preexisting_scratch && dump_file)
	fprintf (dump_file, "Dst-autoincr refusal: pre-existing access "
		 "through the scratch modifier\n");
    }

  if (!fn.bail && !preexisting_scratch)
    {
      resolve_replay (fn);
      for (bb_scan &scan : fn.blocks)
	find_candidates (scan, caps);

      /* Iterate group formation, profitability, and payload-coverage
	 filtering to a fixed point: dropping a payload's candidates can
	 split neighboring groups.  */
      std::vector<group> groups;
      bool changed = true;
      while (changed)
	{
	  changed = false;
	  groups.clear ();

	  for (bb_scan &scan : fn.blocks)
	    {
	      group current;
	      current.scan = &scan;
	      auto close_group = [&] ()
	      {
		if (current.cand_ix.empty ())
		  return;
		groups.push_back (current);
		current.cand_ix.clear ();
	      };

	      unsigned prev_incr_item = 0;
	      for (unsigned cx = 0; cx != scan.candidates.size (); ++cx)
		{
		  candidate &cand = scan.candidates[cx];
		  if (cand.dropped)
		    {
		      close_group ();
		      continue;
		    }
		  if (!current.cand_ix.empty ())
		    {
		      bool clean = true;
		      for (unsigned ix = prev_incr_item + 1;
			   ix < cand.lead_item; ++ix)
			if (!gap_item_ok (scan.items[ix], caps))
			  {
			    clean = false;
			    if (dump_file
				&& scan.items[ix].cls == AIC_FOREIGN)
			      fprintf (dump_file, "Dst-autoincr refusal: "
				       "foreign effect breaks ownership "
				       "(bb %d)\n", scan.bb->index);
			    break;
			  }
		      if (clean && cand.stride != current.stride)
			{
			  clean = false;
			  if (dump_file)
			    fprintf (dump_file, "Dst-autoincr refusal: "
				     "stride mismatch between rows "
				     "(bb %d)\n", scan.bb->index);
			}
		      if (!clean)
			close_group ();
		    }
		  if (current.cand_ix.empty ())
		    current.stride = cand.stride;
		  current.cand_ix.push_back (cx);
		  prev_incr_item = cand.incr_item;
		}
	      close_group ();
	    }

	  /* Silicon-refuted no-exec record composition (see
	     noexec_record_composition_p): groups whose mod-write can
	     still be retiring when a no-exec recording window begins
	     ingesting refuse by name before any placement or pricing.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      const capture_rec *hazard;
	      const char *detail;
	      if (noexec_record_composition_p (fn, grp, caps, &hazard,
					       &detail))
		{
		  rvtt_refuse
		    (RVTT_REF_MOD_WRITE_NOEXEC_RECORD_COMPOSITION_UNAUDITED,
		     dump_file,
		     "Dst-autoincr refusal: "
		     "mod-write-noexec-record-composition-"
		     "unaudited (%s, bb %d, capture bb %d)\n",
		     detail, grp.scan->bb->index,
		     hazard->bb->index);
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		++it;
	    }
	  if (changed)
	    continue;

	  /* Placement: dominating/shared first, then per-group, all under
	     the distance guard.  */
	  place_groups (fn, groups, caps);

	  /* Distance-guard refusals leave the group untransformed.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      if (grp.guard_refused)
		{
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		++it;
	    }
	  if (changed)
	    continue;

	  /* Mod-write backedge-crossing price (see the file comment): the
	     block's final implicit advance inside a loop is charged the
	     uncovered part of the audited positional-state retirement
	     guard, per iteration.  Rows that cannot pay refuse by name;
	     survivors carry the charge into the configuration-cost
	     comparison below.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      grp.crossing_charge
		= crossing_penalty (grp, caps, &grp.live_crossing);
	      if ((HOST_WIDE_INT) grp.crossing_charge
		  >= (HOST_WIDE_INT) grp.cand_ix.size ())
		{
		  rvtt_refuse (RVTT_REF_MOD_WRITE_DOMINATES_ROLLED_BODY,
			       dump_file,
			       "Dst-autoincr refusal: "
			       "mod-write-dominates-rolled-body (rows %u, "
			       "uncovered crossing slots %u, bb %d)\n",
			       unsigned (grp.cand_ix.size ()),
			       grp.crossing_charge, grp.scan->bb->index);
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		{
		  if (grp.crossing_charge && dump_file)
		    fprintf (dump_file, "Dst-autoincr: mod-write backedge "
			     "crossing priced (rows %u, uncovered crossing "
			     "slots %u, bb %d)\n",
			     unsigned (grp.cand_ix.size ()),
			     grp.crossing_charge, grp.scan->bb->index);
		  ++it;
		}
	    }
	  if (changed)
	    continue;

	  /* Profitability: configuration cost against dynamically removed
	     increments, less the per-iteration mod-write crossing charge,
	     both in frontend issue slots per execution of the program.  A
	     shared program's cost is paid once for every group it serves.
	     The cost model splits by PLACEMENT (see group_cost below): a
	     preheader program keeps the original word pricing plus the
	     live-crossing entry residual (the covered hardware witnesses
	     measured the whole entry window, program included, at ~2
	     cycles); a non-preheader program re-executes per region
	     execution and pays the audited two-cycle configuration issue
	     class per SETC16 word plus the once-per-entry drain residual
	     (the binopscalar hardware witness, both directions
	     bracketed on lcm/relu).  */
	  auto priced_rows = [] (const group &grp)
	  {
	    HOST_WIDE_INT iter_mult
	      = grp.dynamic_rows / (HOST_WIDE_INT) grp.cand_ix.size ();
	    return grp.dynamic_rows
		   - (HOST_WIDE_INT) grp.crossing_charge * iter_mult;
	  };
	  std::map<int, HOST_WIDE_INT> shared_rows;
	  for (const group &grp : groups)
	    if (grp.shared_set >= 0)
	      shared_rows[grp.shared_set] += priced_rows (grp);

	  /* Placement decides the program's slot pricing (hardware-measured,
	     both directions):
	     - A PREHEADER program executes once per loop entry inside the
	       same pre-steady-state window the once-per-entry drain
	       residual already prices: the covered fat witnesses
	       measured ~2 cycles per entry TOTAL with the three-word
	       program in the preheader, so its words price at their word
	       count and the residual is charged only through a live
	       crossing, as before.  Charging the configuration class
	       occupancy there double-counts the entry window: doing so
	       refused the lcm row loop (692423 -> 694979, +0.37%) and the
	       relu hand rolled loop (45744 -> 49330, +7.8%) -- both
	       preheader 8x1 groups hardware-measured BETTER fired.
	     - A NON-PREHEADER program re-executes on every execution of
	       its region: each SETC16 occupies the audited two-cycle
	       configuration issue class (rvtt_issue_cfg) against the
	       removed increments' single-cycle slots, plus the
	       once-per-entry drain residual -- the scalar entry control
	       that reaches it drains the frontend the configuration then
	       consumes (binopscalar witness: 21929 vs 21164, ~1.5
	       cycles/invocation at the old admission).  */
	  auto group_cost = [&] (const group &grp)
	  {
	    HOST_WIDE_INT cost = (HOST_WIDE_INT) caps.nslots * 3;
	    if (!grp.use_preheader)
	      cost = cost * caps.config_issue_slots
		     + caps.min_config_distance;
	    else if (grp.live_crossing)
	      cost += caps.min_config_distance;
	    return cost;
	  };

	  /* Payload families: groups whose candidates share a rewritten
	     capture payload live or die TOGETHER -- the payload-coverage
	     rule below forces all-or-nothing on every execution site of
	     the capture -- so the two real alternatives are the WHOLE
	     family transformed or the whole family kept, and profitability
	     must compare exactly those: family removed = sum of the member
	     groups' priced rows; family cost = one program cost per
	     distinct emitted program (a shared placement's program counted
	     once for its set).  Pricing an orphan member in isolation
	     would let a small split-off group's refusal poison a paying
	     sibling stream through payload coverage (the rdiv
	     hand kernel's 32-launch stream splits 8+24; 32 removed vs two
	     programs' 16 slots pays, the 8-row orphan alone does not).
	     Union-find over shared captures.  */
	  /* Union-find by group INDEX (verdicts are computed before any
	     erasure mutates the vector).  */
	  std::vector<int> fam (groups.size (), -1);
	  auto fam_find = [&] (int g)
	  {
	    while (fam[g] >= 0 && fam[g] != g)
	      g = fam[g];
	    return g;
	  };
	  {
	    std::map<const capture_rec *, int> cap_owner;
	    for (unsigned gx = 0; gx != groups.size (); ++gx)
	      for (unsigned cx : groups[gx].cand_ix)
		if (capture_rec *cap
		      = groups[gx].scan->candidates[cx].payload)
		  {
		    if (fam[gx] < 0)
		      fam[gx] = gx;
		    int me = fam_find (gx);
		    auto it = cap_owner.find (cap);
		    if (it == cap_owner.end ())
		      cap_owner[cap] = me;
		    else
		      {
			int other = fam_find (it->second);
			if (other != me)
			  fam[other] = me;
		      }
		  }
	  }
	  std::map<int, HOST_WIDE_INT> fam_removed, fam_cost;
	  std::map<int, std::vector<int>> fam_shared_seen;
	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    if (fam[gx] >= 0)
	      {
		int root = fam_find (gx);
		group &grp = groups[gx];
		fam_removed[root] += priced_rows (grp);
		bool count_cost = true;
		if (grp.shared_set >= 0)
		  {
		    std::vector<int> &seen = fam_shared_seen[root];
		    if (std::find (seen.begin (), seen.end (),
				   grp.shared_set) != seen.end ())
		      count_cost = false;
		    else
		      seen.push_back (grp.shared_set);
		  }
		if (count_cost)
		  fam_cost[root] += group_cost (grp);
	      }

	  /* Verdicts first (indices stay stable), erasure second.  */
	  std::vector<bool> refuse (groups.size (), false);
	  std::vector<HOST_WIDE_INT> verdict_cost (groups.size (), 0);
	  std::vector<HOST_WIDE_INT> verdict_removed (groups.size (), 0);
	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    {
	      group &grp = groups[gx];
	      bool in_family = fam[gx] >= 0;
	      HOST_WIDE_INT cost, removed;
	      if (in_family)
		{
		  int root = fam_find (gx);
		  cost = fam_cost[root];
		  removed = fam_removed[root];
		}
	      else
		{
		  cost = group_cost (grp);
		  removed = grp.shared_set >= 0
		    ? shared_rows[grp.shared_set] : priced_rows (grp);
		}
	      verdict_cost[gx] = cost;
	      verdict_removed[gx] = removed;
	      if (removed <= cost)
		refuse[gx] = true;
	    }

	  /* Cross-call ADDR_MOD contract (flag-gated): a function
	     whose groups ALL refuse by this pricing may fire them instead
	     at zero per-call configuration cost under the hoisted-program
	     contract; every unproven link keeps the verdicts untouched
	     (see attempt_addrmod_contract).  A function with any firing
	     group is out of scope: its per-call slot program would clobber
	     the contract between calls.  */
	  if (riscv_tt_opt_crosscall_addrmod && !groups.empty ())
	    {
	      bool all_refused = true;
	      for (unsigned gx = 0; gx != groups.size (); ++gx)
		all_refused &= refuse[gx] ? true : false;
	      if (all_refused)
		attempt_addrmod_contract (fn, groups, caps, &refuse);
	    }

	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    if (refuse[gx] && dump_file)
	      fprintf (dump_file,
		       "Dst-autoincr refusal: unprofitable %s "
		       "(config+entry slots " HOST_WIDE_INT_PRINT_DEC
		       " >= removed " HOST_WIDE_INT_PRINT_DEC
		       ", bb %d)\n",
		       fam[gx] >= 0 ? "payload family" : "group",
		       verdict_cost[gx], verdict_removed[gx],
		       groups[gx].scan->bb->index);
	  {
	    std::vector<group> kept;
	    for (unsigned gx = 0; gx != groups.size (); ++gx)
	      if (refuse[gx])
		{
		  for (unsigned cx : groups[gx].cand_ix)
		    groups[gx].scan->candidates[cx].dropped = true;
		  changed = true;
		}
	      else
		kept.push_back (groups[gx]);
	    groups.swap (kept);
	  }

	  /* Payload coverage: every execution site of a rewritten payload
	     must be a surviving row, or the uncovered site's RWC state
	     changes unrestorably.  */
	  for (capture_rec *cap : fn.captures)
	    cap->covered_sites = 0;
	  for (const group &grp : groups)
	    for (unsigned cx : grp.cand_ix)
	      if (capture_rec *cap = grp.scan->candidates[cx].payload)
		++cap->covered_sites;
	  for (capture_rec *cap : fn.captures)
	    if (cap->covered_sites != 0
		&& cap->covered_sites != cap->exec_sites)
	      {
		if (dump_file)
		  fprintf (dump_file, "Dst-autoincr refusal: payload "
			   "execution site without matching increment "
			   "(live-out RWC state)\n");
		for (bb_scan &scan : fn.blocks)
		  for (candidate &cand : scan.candidates)
		    if (cand.payload == cap && !cand.dropped)
		      {
			cand.dropped = true;
			changed = true;
		      }
	      }

	  if (changed)
	    continue;

	  for (const group &grp : groups)
	    transform_group (grp, caps);
	}
    }
  else if (fn.bail && dump_file)
    rvtt_refuse_by_name (fn.bail_reason, dump_file,
			 "Dst-autoincr refusal: %s\n", fn.bail_reason);

  for (capture_rec *cap : fn.captures)
    delete cap;
  for (launch_rec *launch : fn.launches)
    delete launch;
}

const pass_data pass_data_rvtt_dst_autoincr =
{
  RTL_PASS, /* type */
  "rvtt_dst_autoincr", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_dst_autoincr : public rtl_opt_pass
{
public:
  pass_rvtt_dst_autoincr (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_dst_autoincr, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_dst_autoincr > 0;
  }

  virtual unsigned execute (function *fn) override
  {
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    transform (fn);
    loop_optimizer_finalize ();
    free_dominance_info (CDI_DOMINATORS);
    return 0;
  }
}; /* class pass_rvtt_dst_autoincr */

} /* anon namespace */

/* Exported single source of the audited W_drain value (rvtt-protos.h;
   see rtl-rvtt-replay.cc): the replay former's no-exec record placement
   obligation audits the same hardware-refuted composition this file's
   group guard does, so both must price the same audited quantity.  */

unsigned
rvtt_modwrite_drained_frontend_window (void)
{
  return target_autoincr_caps ().drained_frontend_window;
}

/* Lane FZ (record-hoist x mod-write downstream-fallback pricing;
   rvtt-cost.md "RECORD-HOIST x MOD-WRITE COMPOSITION" entry).

   Would-be EXPLICIT candidate row, by forward folded scan of BB --
   scan_block's own item shape without the function context: capture
   recording shadows are folded (members are not items), and a typed
   pure-Dst increment is a candidate exactly when its architecturally
   preceding non-neutral ITEM is a retargetable no-increment explicit
   Dst access (find_candidates' ROW_EXPLICIT arm).  Replay-row leads
   (launch / executing capture) are NOT candidates here: their
   candidacy needs the vetted payload terminator (payload_ok), which
   needs whole-function launch resolution -- and their formed groups
   refuse ANY same-function no-exec capture under the expander
   persistence clause regardless of distance, so mirroring them buys
   no byte outcome the group guard does not already own (documented
   scope bound; the sdpa pack TUs are the witnessed shape: launch-led
   increments that find_candidates rejects as "no owned terminator
   access", where a coarse replay-lead arm over-refused real hoists).
   BEFORE_INSN bounds the scan exclusively; null scans the whole
   block.  A non-constant capture stops the scan (no-candidate
   direction: this pricing may only refuse a HOIST; group-guard
   soundness is unconditional either way).  */

static bool
block_has_explicit_candidate_increment_p (basic_block bb,
					  const autoincr_caps &caps,
					  rtx_insn *before_insn)
{
  bool lead_is_access = false;
  access_info lead_acc;
  rtx_insn *insn = BB_HEAD (bb);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (; insn && insn != end; insn = NEXT_INSN (insn))
    {
      if (insn == before_insn)
	break;
      if (!NONDEBUG_INSN_P (insn))
	continue;
      access_info acc;
      autoincr_class cls = classify_insn (insn, &acc);
      if (cls == AIC_NEUTRAL)
	continue;
      if (cls == AIC_REPLAY)
	{
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (load))
	    return false; /* variable capture: stop, no-candidate side */
	  if (INTVAL (load) != 0)
	    {
	      /* Capture: fold the recording shadow like scan_block.  */
	      unsigned remaining = UINTVAL (len);
	      while (remaining && insn)
		{
		  insn = NEXT_INSN (insn);
		  if (!insn || insn == end || insn == before_insn)
		    return false;
		  if (!NONDEBUG_INSN_P (insn))
		    continue;
		  if (occupies_replay_slot_p (insn))
		    --remaining;
		}
	    }
	  lead_is_access = false; /* replay row lead: not mirrored */
	  continue;
	}
      if (cls == AIC_INCRWC)
	{
	  HOST_WIDE_INT stride;
	  if (pure_dst_increment_p (insn, &stride) && lead_is_access
	      && lead_acc.mode == caps.noinc_mode && lead_acc.retargetable)
	    return true;
	  lead_is_access = false;
	  continue;
	}
      if (cls == AIC_ACCESS)
	{
	  lead_is_access = true;
	  lead_acc = acc;
	  continue;
	}
      lead_is_access = false; /* AIC_RWC_STEP / AIC_FOREIGN */
    }
  return false;
}

/* The raw-insn block walk prices blocks identically to the scan's
   item view BY CONSTRUCTION: insn_frontend_cover_words (defined with
   item_frontend_words above -- the one cover spelling) is
   the per-insn primitive both views sum.  */

static unsigned
block_frontend_cover_words (basic_block bb)
{
  unsigned words = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    if (NONDEBUG_INSN_P (insn))
      words += insn_frontend_cover_words (insn);
  return words;
}

/* Exported to the replay former (rvtt-protos.h): would a
   NO-EXEC replay capture hoisted into PREHEADER (at the replay pass's
   anchor rule: before a trailing jump, else at block end) lie within
   the audited drained-frontend window of a WOULD-BE mod-write row --
   a candidate this pass's find_candidates would transform?  If so this
   pass's group guard (noexec_record_composition_p) is certain to
   REFUSE the group formed from that candidate, the transformed stores
   fall back to their explicit-increment form, and the record-hoist's
   streams-identical pricing premise is void (the hoisted and unhoisted
   worlds then EXECUTE different word streams).  Distance semantics
   mirror the guard exactly: candidate-block tail credited zero, the
   capture block's prefix before the insertion point, intermediate
   blocks at their full frontend issue-word cover; a path reaching the
   function entry, or accumulating >= the window, is separated.  A true
   return is a PRICING refusal only (the hoist keeps today's reviewed
   bytes); group-guard soundness is unconditional either way.  */

bool
rvtt_dst_autoincr_hoist_capture_composition_p (basic_block preheader,
					       unsigned *dist)
{
  *dist = 0;
  const autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available || !caps.drained_frontend_window)
    return true; /* no audited window: every distance refuses */
  unsigned window = caps.drained_frontend_window;

  /* Prefix cover inside the preheader itself.  A candidate increment in
     the capture's own block refuses fail-closed (the group guard's
     same-block rule).  */
  rtx_insn *anchor = BB_END (preheader);
  rtx_insn *stop = (anchor && JUMP_P (anchor)) ? anchor : nullptr;
  if (block_has_explicit_candidate_increment_p (preheader, caps, stop))
    return true;
  unsigned cover = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (preheader, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (stop && insn == stop)
	break; /* the capture lands before a trailing jump */
      cover += insn_frontend_cover_words (insn);
    }
  if (cover >= window)
    return false;

  /* Backward min-distance walk over predecessors, pruned at WINDOW: the
     group guard's forward Dijkstra reversed.  */
  hash_map<basic_block, unsigned> best;
  std::vector<std::pair<unsigned, basic_block>> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, preheader->preds)
    work.emplace_back (cover, e->src);
  while (!work.empty ())
    {
      auto it = std::min_element (work.begin (), work.end ());
      unsigned cost = it->first;
      basic_block bb = it->second;
      work.erase (it);
      if (cost >= window || bb == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      unsigned *seen = best.get (bb);
      if (seen && *seen <= cost)
	continue;
      best.put (bb, cost);
      if (block_has_explicit_candidate_increment_p (bb, caps, nullptr))
	{
	  *dist = cost;
	  return true;
	}
      unsigned out = cost + block_frontend_cover_words (bb);
      if (out >= window)
	continue;
      FOR_EACH_EDGE (e, ei, bb->preds)
	work.emplace_back (out, e->src);
    }
  return false;
}

/* Exported to the post-auto-increment window re-formation (rvtt-protos.h;
   the reform mode): is INSN a typed Dst access this pass has retargeted to the
   compiler-owned auto-increment scratch modifier -- a CARRIED access
   whose every execution advances the Dst RWC through the owned ADDR_MOD
   program?  Classification is this pass's own classify_access over the
   static modifier operand.  Only this pass writes the scratch modifier
   (the platform contract the capability table documents: SFPI code uses
   the no-increment modifier, the scratch slot is compiler-owned), so a
   static modifier operand equal to the scratch mode is authoritative.
   Dynamically-addressed alternatives return false: they cannot have been
   retargeted (classify_access marks them non-retargetable and the pass
   never rewrites them).  */

bool
rvtt_dst_autoincr_carried_access_p (rtx_insn *insn)
{
  const autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available)
    return false;
  if (GET_CODE (insn) != INSN)
    return false;
  int code = recog_memoized (insn);
  if (code < 0)
    return false;
  access_info acc;
  if (!classify_access (insn, code, &acc))
    return false;
  return acc.retargetable && acc.mode == caps.scratch_mode;
}

/* Instantiate the Dst auto-increment pass for CTXT; rvtt-passes.def
   places it after postreload (after replay formation, before MOP
   formation), and it gates on the Tensix extension plus
   -mtt-tensix-optimize-dst-autoincr.  */

rtl_opt_pass *
make_pass_rvtt_dst_autoincr (gcc::context *ctxt)
{
  return new pass_rvtt_dst_autoincr (ctxt);
}
