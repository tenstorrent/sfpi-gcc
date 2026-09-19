/* Internal interface between the two halves of the Tensix Dst
   auto-increment pass.

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

#ifndef GCC_RTL_RVTT_DST_AUTOINCR_INT_H
#define GCC_RTL_RVTT_DST_AUTOINCR_INT_H

/* A named namespace, not the global one.  These types were file-local
   before the split (inside the anonymous namespace of
   rtl-rvtt-dst-autoincr.cc); putting them at global scope would collide
   with the unrelated `candidate' in gimple-rvtt-prgm-fuse.cc and in
   gcc/sched-rgn.cc, and would take core GCC's classify_insn name.  */


#include <vector>

namespace rvtt_autoincr {

/* rtl-rvtt-dst-autoincr.cc grew past 2700 lines.  The scan/model half
   -- the target capability table, the classification vocabulary, the
   replay linearization records, candidate discovery, config-window
   legality and issue-word accounting -- is in
   rtl-rvtt-dst-autoincr-scan.cc.  It builds a read-only model and
   decides nothing; the guard/cost layer, placement, emission and the
   pass driver stay in the original file.

   These types are here rather than in either .cc because a function
   declared below takes them by reference: a type left in an anonymous
   namespace while a header names it is a subobject-linkage error.
   They are ordered by dependency.  */

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

extern autoincr_caps
target_autoincr_caps ();

extern bool
classify_access (rtx_insn *insn, int code, access_info *acc);

extern autoincr_class
classify_insn (rtx_insn *insn, access_info *acc);

extern bool
pure_dst_increment_p (rtx_insn *insn, HOST_WIDE_INT *stride);

extern bool
occupies_replay_slot_p (rtx_insn *insn);

extern void
scan_block (function_scan &fn, basic_block bb, const autoincr_caps &caps);

extern void
resolve_replay (function_scan &fn);

extern void
find_candidates (bb_scan &scan, const autoincr_caps &caps);

extern bool
gap_item_ok (const bb_item &item, const autoincr_caps &caps);

extern bool
config_window_item_ok (const bb_item &item, const autoincr_caps &caps);

extern unsigned
item_issue_words (const bb_item &item);

extern bool
crossing_reanchored_p (const bb_scan &scan, const candidate &cand);

extern unsigned
insn_frontend_cover_words (rtx_insn *insn);

extern unsigned
item_frontend_words (const bb_item &item);

} /* namespace rvtt_autoincr */

#endif /* GCC_RTL_RVTT_DST_AUTOINCR_INT_H */
