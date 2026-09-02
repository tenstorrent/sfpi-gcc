/* Dst tile-register / RWC ownership analysis and identity-reload folding.
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

/* Track B (SFPI_COMPILER_UPGRADE.md 18.9): model the Dst accumulator and
   its read-write-clear (RWC) counters as compiler-visible state so that
   author-written Dst round-trips ("reload due to register pressure")
   fold into LREG-resident values.

   Architecture: a pre-IRA mirror of rtl-rvtt-lreg-livein.cc.  Where that
   pass runs a forward union-join dataflow over an 8-bit LREG membership
   mask and closes CFG joins with a fresh local token, this pass runs the
   same forward union-join fixpoint over an abstract Dst/RWC state tuple
   and closes lossy joins with a fresh per-BB join token (the same idiom,
   translated from sentinel pseudos to state tokens: Dst faces and RWC
   counters are not IRA-allocatable resources, so no register interval
   needs to be materialized -- every Dst-touching instruction is an
   UNSPEC_VOLATILE and therefore already an ordering barrier).

   The abstract state per program point:

     - rwc epoch: an equality token for the RWC counter state
       (dst_rwc/dst_rwc_cr).  Every instruction whose typed RWC effect is
       not provably `none' (TTINCRWC/TTSETRWC/ttdstface, auto-increment
       load/store address modes, or any unproven effect) starts a fresh
       epoch.  Two Dst addresses name the same physical rows only within
       one epoch; this subsumes the permuted paired-row aliasing question
       because identity is only ever proven syntactically -- no address
       arithmetic is performed, and disjointness is never claimed.

     - layout epoch: an equality token for the CFG-state Dst layout
       (ALU_ACC_CTRL_* / dst_32bit_addr_en).  Any typed configuration write,
       any address-modifier slot write, and any instruction that may
       store to memory (configuration registers are memory mapped) starts
       a fresh epoch.  The absolute fp32/bf16/int8 tag is deliberately
       not computed: the fold only needs layout *stability*, i.e. that the
       store/reload pair sits inside one epoch with equal typed data-mode
       operands.  An imprecise tag therefore default-denies, per the
       18.9.6 risk note.

     - CC lane-state: ALL (provably all lanes enabled), OTHER (some
       narrowed or unknown-but-balanced state), or UNPROVED (poisoned).
       The compiler's own CC synthesis (gimple-rvtt-cc.cc) codifies the
       ambient contract: the outermost predication region's SFPPUSHC is
       removed and its SFPPOPC is replaced by the word-exact all-lanes
       SFPENCC, so function-scope ambient CC is all-lanes by construction.
       PUSHC/POPC maintain an explicit bounded stack of saved states;
       COMPC/SETCC and any non-all-lanes CC write narrow to OTHER; the
       proven all-lanes SFPENCC (word-exact against the capability
       table's encoding, via xtt_effect_set::cc_write_all_lanes) restores
       ALL.  Raw/opaque instructions are CC-TRANSPARENT, mirroring the
       shipped CC synthesis exactly (see transfer_insn); UNPROVED arises
       only from disagreeing CFG joins and stack over/underflow, and a
       typed CC write recovers from it.

   Classification is derived exclusively from:
     (a) the typed effect attribute family via rvtt_insn_effects
	 (refusing defaults: any unaudited instruction poisons), and
     (b) the RTL structure itself: an instruction whose pattern contains
	 no unspec_volatile, no memory store, and is neither a call nor
	 asm cannot touch Dst, RWC counters, configuration state, or CC
	 -- plain RISC scalar code is transparent by RTL semantics, and
     (c) the compiler's own CC-bracket and copy patterns
	 (SFPPUSHC/SFPPOPC/SFPCOMPC and the predicated-assign copy),
	 reached by recognized insn code exactly as rtl-rvtt-lreg-livein
	 reaches its sentinel patterns.  These are typed compiler-emitted
	 patterns whose semantic identity is their insn code; no opcode
	 words, operand fingerprints, or operation names are consulted.
	 Their effect data belongs in the attribute family; it is decoded
	 here (only) so the frozen macro-planner refusal surface is not
	 disturbed by an attribute audit in the same change.

   The transform (B4, under -mtt-tensix-optimize-dst-ownership): cancel a
   proven-identity Dst reload -- a load whose full typed operand tuple
   (logical Dst address, data mode, address mode, opcode/shift fields)
   matches an earlier load in the same block, with no RWC boundary, no
   layout boundary, no possibly-aliasing Dst store, no opacity between,
   the earlier load executed under provably all-lanes CC, and the reload
   carrying no live-value merge.  Two reads of unwritten Dst memory
   through the same mode return bit-identical values (the dst_row_valid
   bitmap state is also unchanged, so even an invalid-row read folds
   exactly); the reload is replaced by a plain XTT32SI copy, which the
   backend emits as the unconditional SFPMOV or coalesces away.  Matrix
   write -> pack read ordering is untouched: stores are never moved or
   removed, and any store kills every availability record.

   A candidate that fails a proof falls to a named refusal
   (`dst-rwc-effect-unproved', the SFPLOADMACRO_FORMATION.md vocabulary,
   with a parenthesized detail; CC failures use `cc-enable-unproved';
   an LREG pressure guard refusal uses `lreg-pressure-exceeded').
   Refusal paths never mutate anything, keeping refusals byte-identical
   to the flag-off compilation.  QSR has no faithful RWC model (unified
   counter field, untested sim path): the whole pass hard-refuses.

   The LREG pressure guard: folding extends the earlier load's value
   across the intervening computation.  XTT32SI values cannot be spilled
   (rvtt_sfpassign's BADLOAD/BADSTORE alternatives are compile errors),
   so the fold is refused unless the maximum simultaneous SFPU pressure
   over the extension span, plus one, fits the allocatable SFPU variable
   register budget (minus any raw-LREG reservations present in the
   function, counted conservatively function-wide).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "rtl-iter.h"
#include "tree.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "df.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-refuse.h"

namespace {

/* ------------------------------ state ------------------------------ */

/* CC lane-state lattice value.  */
enum cc_val : uint8_t { CC_ALL, CC_OTHER, CC_UNPROVED };

/* Bounded explicit stack of saved CC states; deeper nesting poisons.  */
constexpr unsigned CC_STACK_MAX = 16;

struct dstown_state
{
  /* Equality tokens; compared, never interpreted.  0 = function entry.  */
  int rwc_epoch;
  int layout_epoch;

  cc_val cc;
  uint8_t cc_depth;
  uint8_t cc_stack[CC_STACK_MAX];

  bool reached;

  static dstown_state entry ()
  {
    dstown_state s = {};
    s.rwc_epoch = 0;
    s.layout_epoch = 0;
    s.cc = CC_ALL;
    s.cc_depth = 0;
    s.reached = true;
    return s;
  }

  static dstown_state unreached ()
  {
    dstown_state s = {};
    s.reached = false;
    return s;
  }

  bool operator== (const dstown_state &o) const
  {
    if (reached != o.reached)
      return false;
    if (!reached)
      return true;
    if (rwc_epoch != o.rwc_epoch || layout_epoch != o.layout_epoch
	|| cc != o.cc || cc_depth != o.cc_depth)
      return false;
    for (unsigned i = 0; i < cc_depth && i < CC_STACK_MAX; i++)
      if (cc_stack[i] != o.cc_stack[i])
	return false;
    return true;
  }
  bool operator!= (const dstown_state &o) const { return !(*this == o); }

  void poison_cc () { cc = CC_UNPROVED; cc_depth = 0; }

  void cc_push ()
  {
    if (cc == CC_UNPROVED)
      return;
    if (cc_depth >= CC_STACK_MAX)
      {
	poison_cc ();
	return;
      }
    cc_stack[cc_depth++] = cc;
  }

  void cc_pop ()
  {
    if (cc == CC_UNPROVED)
      return;
    if (cc_depth == 0)
      {
	/* POPC without a visible matching PUSHC: unprovable.  */
	poison_cc ();
	return;
      }
    cc = (cc_val) cc_stack[--cc_depth];
  }
};

/* ------------------------- classification -------------------------- */

/* What one instruction does to the abstract state.  */
struct insn_facts
{
  bool poison;			/* opacity: everything unproved	     */
  bool rwc_boundary;		/* RWC counter state changes/unproved  */
  bool layout_boundary;		/* Dst layout CFG state may change     */
  bool dst_store;		/* may write Dst memory		     */
  bool cc_push, cc_pop;		/* bracket ops			     */
  bool cc_write;		/* CC written (non-bracket)	     */
  bool cc_write_all_lanes;	/* ... provably to the all-lanes state */
  bool plain_load;		/* the admitted Dst load pattern       */
};

/* Structural transparency: no unspec_volatile anywhere in the pattern,
   no memory store, no call, no asm.  Such an instruction cannot reach
   Dst, the RWC counters, configuration state, or CC.  */

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

/* Audited architectural effect data for typed value-op patterns whose
   generated FULL effect sets are not on record (rvtt_insn_effects
   reports them opaque) now lives at the definitions: the
   xtt_lane_local/xtt_cc_write attribute rows in rvtt.md, reached
   through rvtt_lane_local_effects (FABLE item #4; the former
   effect_overrides table copied verbatim from rtl-rvtt-lp-alloc.cc is
   deleted, and the planner-oracle re-freeze that blocked the migration
   is recorded in testsuite oracles/refreeze-pin49-20260831.txt).  The
   query is effect data, not operation-identity decision logic: the
   fold never keys on these codes, they only refine "opaque" to the
   pattern's architectural effect set.  */

static insn_facts
classify (rtx_insn *insn)
{
  insn_facts f = {};

  /* The compiler's own CC bracket patterns and predicated copy: typed
     compiler-emitted patterns reached by insn code (see the file
     comment).  PUSHC's operand is its mod field; only the plain push
     form is modeled.  */
  int code = recog_memoized (insn);
  if (code == CODE_FOR_rvtt_sfppushc)
    {
      extract_insn (insn);
      if (CONST_INT_P (recog_data.operand[0])
	  && INTVAL (recog_data.operand[0]) == 0)
	f.cc_push = true;
      else
	{
	  /* A mod-bearing PUSHC (e.g. replace) is a CC write of
	     unmodeled shape.  */
	  f.cc_push = true;
	  f.cc_write = true;
	}
      return f;
    }
  if (code == CODE_FOR_rvtt_sfppopc)
    {
      f.cc_pop = true;
      return f;
    }
  if (code == CODE_FOR_rvtt_sfpcompc)
    {
      f.cc_write = true;
      return f;
    }
  /* The predicated-assign copy is a pure LREG move under CC (its split
     form is a plain SET); it has no Dst/RWC/config/CC-write effect.
     Its define_insn is starred (no CODE_FOR), so it is identified by
     its typed unspec code.  */
  {
    rtx pat = PATTERN (insn);
    if (GET_CODE (pat) == SET
	&& GET_CODE (SET_SRC (pat)) == UNSPEC_VOLATILE
	&& XINT (SET_SRC (pat), 1) == UNSPECV_SFPASSIGN)
      return f;
  }

  {
    bool cc_writes;
    if (rvtt_lane_local_effects (insn, &cc_writes))
      {
	f.cc_write = cc_writes;
	return f;
      }
  }

  if (pattern_transparent_p (insn))
    return f;

  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    {
      f.poison = true;
      return f;
    }

  if (e.rwc.kind != xtt_rwc_effect_t::NONE)
    f.rwc_boundary = true;
  if (e.config_dests_written != 0 || e.addr_mod_slot_write)
    f.layout_boundary = true;
  if (e.dst_mem_write)
    f.dst_store = true;
  if (e.cc_write)
    {
      f.cc_write = true;
      f.cc_write_all_lanes = e.cc_write_all_lanes;
    }
  if (e.dst_mem_read && !e.dst_mem_write
      && recog_memoized (insn) == CODE_FOR_rvtt_sfpload_lv_int
      && e.rwc.kind == xtt_rwc_effect_t::NONE)
    f.plain_load = true;

  return f;
}

/* Apply FACTS to STATE.  Epoch tokens minted at a boundary instruction
   are a deterministic function of the instruction (its UID), so the
   fixpoint converges.  */

static void
transfer_insn (dstown_state &s, const insn_facts &f, rtx_insn *insn)
{
  if (f.poison)
    {
      /* Opacity is a hard boundary for the memory-correctness surfaces
	 (RWC counters, layout, and -- via the record kill in the scan
	 below -- Dst contents), but it is CC-TRANSPARENT: this mirrors
	 the shipped CC synthesis, which pairs v_if brackets across raw
	 asm statements and rewrites the outermost restore to the
	 word-exact all-lanes SFPENCC regardless of surrounding raw
	 code (gimple-rvtt-cc.cc) -- i.e. the compiler already bakes in
	 the contract that raw sequences never hand narrowed lanes to
	 typed code.  A raw CC-narrow feeding typed loads would already
	 be miscompiled by every existing v_endif; this pass inherits
	 exactly that established contract, adding no new assumption.  */
      s.rwc_epoch = INSN_UID (insn) * 4 + 1;
      s.layout_epoch = INSN_UID (insn) * 4 + 2;
      return;
    }
  if (f.rwc_boundary)
    s.rwc_epoch = INSN_UID (insn) * 4 + 1;
  if (f.layout_boundary)
    s.layout_epoch = INSN_UID (insn) * 4 + 2;
  if (f.cc_push)
    s.cc_push ();
  /* A typed CC write fully determines the current lane state, so it
     also recovers from an unproved state (e.g. after a lossy join);
     only the saved stack below stays whatever it was.  */
  if (f.cc_write)
    s.cc = f.cc_write_all_lanes ? CC_ALL : CC_OTHER;
  if (f.cc_pop)
    s.cc_pop ();
}

static dstown_state
transfer_block (basic_block bb, dstown_state s)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      transfer_insn (s, classify (insn), insn);
    }
  return s;
}

/* Join PRED into ACC for basic block BB.  A disagreeing (lossy) join
   mints the block's stable local join token -- the lreg-livein fresh
   local token idiom -- and poisons CC.  Returns true if the join was
   lossy in a dimension.  */

static bool
join_state (dstown_state &acc, const dstown_state &pred, basic_block bb)
{
  if (!pred.reached)
    return false;
  if (!acc.reached)
    {
      acc = pred;
      return false;
    }
  bool lossy = false;
  if (acc.rwc_epoch != pred.rwc_epoch)
    {
      acc.rwc_epoch = -(bb->index * 4 + 1);
      lossy = true;
    }
  if (acc.layout_epoch != pred.layout_epoch)
    {
      acc.layout_epoch = -(bb->index * 4 + 2);
      lossy = true;
    }
  if (acc.cc != pred.cc || acc.cc_depth != pred.cc_depth)
    {
      acc.poison_cc ();
      lossy = true;
    }
  else
    for (unsigned i = 0; i < acc.cc_depth; i++)
      if (acc.cc_stack[i] != pred.cc_stack[i])
	{
	  acc.poison_cc ();
	  lossy = true;
	  break;
	}
  return lossy;
}

/* --------------------------- fixpoint ------------------------------ */

static void
run_dataflow (function *fn, auto_vec<dstown_state> &in,
	      auto_vec<dstown_state> &out, sbitmap lossy_join)
{
  const unsigned n_bbs = last_basic_block_for_fn (fn);
  in.safe_grow_cleared (n_bbs);
  out.safe_grow_cleared (n_bbs);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      in[bb->index] = dstown_state::unreached ();
      out[bb->index] = dstown_state::unreached ();
    }
  bitmap_clear (lossy_join);

  bool changed;
  do
    {
      changed = false;
      FOR_EACH_BB_FN (bb, fn)
	{
	  dstown_state next_in = dstown_state::unreached ();
	  bool entry_pred = false;
	  bool lossy = false;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
		{
		  lossy |= join_state (next_in, dstown_state::entry (), bb);
		  entry_pred = true;
		}
	      else
		lossy |= join_state (next_in, out[e->src->index], bb);
	    }
	  if (!entry_pred && EDGE_COUNT (bb->preds) == 0)
	    next_in = dstown_state::entry ();
	  if (lossy)
	    bitmap_set_bit (lossy_join, bb->index);
	  dstown_state next_out
	    = next_in.reached ? transfer_block (bb, next_in) : next_in;
	  if (next_in != in[bb->index] || next_out != out[bb->index])
	    {
	      in[bb->index] = next_in;
	      out[bb->index] = next_out;
	      changed = true;
	    }
	}
    }
  while (changed);
}

/* ------------------------ availability scan ------------------------ */

/* Record of an earlier admitted Dst load.  DEAD_REASON is null while the
   record can still prove identity.  */
struct load_record
{
  rtx_insn *insn;
  rtx dest;
  /* Operand snapshot: 1 mem, 2 opcode, 3 shifts, 4 address, 5 src,
     7 mode, 8 address mode.  */
  rtx op1, op2, op3, op4, op5, op7, op8;
  const char *dead_reason;
};

static bool
noval_p (rtx x)
{
  return (GET_CODE (x) == UNSPEC
	  && XINT (x, 1) == UNSPEC_SFPNOVAL);
}

/* Whether INSN may set REG (rtlanal; no DF dependency).  */

static bool
insn_writes_reg_p (rtx_insn *insn, rtx reg)
{
  return reg_set_p (reg, insn);
}

/* ----------------------- LREG pressure guard ----------------------- */

/* Allocatable SFPU variable register budget, minus a conservative
   function-wide reservation for raw-LREG accesses (metadata builtins
   name architectural LREGs the allocator must avoid; treat the union of
   all masks in the function as permanently reserved).  */

static int
sfpu_budget (function *fn)
{
  int budget = 0;
  for (unsigned r = 0; r < FIRST_PSEUDO_REGISTER; r++)
    if (TEST_HARD_REG_BIT (reg_class_contents[SFPU_VAR_REGS], r)
	&& !fixed_regs[r])
      budget++;

  unsigned reserved = 0;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  switch (recog_memoized (insn))
	    {
	    case CODE_FOR_rvtt_sfprawlreg_access:
	      {
		rtx pat = PATTERN (insn);
		reserved |= UINTVAL (XVECEXP (pat, 0, 0)) & 0xff;
		reserved |= UINTVAL (XVECEXP (pat, 0, 1)) & 0xff;
	      }
	      break;
	    case CODE_FOR_rvtt_sfpreadlreg0:
	    case CODE_FOR_rvtt_sfpwritelreg0: reserved |= 1u << 0; break;
	    case CODE_FOR_rvtt_sfpreadlreg1:
	    case CODE_FOR_rvtt_sfpwritelreg1: reserved |= 1u << 1; break;
	    case CODE_FOR_rvtt_sfpreadlreg2:
	    case CODE_FOR_rvtt_sfpwritelreg2: reserved |= 1u << 2; break;
	    case CODE_FOR_rvtt_sfpreadlreg3:
	    case CODE_FOR_rvtt_sfpwritelreg3: reserved |= 1u << 3; break;
	    case CODE_FOR_rvtt_sfpreadlreg4:
	    case CODE_FOR_rvtt_sfpwritelreg4: reserved |= 1u << 4; break;
	    case CODE_FOR_rvtt_sfpreadlreg5:
	    case CODE_FOR_rvtt_sfpwritelreg5: reserved |= 1u << 5; break;
	    case CODE_FOR_rvtt_sfpreadlreg6:
	    case CODE_FOR_rvtt_sfpwritelreg6: reserved |= 1u << 6; break;
	    case CODE_FOR_rvtt_sfpreadlreg7:
	    case CODE_FOR_rvtt_sfpwritelreg7: reserved |= 1u << 7; break;
	    default:
	      {
		/* Fail closed: a raw-LREG accessor the cases above did
		   not decode must never be silently unreserved --
		   omission would OVERSTATE the budget and fold into
		   unallocatable pressure (the unsound direction; every
		   sibling gate fails closed on omission).  Any insn
		   carrying the raw-accessor unspec families reserves
		   the entire variable file.  */
		subrtx_iterator::array_type array;
		FOR_EACH_SUBRTX (iter, array, PATTERN (insn), ALL)
		  {
		    const_rtx x = *iter;
		    if (GET_CODE (x) == UNSPEC_VOLATILE
			&& (XINT (x, 1) == UNSPECV_SFPVARLREG
			    || XINT (x, 1) == UNSPECV_SFPRAWLREG_ACCESS))
		      {
			reserved = 0xff;
			if (dump_file)
			  fprintf (dump_file,
				   "Dst-ownership: unlisted raw-LREG"
				   " accessor at insn %d"
				   " (raw-lreg-accessor-unlisted):"
				   " reserving the variable file\n",
				   INSN_UID (insn));
			break;
		      }
		  }
	      }
	      break;
	    }
	}
    }
  return budget - popcount_hwi (reserved);
}

/* Whether REGNO counts against SFPU pressure.  */

static bool
sfpu_pressure_reg_p (unsigned regno)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    return GET_MODE (regno_reg_rtx[regno]) == XTT32SImode;
  return TEST_HARD_REG_BIT (reg_class_contents[SFPU_VAR_REGS], regno);
}

/* Maximum simultaneous SFPU pressure over (FROM, TO] in BB assuming
   SRC's live range is extended through TO.  Requires up-to-date DF LR.
   Walks backward from BB end using the standard simulation.  */

static int
span_pressure_with_extension (basic_block bb, rtx_insn *from, rtx_insn *to,
			      rtx src)
{
  auto_bitmap live;
  bitmap_copy (live, DF_LR_OUT (bb));
  df_simulate_initialize_backwards (bb, live);

  int max_pressure = 0;
  bool in_span = false;
  for (rtx_insn *insn = BB_END (bb); insn; insn = PREV_INSN (insn))
    {
      if (insn == to)
	in_span = true;
      if (NONDEBUG_INSN_P (insn))
	df_simulate_one_insn_backwards (bb, insn, live);
      if (in_span)
	{
	  int pressure = 0;
	  unsigned regno;
	  bitmap_iterator bi;
	  EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, bi)
	    if (sfpu_pressure_reg_p (regno))
	      pressure++;
	  if (!bitmap_bit_p (live, REGNO (src)))
	    pressure++;		/* the extension itself */
	  if (pressure > max_pressure)
	    max_pressure = pressure;
	}
      if (insn == from || insn == BB_HEAD (bb))
	break;
    }
  return max_pressure;
}

/* ITEM #13 (placement arbiter) shadow census: on a pressure-guard
   refusal, name the rematerializable constant-materialization webs
   live through the refused span -- the relief anatomy the arbiter's
   pressure-park fold reserve (gimple-rvtt-prgm-const.cc) prices at
   placement time.  A web qualifies as remat-class when its every
   definition is the constant-immediate SFPLOADI materialization (a
   multi-issue constant's completion writes are loadi too); such webs
   are exactly what the placement tiers pinned into the file.  Pure
   dump; the fold verdict is decided above, unchanged, in both flag
   states.  */

static void
dump_relief_census (basic_block bb, rtx_insn *from, rtx_insn *to)
{
  if (!dump_file)
    return;
  auto_bitmap live, span_live;
  bitmap_copy (live, DF_LR_OUT (bb));
  df_simulate_initialize_backwards (bb, live);
  bool in_span = false;
  for (rtx_insn *insn = BB_END (bb); insn; insn = PREV_INSN (insn))
    {
      if (insn == to)
	in_span = true;
      if (NONDEBUG_INSN_P (insn))
	df_simulate_one_insn_backwards (bb, insn, live);
      if (in_span)
	bitmap_ior_into (span_live, live);
      if (insn == from || insn == BB_HEAD (bb))
	break;
    }
  unsigned webs = 0;
  fprintf (dump_file, "placement-arbiter: relief census at insn %d:",
	   INSN_UID (to));
  unsigned regno;
  bitmap_iterator bi;
  EXECUTE_IF_SET_IN_BITMAP (span_live, FIRST_PSEUDO_REGISTER, regno, bi)
    {
      if (!sfpu_pressure_reg_p (regno))
	continue;
      bool remat = false;
      unsigned HOST_WIDE_INT value = 0;
      for (df_ref def = DF_REG_DEF_CHAIN (regno); def;
	   def = DF_REF_NEXT_REG (def))
	{
	  rtx_insn *di = DF_REF_INSN (def);
	  if (!di || recog_memoized (di) != CODE_FOR_rvtt_sfploadi_lv_int)
	    {
	      remat = false;
	      break;
	    }
	  extract_insn (di);
	  rtx imm = recog_data.operand[4];
	  if (!CONST_INT_P (imm))
	    {
	      remat = false;
	      break;
	    }
	  if (!remat)
	    value = UINTVAL (imm);
	  remat = true;
	}
      if (remat)
	{
	  ++webs;
	  fprintf (dump_file, " r%u(loadi 0x%08x)", regno,
		   (unsigned) value);
	}
    }
  fprintf (dump_file,
	   " -- %u remat-class constant web(s) live through the refused"
	   " span\n", webs);
}

/* ------------------------------ pass ------------------------------- */

static void
dump_refusal (const char *reason, const char *detail, rtx_insn *insn)
{
  rvtt_refuse_by_name (reason, dump_file,
		       "Dst-ownership formation-refusal: %s (%s) at insn %d\n",
		       reason, detail ? detail : "", insn ? INSN_UID (insn) : -1);
}

static unsigned
dst_ownership (function *fn)
{
  const bool transform = riscv_tt_opt_dst_ownership;

  /* QSR's RWC model (unified value field, untested simulator path) has
     no faithful typed representation: ownership is modeled precisely on
     WH/BH and hard-refused on QSR -- never wrong.  */
  if (TARGET_XTT_TENSIX_QSR)
    {
      dump_refusal ("dst-rwc-effect-unproved", "qsr-unmodeled", NULL);
      return 0;
    }

  auto_vec<dstown_state> in, out;
  auto_sbitmap lossy_join (last_basic_block_for_fn (fn));
  run_dataflow (fn, in, out, lossy_join);

  if (dump_file)
    {
      basic_block bb;
      FOR_EACH_BB_FN (bb, fn)
	{
	  const dstown_state &s = in[bb->index];
	  if (!s.reached)
	    {
	      fprintf (dump_file, "Dst-ownership: bb %d unreached\n",
		       bb->index);
	      continue;
	    }
	  fprintf (dump_file,
		   "Dst-ownership: bb %d in rwc=%d layout=%d cc=%s depth=%u"
		   "%s\n",
		   bb->index, s.rwc_epoch, s.layout_epoch,
		   s.cc == CC_ALL ? "all" : s.cc == CC_OTHER ? "other"
		   : "unproved",
		   s.cc_depth,
		   bitmap_bit_p (lossy_join, bb->index)
		   ? " (lossy-join)" : "");
	  if (bitmap_bit_p (lossy_join, bb->index))
	    dump_refusal ("dst-rwc-effect-unproved", "lossy-join",
			  BB_HEAD (bb) ? BB_END (bb) : NULL);
	}
    }

  int budget = 0;
  bool df_ready = false;
  bool mutated = false;
  unsigned folds = 0;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (!in[bb->index].reached)
	continue;
      dstown_state s = in[bb->index];
      /* A lossy join gives this block fresh epochs already; CC is
	 poisoned by the join when it disagreed.  */

      auto_vec<load_record, 8> records;

      rtx_insn *insn, *next;
      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb)); insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  insn_facts f = classify (insn);

	  if (f.plain_load)
	    {
	      extract_insn (insn);
	      rtx dest = recog_data.operand[0];
	      rtx op1 = recog_data.operand[1];
	      rtx op2 = recog_data.operand[2];
	      rtx op3 = recog_data.operand[3];
	      rtx op4 = recog_data.operand[4];
	      rtx op5 = recog_data.operand[5];
	      rtx op6 = recog_data.operand[6];
	      rtx op7 = recog_data.operand[7];
	      rtx op8 = recog_data.operand[8];

	      /* Candidate reload?  Match against records newest-first;
		 a structural match that fails a proof dumps its named
		 refusal.  */
	      load_record *hit = NULL;
	      for (unsigned i = records.length (); i-- > 0;)
		{
		  load_record &r = records[i];
		  if (rtx_equal_p (r.op1, op1) && rtx_equal_p (r.op2, op2)
		      && rtx_equal_p (r.op3, op3) && rtx_equal_p (r.op4, op4)
		      && rtx_equal_p (r.op5, op5) && rtx_equal_p (r.op7, op7)
		      && rtx_equal_p (r.op8, op8))
		    {
		      hit = &r;
		      break;
		    }
		}

	      bool folded = false;
	      if (hit)
		{
		  if (hit->dead_reason)
		    {
		      /* CC failures use their own vocabulary word.  */
		      if (strcmp (hit->dead_reason,
				  "cc-enable-unproved") == 0)
			dump_refusal ("cc-enable-unproved", NULL, insn);
		      else
			dump_refusal ("dst-rwc-effect-unproved",
				      hit->dead_reason, insn);
		    }
		  else if (!noval_p (op6))
		    dump_refusal ("dst-rwc-effect-unproved",
				  "live-value-merge", insn);
		  else if (transform)
		    {
		      if (!df_ready)
			{
			  df_analyze ();
			  budget = sfpu_budget (fn);
			  df_ready = true;
			}
		      int pressure
			= span_pressure_with_extension (bb, hit->insn, insn,
							hit->dest);
		      if (pressure > budget)
			{
			  rvtt_refuse (RVTT_REF_LREG_PRESSURE_EXCEEDED, dump_file,
				       "Dst-ownership formation-refusal:"
				       " lreg-pressure-exceeded"
				       " (pressure %d > budget %d) at insn"
				       " %d\n",
				       pressure, budget, INSN_UID (insn));
			  dump_relief_census (bb, hit->insn, insn);
			}
		      else
			{
			  /* A plain XTT32SI reg-reg SET: the backend's
			     unconditional copy (rvtt_sfpassign), which
			     IRA coalesces or emits as SFPMOV mod 2.  */
			  rtx_insn *copy
			    = emit_insn_before (gen_rtx_SET (dest,
							     hit->dest),
						insn);
			  if (dump_file)
			    fprintf (dump_file,
				     "Dst-ownership fold: reload insn %d"
				     " (of load insn %d) replaced by"
				     " lreg-resident copy insn %d\n",
				     INSN_UID (insn), INSN_UID (hit->insn),
				     INSN_UID (copy));
			  delete_insn (insn);
			  folds++;
			  mutated = true;
			  folded = true;
			  /* DF and pressure are stale now; recompute
			     lazily before the next guard query.  */
			  df_ready = false;
			}
		    }
		  else if (dump_file)
		    fprintf (dump_file,
			     "Dst-ownership: provable identity reload at"
			     " insn %d (of load insn %d); transform"
			     " disabled\n",
			     INSN_UID (insn), INSN_UID (hit->insn));
		}

	      if (!folded)
		{
		  /* This load becomes (or refreshes) the record for its
		     tuple, provided CC is provably all-lanes here.  */
		  load_record r = {};
		  r.insn = insn;
		  r.dest = dest;
		  r.op1 = op1; r.op2 = op2; r.op3 = op3; r.op4 = op4;
		  r.op5 = op5; r.op7 = op7; r.op8 = op8;
		  /* Recordable only when this load provably wrote all
		     lanes: any later reload's lane set is then a
		     subset, whatever CC narrows to in between.  */
		  r.dead_reason
		    = s.cc == CC_ALL ? NULL : "cc-enable-unproved";
		  /* Address carried in a register: the record dies if
		     that register is redefined (checked below via the
		     generic writes scan).  */
		  records.safe_push (r);
		}
	      /* Loads have no state effect (rwc NONE admitted).  */
	      continue;
	    }

	  /* Non-load instruction: update state and kill records.  */
	  const char *kill = NULL;
	  if (f.poison)
	    kill = "opaque-effect";
	  else if (f.rwc_boundary)
	    kill = "rwc-boundary";
	  else if (f.dst_store)
	    kill = "dst-store-may-alias";
	  else if (f.layout_boundary)
	    kill = "layout-boundary";

	  if (kill)
	    for (unsigned i = 0; i < records.length (); i++)
	      if (!records[i].dead_reason)
		records[i].dead_reason = kill;

	  /* A redefinition of a record's value or address register
	     invalidates it.  */
	  if (INSN_P (insn))
	    for (unsigned i = 0; i < records.length (); i++)
	      {
		load_record &r = records[i];
		if (r.dead_reason)
		  continue;
		if ((REG_P (r.dest) && insn_writes_reg_p (insn, r.dest))
		    || (REG_P (r.op4) && insn_writes_reg_p (insn, r.op4)))
		  r.dead_reason = "value-clobbered";
	      }

	  transfer_insn (s, f, insn);
	}
    }

  if (dump_file)
    fprintf (dump_file, "Dst-ownership: %u reload(s) folded\n", folds);

  return mutated ? TODO_df_finish : 0;
}

const pass_data pass_data_rvtt_dst_ownership =
{
  RTL_PASS, "rvtt_dst_ownership", OPTGROUP_OTHER, TV_NONE,
  0, 0, 0, 0, 0
};

class pass_rvtt_dst_ownership : public rtl_opt_pass
{
public:
  pass_rvtt_dst_ownership (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_dst_ownership, ctxt) {}

  bool gate (function *) final override { return TARGET_XTT_TENSIX; }

  unsigned execute (function *fn) final override
  {
    return dst_ownership (fn);
  }
};

} /* anonymous namespace */

rtl_opt_pass *
make_pass_rvtt_dst_ownership (gcc::context *ctxt)
{
  return new pass_rvtt_dst_ownership (ctxt);
}
