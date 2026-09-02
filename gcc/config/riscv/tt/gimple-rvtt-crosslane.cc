/* Cross-lane structured-op lowering/fusion pass (lane FG, X4).
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

/* -mtt-tensix-optimize-crosslane (default off).

   THE PROBLEM.  The typed cross-lane vocabulary (sfpi_crosslane.h, lane
   FA; design: lane EY-R CROSSLANE-DESIGN-INPUT.md) lowers its composite
   primitives as PINNED canonical inline frames -- rotate chains of
   SFPSHFT2 SUBVEC_SHFLROR1, SFPSWAP compare-exchange stages, dual-bank
   SFPTRANSP frames, and the zip frame (staged dual-bank transpose plus
   a row-predicated pair swap).  Composition of these primitives at the
   call level (rotate of rotate, sort of sorted, zip round trips, the
   per-arch slide spelling) leaves algebraically redundant instruction
   chains that no generic pass understands: the ops are opaque builtin
   calls.

   THE MECHANISM (recognizer chain over canonical forms).  Exactly the
   architecture every production backend uses for constant-pattern
   shuffles (GCC VEC_PERM_EXPR: per-target recognizer chain, dual
   test/emit, cheapest first -- aarch64.cc evpc chain, riscv-v.cc
   shuffle chain; LLVM's is*Mask recognizers over a small canonical
   form): this pass recognizes the canonical frames the surface pins
   (the documented "X4 seam" of every sfpi_crosslane.h op) and applies
   the proven algebra of the underlying permutations:

     R1 rotate-chain collapse    ror1^n == ror1^(n mod 8)   [ror1^8 = id]
     R2 slide re-lowering        rotr<K> + predicated-zero(cols < K)
                                 == K x SUBVEC_SHFLSHR1     [per-arch]
     R3 swap refold              min/max o min/max == min/max (values);
                                 mod0 swap o mod0 swap == identity
     R4 transpose involution     transp8 o transp8 == identity
     R5 zip-chain collapse       zip^3 == identity (the 8-row riffle has
                                 order 3; zip^2 == unzip is canonical)

   Each identity is host-proven in the independent acceptance arsenal
   (lane FB, tt-metal tests/python_tests/test_crosslane_oracle_identities
   .py: rotate/transpose/zip inverses, swap mask table and tie-model
   witnesses) -- that battery is the specification of the algebra used
   here, derived only from the tt-isa-documentation functional models
   (SFPSHFT2.md, SFPSWAP.md, SFPTRANSP.md, SFPCONFIG.md).

   LANE-STATE OBLIGATION.  Every rewritten instruction is lanewise
   predicated (VectorUnit.md LaneEnabled), and rotation/transposition
   algebra composes only when every lane participates: under a partial
   enable state a fresh-destination rotate leaves stale values in
   disabled columns which the NEXT rotate reads back into enabled ones,
   so ror1^8 is NOT the identity there.  Following the transp-involution
   precedent this pass takes NO reaching-state axiom: rewrites R1, R2,
   R4 and R5 require a PROVEN all-lanes state -- a dominating word-exact
   all-lanes SFPENCC (rvtt_macro::sfpencc_all_lanes_word) with only
   proven lane-state-preserving statements between -- and refuse
   otherwise (crosslane-lane-state-unproven).  R3 is the exception with
   a weaker obligation: min/max refolding is per-lane idempotent
   whatever the enable state, PROVIDED the state cannot change between
   the two exchanges (a lane enabled only for the second exchange would
   really sort); it requires only an unchanged-CC window
   (crosslane-cc-window on failure).

   KEY-VALUE REFUSAL (tie divergence).  The indexed (ENABLE_DEST_INDEX)
   compare-exchange moves companion payloads on the swap decision, and
   the swap decision on EQUAL keys is an UNADJUDICATED doc-vs-sim
   divergence (lane FB finding: SFPSWAP.md keys tie-swaps on sign; the
   pinned simulator uses min c<d / max c>=d).  Under the doc model a
   second identical exchange moves companions AGAIN on ties, so indexed
   refolding is not provable until silicon adjudicates: refused by name
   (crosslane-kv-refold-tie-unadjudicated).  The mod-0 indexed pair
   (unconditional swap, no comparison) has no tie arm and cancels
   exactly like the value form.

   PER-ARCH CAPABILITY TABLE.  Following the surface's own per-arch
   split (subvec_slideup: Wormhole's SUBVEC_SHFLSHR1 gives lane 0 an
   UnpredictableValue -- WormholeB0 SFPSHFT2.md -- so WH lowers slides
   via rotate+mask), R2's re-lowering into SHFLSHR1 consults the table
   and refuses by name on targets without the fixed shift
   (crosslane-shflshr1-unsupported).  Quasar is not covered by the
   pinned-simulator proof battery yet: the whole pass refuses there
   (crosslane-unsupported-target).

   PRICING.  Every rewrite is priced in issue slots from the audited
   hazard facts before it fires: SFPSWAP and the SFPSHFT2 shuffle modes
   accept only SFPNOP on the next cycle (SFPSWAP.md / SFPSHFT2.md; the
   rvtt.md xtt_next_slot_stall attribute carries the same audit), so
   each costs 2 slots; SFPTRANSP costs 1.  A rewrite fires only when
   the priced after-cost does not exceed the before-cost; the dump
   records both sides of every decision.

   REFUSAL NAMES (dump-visible, append-only):
     crosslane-unsupported-target, crosslane-lane-state-unproven,
     crosslane-cc-window, crosslane-kv-refold-tie-unadjudicated,
     crosslane-shflshr1-unsupported, crosslane-companion-escape,
     crosslane-priced-no-gain, crosslane-frame-value-escape.

   All refusals leave the function byte-identical.  The TEN-2932
   ENABLE_DEST_INDEX window model (the other half of the X4 charter) is
   enforced on the FINAL RTL stream by the companion pass
   rtl-rvtt-crosslane-window.cc, behind the same flag.  */

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-ssanames.h"
#include "tree-cfg.h"
#include "cfghooks.h"
#include "cfganal.h"
#include "dominance.h"
#include "fold-const.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-macro-tables.h"
#include "tree-dfa.h"

namespace {

#define DUMP(...) if (dump_file) fprintf (dump_file, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Per-arch capability and pricing table.

   Costs are issue slots on the Tensix vector unit front end:
   SFPSWAP.md and SFPSHFT2.md pin the next-slot acceptance stall ("on
   the next cycle, the only instruction that the Vector Unit can accept
   is SFPNOP") for the swap and the SFPSHFT2 shuffle modes, so each
   such instruction occupies two slots; SFPTRANSP has no such note and
   occupies one.  The same audited facts ride rvtt-cost.md
   (xtt_next_slot_stall).  */

struct crosslane_caps
{
  bool shflror1;		/* SFPSHFT2 Mod1=3 usable  */
  bool shflshr1;		/* SFPSHFT2 Mod1=4 usable  */
  unsigned shft2_shuffle_slots;	/* issue + forced-NOP slot */
  unsigned swap_slots;		/* issue + forced-NOP slot */
  unsigned transp_slots;
};

static const crosslane_caps *
get_crosslane_caps ()
{
  /* BlackholeA0 SFPSHFT2.md: both shuffle modes architectural.  */
  static const crosslane_caps bh = { true, true, 2, 2, 1 };
  /* WormholeB0 SFPSHFT2.md, SUBVEC_SHFLSHR1: "Lane 0 receives an
     UnpredictableValue ... should not be used on Wormhole".  */
  static const crosslane_caps wh = { true, false, 2, 2, 1 };
  if (TARGET_XTT_TENSIX_BH)
    return &bh;
  if (TARGET_XTT_TENSIX_WH)
    return &wh;
  return nullptr;		/* Quasar: crosslane-unsupported-target */
}

/* SFPSHFT2 mod1 values (SFPSHFT2.md).  Architectural encodings, not
   fingerprints: they are the two members the builtin's own mod envelope
   ({M, 0x18} in rvtt-insn.def) admits.  */
static const unsigned SHFT2_MOD1_ROR1 = 3;
static const unsigned SHFT2_MOD1_SHR1 = 4;

/* ------------------------------------------------------------------ */
/* Small gimple helpers.  */

static bool
const_uarg (const gcall *call, unsigned argno, unsigned *out)
{
  if (gimple_call_num_args (call) <= argno)
    return false;
  tree arg = gimple_call_arg (call, argno);
  if (TREE_CODE (arg) != INTEGER_CST || !tree_fits_uhwi_p (arg))
    return false;
  *out = (unsigned) tree_to_uhwi (arg);
  return true;
}

static const rvtt_insn_data *
call_insnd (gimple *stmt)
{
  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    return nullptr;
  return rvtt_get_insn_data (call);
}

/* Look through sfpassign/sfpassign_lv version threading: the VALUE of
   an assign is its last source (transp-involution precedent).  */

static tree
resolve_value (tree v, unsigned depth = 0)
{
  while (depth++ < 16 && TREE_CODE (v) == SSA_NAME)
    {
      gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (v));
      if (!def)
	return v;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
      if (!insnd
	  || (insnd->id != rvtt_insn_data::sfpassign
	      && insnd->id != rvtt_insn_data::sfpassign_lv))
	return v;
      v = gimple_call_arg (def, gimple_call_num_args (def) - 1);
    }
  return v;
}

/* Follow one optional assign/assign_lv thread consuming V as its value
   (last argument) in BB.  Pushes a consumed assign onto STMTS.  A
   result-less assign (a dead thread; the surface's discarded outputs
   compile to lhs-less sfpassign_lv calls) is consumed but V is kept:
   with its single use consumed, V itself carries the value for both
   resolve comparisons and (vacuous) use forwarding.  */

static tree
thread_assign (tree v, basic_block bb, std::vector<gimple *> *stmts)
{
  use_operand_p use_p;
  gimple *use;
  if (!v || TREE_CODE (v) != SSA_NAME || !has_single_use (v)
      || !single_imm_use (v, &use_p, &use))
    return v;
  gcall *asg = dyn_cast <gcall *> (use);
  const rvtt_insn_data *asgd = asg ? rvtt_get_insn_data (asg) : nullptr;
  if (!asgd
      || (asgd->id != rvtt_insn_data::sfpassign_lv
	  && asgd->id != rvtt_insn_data::sfpassign)
      || gimple_bb (asg) != bb
      || gimple_call_arg (asg, gimple_call_num_args (asg) - 1) != v)
    return v;
  stmts->push_back (asg);
  tree lhs = gimple_call_lhs (asg);
  return lhs ? lhs : v;
}

/* Word-exact all-lanes SFPENCC -- proven against the capability
   table's architectural encoding.  The builtin's argument order is
   (mod1, imm12): the correct all-lanes call is
   __builtin_rvtt_sfpencc (10, 3), which the rvtt_sfpencc template
   ("SFPENCC\t%1, %0") prints as the gas spelling "SFPENCC 3, 10"
   (Imm12 first, Mod1 second -- gas-verified: the inverted spelling
   rejects with an invalid-mod error).  Mirrors the structured-CC
   lowering's region-exit emission and transp-involution's check.
   NOTE (lane FG finding): several in-tree compile-only dg tests spell
   the call inverted, (3, 10); their .s would not assemble.  */

static bool
encc_all_lanes_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (insnd->id != rvtt_insn_data::sfpencc)
    return false;
  unsigned mod1, imm12;
  if (!const_uarg (call, 0, &mod1) || !const_uarg (call, 1, &imm12))
    return false;
  uint32_t word;
  return rvtt_macro::sfpencc_encode (imm12, mod1, &word)
	 && word == rvtt_macro::sfpencc_all_lanes_word ();
}

/* ------------------------------------------------------------------ */
/* Statement classification for the lane-state / CC-window scans
   (refusing default; the audited safe-compute list mirrors
   gimple-rvtt-transp-involution.cc).  */

enum stmt_class
{
  SC_SKIP,	/* no code					*/
  SC_SAFE,	/* no CC write, no hidden raw effect		*/
  SC_ENCC_ALL,	/* word-exact all-lanes enable			*/
  SC_CC_WRITE,	/* typed CC writer				*/
  SC_BARRIER,	/* everything unproven				*/
};

static bool
safe_compute_id_p (rvtt_insn_data::insn_id id)
{
  switch (id)
    {
    case rvtt_insn_data::synth_opcode:
    case rvtt_insn_data::sfpnop:
    case rvtt_insn_data::sfpnovalue:
    case rvtt_insn_data::sfpselect2:
    case rvtt_insn_data::sfpselect4:
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
    case rvtt_insn_data::sfploadi:
    case rvtt_insn_data::sfploadi_lv:
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpand:
    case rvtt_insn_data::sfpand_lv:
    case rvtt_insn_data::sfpor:
    case rvtt_insn_data::sfpor_lv:
    case rvtt_insn_data::sfpxor:
    case rvtt_insn_data::sfpxor_lv:
    case rvtt_insn_data::sfpnot:
    case rvtt_insn_data::sfpnot_lv:
    case rvtt_insn_data::sfpshft_v:
    case rvtt_insn_data::sfpshft_v_lv:
    case rvtt_insn_data::sfpshft_i:
    case rvtt_insn_data::sfpshft_i_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
    case rvtt_insn_data::sfpswap:
    case rvtt_insn_data::sfpswap_indexed:
    case rvtt_insn_data::sfptransp8:
    case rvtt_insn_data::sfpshft2_subvec_shfl1:
    case rvtt_insn_data::sfpshft2_subvec_shfl1_lv:
    case rvtt_insn_data::sfpmul24:
    case rvtt_insn_data::sfpmul24_lv:
    case rvtt_insn_data::sfparecip:
    case rvtt_insn_data::sfparecip_lv:
    case rvtt_insn_data::sfpnonlinear:
    case rvtt_insn_data::sfpnonlinear_lv:
    case rvtt_insn_data::sfpreadconfig:
    case rvtt_insn_data::sfpreadconfig_lv:
    case rvtt_insn_data::sfpreadlreg:
    case rvtt_insn_data::sfpload:
    case rvtt_insn_data::sfpload_lv:
    case rvtt_insn_data::sfpstore:
      return true;
    default:
      return false;
    }
}

static bool
cc_writer_id_p (rvtt_insn_data::insn_id id)
{
  switch (id)
    {
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpxvif:
    case rvtt_insn_data::sfpxbool:
    case rvtt_insn_data::sfpxcondb:
    case rvtt_insn_data::sfpxcondi:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
      return true;
    default:
      return false;
    }
}

static stmt_class
classify_stmt (gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_NOP
      || gimple_code (stmt) == GIMPLE_PREDICT)
    return SC_SKIP;

  if (gasm *a = dyn_cast <gasm *> (stmt))
    {
      const char *s = gimple_asm_string (a);
      while (s && (*s == ' ' || *s == '\t'))
	++s;
      return (s && !*s) ? SC_SAFE : SC_BARRIER;
    }

  if (gimple_code (stmt) == GIMPLE_COND || gimple_code (stmt) == GIMPLE_PHI)
    return SC_SAFE;

  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    return SC_SAFE;		/* plain scalar gimple */

  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    {
      if (gimple_call_internal_p (call))
	return SC_SAFE;
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	return SC_SAFE;
      return SC_BARRIER;	/* real call */
    }

  if (insnd->id == rvtt_insn_data::sfpencc_all_lanes)
    return SC_ENCC_ALL;	/* the typed spelling; its expander asserts
				   the word-exact all-lanes encoding */
  if (encc_all_lanes_call_p (call, insnd))
    return SC_ENCC_ALL;
  if (cc_writer_id_p (insnd->id) || insnd->sets_cc (call))
    return SC_CC_WRITE;
  if (safe_compute_id_p (insnd->id))
    return SC_SAFE;
  return SC_BARRIER;
}

/* ------------------------------------------------------------------ */
/* The transform.  */

class crosslane_transform
{
public:
  crosslane_transform (function *fn, const crosslane_caps *caps)
    : m_fn (fn), m_caps (caps), m_changed (false) {}
  bool run ();

private:
  void compute_lane_states ();
  bool bb_in_clean (basic_block bb);
  bool lane_state_before (gimple *stmt);
  bool cc_unchanged_between (gimple *from, gimple *to);
  void note_frame_stmt (gimple *stmt) { m_frame_stmts.add (stmt); }
  bool companion_escape_p ();

  void collapse_rotate_chains ();
  void relower_slides ();
  void refold_swaps ();
  void cancel_transp8_pairs ();
  void collapse_zip_chains ();

  void delete_stmt (gimple *stmt);

  function *m_fn;
  const crosslane_caps *m_caps;
  bool m_changed;
  /* Per-BB OUT lane state: CLEAN = proven all-lanes.  */
  auto_bitmap m_clean_out;
  /* Statements consumed by recognized frames (readlreg bookkeeping).  */
  hash_set<gimple *> m_frame_stmts;
};

/* Forward all-lanes dataflow (transp-involution discipline: entry is
   DIRTY, no reaching-state axiom).  */

void
crosslane_transform::compute_lane_states ()
{
  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, m_fn)
	{
	  bool state = bb_in_clean (bb);
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    switch (classify_stmt (gsi_stmt (gsi)))
	      {
	      case SC_SKIP: case SC_SAFE:
		break;
	      case SC_ENCC_ALL:
		state = true;
		break;
	      default:
		state = false;
		break;
	      }
	  if (state != bitmap_bit_p (m_clean_out, bb->index))
	    {
	      if (state)
		bitmap_set_bit (m_clean_out, bb->index);
	      else
		bitmap_clear_bit (m_clean_out, bb->index);
	      changed = true;
	    }
	}
    }
}

bool
crosslane_transform::bb_in_clean (basic_block bb)
{
  if (bb == ENTRY_BLOCK_PTR_FOR_FN (m_fn) || EDGE_COUNT (bb->preds) == 0)
    return false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == ENTRY_BLOCK_PTR_FOR_FN (m_fn)
	|| !bitmap_bit_p (m_clean_out, e->src->index))
      return false;
  return true;
}

/* All-lanes state proven immediately before STMT.  */

bool
crosslane_transform::lane_state_before (gimple *stmt)
{
  basic_block bb = gimple_bb (stmt);
  bool state = bb_in_clean (bb);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      if (gsi_stmt (gsi) == stmt)
	return state;
      switch (classify_stmt (gsi_stmt (gsi)))
	{
	case SC_SKIP: case SC_SAFE:
	  break;
	case SC_ENCC_ALL:
	  state = true;
	  break;
	default:
	  state = false;
	  break;
	}
    }
  return false;
}

/* No CC-state change strictly between FROM and TO (same BB, FROM before
   TO).  An all-lanes re-enable still changes the state (a lane disabled
   at FROM could be enabled at TO), so only SC_SAFE/SC_SKIP pass.  */

bool
crosslane_transform::cc_unchanged_between (gimple *from, gimple *to)
{
  if (gimple_bb (from) != gimple_bb (to))
    return false;
  gimple_stmt_iterator gsi = gsi_for_stmt (from);
  gsi_next (&gsi);
  for (; !gsi_end_p (gsi) && gsi_stmt (gsi) != to; gsi_next (&gsi))
    {
      stmt_class c = classify_stmt (gsi_stmt (gsi));
      if (c != SC_SKIP && c != SC_SAFE)
	return false;
    }
  return !gsi_end_p (gsi);
}

void
crosslane_transform::delete_stmt (gimple *stmt)
{
  /* A matched constituent whose result is still used elsewhere (e.g. a
     CSE-shared zero or mask materialization) stays: its value is
     unchanged by the rewrite, and downstream DCE owns dead leftovers.
     Deletions are ordered so use-exclusive constituents reach zero
     uses before their turn.  */
  tree lhs = gimple_get_lhs (stmt);
  if (lhs && TREE_CODE (lhs) == SSA_NAME && !has_zero_uses (lhs))
    return;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  rvtt_prep_stmt_for_deletion (stmt);
  gsi_remove (&gsi, true);
  m_changed = true;
}

/* ------------------------------------------------------------------ */
/* R1: rotate-chain collapse.

   A chain is a maximal single-use sequence of SUBVEC_SHFLROR1 calls
   (plain or live-value form; under the proven all-lanes state both
   compute the same full-register rotation), each consuming the
   previous link's result.  ror1^8 == identity (SFPSHFT2.md: within
   each group of 8 lanes VD[lane] = VC[lane-1 mod 8]; FB battery:
   rotate inverses), so a chain of n links computes ror1^(n mod 8) and
   the tail's uses can be served by link (n mod 8) -- or by the source
   itself when n mod 8 == 0.  Priced: 2 slots per deleted SFPSHFT2
   shuffle (next-slot acceptance stall, SFPSHFT2.md).  */

struct rot_chain
{
  std::vector<gcall *> links;
  tree source;			/* value rotated by the whole chain */
};

/* Is CALL a subvec ror1 link?  Returns the rotated-value operand.  */

static tree
ror1_link_p (gimple *stmt)
{
  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    return NULL_TREE;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return NULL_TREE;
  unsigned mod;
  if (insnd->id == rvtt_insn_data::sfpshft2_subvec_shfl1)
    {
      if (!const_uarg (call, 1, &mod) || mod != SHFT2_MOD1_ROR1)
	return NULL_TREE;
      return gimple_call_arg (call, 0);
    }
  if (insnd->id == rvtt_insn_data::sfpshft2_subvec_shfl1_lv)
    {
      if (!const_uarg (call, 2, &mod) || mod != SHFT2_MOD1_ROR1)
	return NULL_TREE;
      return gimple_call_arg (call, 1);
    }
  return NULL_TREE;
}

void
crosslane_transform::collapse_rotate_chains ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	tree src = ror1_link_p (stmt);
	if (!src)
	  continue;
	/* Chain head: source is not itself a single-use ror1 link in
	   this BB (those are consumed by their own chain walk).  */
	if (TREE_CODE (src) == SSA_NAME && has_single_use (src))
	  {
	    gimple *def = SSA_NAME_DEF_STMT (src);
	    if (ror1_link_p (def) && gimple_bb (def) == bb)
	      continue;
	  }

	rot_chain chain;
	chain.source = src;
	gcall *cur = as_a <gcall *> (stmt);
	while (true)
	  {
	    chain.links.push_back (cur);
	    tree lhs = gimple_call_lhs (cur);
	    if (!lhs || !has_single_use (lhs))
	      break;
	    use_operand_p use_p;
	    gimple *use;
	    if (!single_imm_use (lhs, &use_p, &use))
	      break;
	    if (gimple_bb (use) != bb)
	      break;
	    tree next_src = ror1_link_p (use);
	    if (!next_src || next_src != lhs)
	      break;
	    cur = as_a <gcall *> (use);
	  }

	unsigned n = chain.links.size ();
	unsigned m = n % 8;
	if (m == n)
	  continue;		/* nothing to collapse */

	gcall *tail = chain.links.back ();
	unsigned before = n * m_caps->shft2_shuffle_slots;
	unsigned after = m * m_caps->shft2_shuffle_slots;
	if (after > before)	/* cannot happen; keep the guard honest */
	  {
	    DUMP ("crosslane: rotate chain at uid %u refused "
		  "(crosslane-priced-no-gain %u -> %u)\n",
		  gimple_uid (tail), before, after);
	    continue;
	  }

	/* Lane-state proof across the whole chain span.  */
	if (!lane_state_before (chain.links.front ()))
	  {
	    DUMP ("crosslane: rotate chain at uid %u refused "
		  "(crosslane-lane-state-unproven)\n",
		  gimple_uid (chain.links.front ()));
	    continue;
	  }
	bool span_ok = true;
	for (gimple_stmt_iterator s = gsi_for_stmt (chain.links.front ());
	     gsi_stmt (s) != (gimple *) tail && span_ok; gsi_next (&s))
	  {
	    stmt_class c = classify_stmt (gsi_stmt (s));
	    if (c != SC_SKIP && c != SC_SAFE)
	      span_ok = false;
	  }
	if (!span_ok)
	  {
	    DUMP ("crosslane: rotate chain at uid %u refused "
		  "(crosslane-lane-state-unproven: span)\n",
		  gimple_uid (chain.links.front ()));
	    continue;
	  }

	tree fwd = m == 0 ? chain.source
			  : gimple_call_lhs (chain.links[m - 1]);
	DUMP ("crosslane: rotate chain collapse %u -> %u links at uid %u "
	      "(priced %u -> %u slots)\n", n, m, gimple_uid (tail),
	      before, after);
	tree tail_lhs = gimple_call_lhs (tail);
	replace_uses_by (tail_lhs, fwd);
	for (unsigned i = n; i-- > m;)
	  delete_stmt (chain.links[i]);
	/* The iterator may have been invalidated for this BB walk; the
	   deleted statements were all at or after GSI's position, and
	   gsi still points at STMT only if STMT survived.  Restart the
	   BB scan to stay safe.  */
	gsi = gsi_start_bb (bb);
      }
}

/* ------------------------------------------------------------------ */
/* R2: slide re-lowering (the per-arch capability split; the surface's
   own subvec_slideup split is the precedent).

   Canonical WH-portable slide form (subvec_slideup's rotate+mask arm,
   and any hand spelling of the same shape):

     t1..tK = ror1 chain (K links)
     PUSHC; m = XVIF; t = READLREG(15); s = SHFT_I(t, -1);
     msk = LOADI(7); col = AND(s, msk); c = XICMPS(col, K, LT);
     XCONDB(c, m); z = LOADI(0); out = ASSIGN_LV(tK, z); POPC

   i.e. rotate the row right K times and zero columns < K under a
   lane_col() predicate (vConstTileId == 2*lane, so col = (tileid >>
   1) & 7 -- the architectural lane-geometry derivation).  On targets
   whose capability table carries SUBVEC_SHFLSHR1 this equals K
   zero-fill shifts (SFPSHFT2.md: VD[lane] = lane&7 ? VC[lane-1] : 0),
   dropping the whole predicated-zero region.  On Wormhole the shift is
   the documented hardware bug and the rewrite refuses by name
   (crosslane-shflshr1-unsupported) -- the canonical form IS the WH
   lowering.  */

struct slide_match
{
  std::vector<gcall *> links;	/* the ror1 chain, length K  */
  unsigned k;
  /* The predicated-zero region statements, in order.  */
  std::vector<gimple *> region;
  gcall *out_assign;		/* out = assign_lv (tail, z)  */
};

/* Match the predicated-zero region beginning right after TAIL.  */

static bool
match_slide_region (gcall *tail, unsigned k, slide_match *m)
{
  basic_block bb = gimple_bb (tail);
  gimple_stmt_iterator gsi = gsi_for_stmt (tail);

  auto next_call = [&] (rvtt_insn_data::insn_id want) -> gcall *
  {
    while (true)
      {
	gsi_next (&gsi);
	if (gsi_end_p (gsi))
	  return nullptr;
	gimple *s = gsi_stmt (gsi);
	if (classify_stmt (s) == SC_SKIP)
	  continue;
	gcall *c = dyn_cast <gcall *> (s);
	if (!c)
	  return nullptr;
	const rvtt_insn_data *d = rvtt_get_insn_data (c);
	if (!d || d->id != want)
	  return nullptr;
	return c;
      }
  };

  /* Flat post-CC-lowering form of `v_if (lane_col () < K) v = 0':

       t = READLREG (15)                 [vConstTileId == 2*lane]
       s = SFPSHFT_I (t, -1)             [column pre-mask]
       msk = SFPLOADI (7)
       col = SFPAND (s, msk)             [column = (tileid >> 1) & 7]
       SFPXIADD_I (col, K, mod 8)        [CC := col < K, no result]
       z = READLREG (9) | SFPLOADI (0)   [the bit-pattern zero]
       out = ASSIGN_LV (tail, z)
       SFPENCC all-lanes                 [region exit]  */
  gcall *readl = next_call (rvtt_insn_data::sfpreadlreg);
  unsigned reg;
  if (!readl || !const_uarg (readl, 0, &reg) || reg != 15)
    return false;		/* vConstTileId == 2*lane (sfpi.h) */
  gcall *shft = next_call (rvtt_insn_data::sfpshft_i);
  unsigned shimm;
  if (!shft || resolve_value (gimple_call_arg (shft, 1)) != gimple_call_lhs (readl)
      || !const_uarg (shft, 2, &shimm) || shimm != 0xffffffffu)
    return false;		/* tileid >> 1 */
  gcall *mask = next_call (rvtt_insn_data::sfploadi);
  unsigned maskimm;
  if (!mask || !const_uarg (mask, 1, &maskimm) || maskimm != 7)
    return false;
  gcall *andc = next_call (rvtt_insn_data::sfpand);
  if (!andc)
    return false;
  {
    tree a0 = resolve_value (gimple_call_arg (andc, 0));
    tree a1 = resolve_value (gimple_call_arg (andc, 1));
    tree sh = gimple_call_lhs (shft), mk = gimple_call_lhs (mask);
    if (!((a0 == sh && a1 == mk) || (a0 == mk && a1 == sh)))
      return false;
  }
  gcall *cmp = next_call (rvtt_insn_data::sfpxiadd_i);
  unsigned imm, mod;
  if (!cmp
      || resolve_value (gimple_call_arg (cmp, 1)) != gimple_call_lhs (andc)
      || !const_uarg (cmp, 2, &imm) || imm != k
      || !const_uarg (cmp, 5, &mod) || mod != 8)
    return false;		/* col < K (canonical LT-compare CC set) */
  /* The zero: a literal zero SFPLOADI or a read of the architectural
     constant-0 register LReg[9].  */
  gimple_stmt_iterator save = gsi;
  gcall *zero = next_call (rvtt_insn_data::sfpreadlreg);
  unsigned zimm;
  if (zero && (!const_uarg (zero, 0, &zimm) || zimm != 9))
    zero = nullptr;
  if (!zero)
    {
      gsi = save;
      zero = next_call (rvtt_insn_data::sfploadi);
      if (!zero || !const_uarg (zero, 1, &zimm) || zimm != 0)
	return false;
    }
  gcall *assign = next_call (rvtt_insn_data::sfpassign_lv);
  if (!assign
      || gimple_call_arg (assign, 0) != gimple_call_lhs (tail)
      || resolve_value (gimple_call_arg (assign, 1)) != gimple_call_lhs (zero))
    return false;
  gcall *encc = next_call (rvtt_insn_data::sfpencc);
  if (!encc || !encc_all_lanes_call_p (encc, rvtt_get_insn_data (encc)))
    return false;

  m->k = k;
  m->out_assign = assign;
  m->region = { readl, shft, mask, andc, cmp, zero, assign, encc };
  gcc_assert (gimple_bb (encc) == bb);
  return true;
}

void
crosslane_transform::relower_slides ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	tree src = ror1_link_p (stmt);
	if (!src)
	  continue;
	if (TREE_CODE (src) == SSA_NAME && has_single_use (src))
	  {
	    gimple *def = SSA_NAME_DEF_STMT (src);
	    if (ror1_link_p (def) && gimple_bb (def) == bb)
	      continue;		/* not a chain head */
	  }

	/* Collect the chain (post-R1 it is already minimal).  */
	slide_match m;
	gcall *cur = as_a <gcall *> (stmt);
	while (true)
	  {
	    m.links.push_back (cur);
	    tree lhs = gimple_call_lhs (cur);
	    if (!lhs || !has_single_use (lhs))
	      break;
	    use_operand_p use_p;
	    gimple *use;
	    if (!single_imm_use (lhs, &use_p, &use)
		|| gimple_bb (use) != bb)
	      break;
	    tree next_src = ror1_link_p (use);
	    if (!next_src || next_src != lhs)
	      break;
	    cur = as_a <gcall *> (use);
	  }
	unsigned k = m.links.size ();
	if (k == 0 || k >= 8)
	  continue;

	if (!match_slide_region (m.links.back (), k, &m))
	  continue;

	/* Recognized: this IS the canonical slide.  Capability split.  */
	if (!m_caps->shflshr1)
	  {
	    DUMP ("crosslane: slide<%u> at uid %u refused "
		  "(crosslane-shflshr1-unsupported: WormholeB0 SFPSHFT2.md "
		  "SUBVEC_SHFLSHR1 lane-0 UnpredictableValue)\n",
		  k, gimple_uid (m.links.front ()));
	    continue;
	  }
	if (!lane_state_before (m.links.front ()))
	  {
	    DUMP ("crosslane: slide<%u> at uid %u refused "
		  "(crosslane-lane-state-unproven)\n",
		  k, gimple_uid (m.links.front ()));
	    continue;
	  }
	/* Price: keep K shuffle slots, drop the predicated-zero region
	   (its two CC words, the three-op column computation, the zero
	   materialization and the merge).  */
	unsigned before = k * m_caps->shft2_shuffle_slots + 8;
	unsigned after = k * m_caps->shft2_shuffle_slots;
	DUMP ("crosslane: slide<%u> re-lowered to SUBVEC_SHFLSHR1 chain at "
	      "uid %u (priced %u -> %u slots)\n", k,
	      gimple_uid (m.links.front ()), before, after);

	/* Flip each link's mod operand to the zero-fill shift.  */
	for (gcall *link : m.links)
	  {
	    const rvtt_insn_data *d = rvtt_get_insn_data (link);
	    unsigned modpos
	      = d->id == rvtt_insn_data::sfpshft2_subvec_shfl1 ? 1 : 2;
	    gimple_call_set_arg (link, modpos,
				 build_int_cst (unsigned_type_node,
						SHFT2_MOD1_SHR1));
	    update_stmt (link);
	  }
	/* Forward the merged result to the chain tail and delete the
	   region.  */
	tree out = gimple_call_lhs (m.out_assign);
	if (out)
	  replace_uses_by (out, gimple_call_lhs (m.links.back ()));
	for (unsigned i = m.region.size (); i-- > 0;)
	  delete_stmt (m.region[i]);
	m_changed = true;
	gsi = gsi_start_bb (bb);
      }
}

/* ------------------------------------------------------------------ */
/* R3: swap refolding.

   Value-form SFPSWAP algebra (SFPSWAP.md; FB battery: swap mask table
   and both tie models):

   - mods 1..8 (min/max families): re-exchanging an exchanged pair in
     the SAME operand roles is the identity on the results.  Bitwise
     exact under BOTH tie models: equal values swap to equal values.
     Per-lane idempotent whatever the enable state, so the only
     obligation is that the enable state cannot CHANGE between the two
     exchanges (crosslane-cc-window).
   - mod 0 (unconditional lanewise swap): a second swap of the first
     swap's results cancels both; the pair's uses are served by the
     original operands.  Same CC-window obligation.

   Indexed (ENABLE_DEST_INDEX) forms: mod 0 cancels exactly like the
   value form (no comparison, no tie arm).  Mods 1..8 REFUSE BY NAME
   (crosslane-kv-refold-tie-unadjudicated): under the doc tie model a
   second exchange moves equal-key companions again, and the doc-vs-sim
   divergence is unadjudicated (lane FB finding).  */

void
crosslane_transform::refold_swaps ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	if (!call)
	  continue;
	const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	if (!insnd)
	  continue;
	bool indexed = insnd->id == rvtt_insn_data::sfpswap_indexed;
	if (insnd->id != rvtt_insn_data::sfpswap && !indexed)
	  continue;
	unsigned nsel = indexed ? 4 : 2;
	unsigned modpos = indexed ? 4 : 2;
	unsigned mod2;
	if (!const_uarg (call, modpos, &mod2) || mod2 > 8)
	  continue;

	/* Resolve every operand to a select on one earlier swap.  */
	gcall *first = nullptr;
	unsigned sel_of_arg[4] = { ~0u, ~0u, ~0u, ~0u };
	bool shape = true;
	for (unsigned i = 0; i != nsel && shape; ++i)
	  {
	    tree v = resolve_value (gimple_call_arg (call, i));
	    if (TREE_CODE (v) != SSA_NAME)
	      { shape = false; break; }
	    gcall *sel = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (v));
	    const rvtt_insn_data *seld = sel ? rvtt_get_insn_data (sel)
					     : nullptr;
	    if (!seld
		|| seld->id != (indexed ? rvtt_insn_data::sfpselect4
				        : rvtt_insn_data::sfpselect2))
	      { shape = false; break; }
	    tree tup = gimple_call_arg (sel, 0);
	    if (TREE_CODE (tup) != SSA_NAME)
	      { shape = false; break; }
	    gcall *sw = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (tup));
	    const rvtt_insn_data *swd = sw ? rvtt_get_insn_data (sw) : nullptr;
	    if (!swd || swd->id != insnd->id || gimple_bb (sw) != bb)
	      { shape = false; break; }
	    if (!first)
	      first = sw;
	    else if (first != sw)
	      { shape = false; break; }
	    unsigned idx;
	    if (!const_uarg (sel, 1, &idx) || idx >= nsel)
	      { shape = false; break; }
	    sel_of_arg[i] = idx;
	  }
	if (!shape || !first)
	  continue;
	unsigned mod1;
	if (!const_uarg (first, modpos, &mod1) || mod1 != mod2)
	  continue;

	/* Same-role repeat (arg i carries select i) or the mod-0
	   cancelling pair (any complete role assignment: a mod-0 swap
	   of the first swap's two results in either order restores the
	   original pair; the select mapping below is exact).  */
	bool same_role = true;
	for (unsigned i = 0; i != nsel; ++i)
	  same_role &= sel_of_arg[i] == i;
	bool complete = true;
	{
	  unsigned seen = 0;
	  for (unsigned i = 0; i != nsel; ++i)
	    seen |= 1u << sel_of_arg[i];
	  complete = seen == (1u << nsel) - 1;
	}
	if (mod2 != 0 && !same_role)
	  continue;
	if (mod2 == 0 && !complete)
	  continue;

	if (indexed && mod2 != 0)
	  {
	    DUMP ("crosslane: indexed swap refold at uid %u refused "
		  "(crosslane-kv-refold-tie-unadjudicated: SFPSWAP.md "
		  "sign-keyed tie swap vs pinned-sim compare -- companion "
		  "movement on equal keys unproven)\n", gimple_uid (call));
	    continue;
	  }

	if (!cc_unchanged_between (first, call))
	  {
	    DUMP ("crosslane: swap refold at uid %u refused "
		  "(crosslane-cc-window)\n", gimple_uid (call));
	    continue;
	  }

	/* Forward each of CALL's selects.  For the same-role repeat,
	   select i of CALL == select i of FIRST == CALL's operand i.
	   For the mod-0 cancelling pair, select j of CALL is FIRST's
	   ORIGINAL operand: a mod-0 swap returns (b, a) for (a, b), so
	   CALL's select 0 is its own operand carrying FIRST's select 1
	   ... i.e. select j of CALL == the operand of CALL whose
	   sel_of_arg is (j ^ 1) == FIRST's original arg (j ^ 1) ^ 1.
	   Both reduce to serving CALL's users from already-live values;
	   compute the forwarding per select index directly.  */
	tree call_lhs = gimple_call_lhs (call);
	if (!call_lhs)
	  continue;
	/* Pre-validate every user and its forwarding value before any
	   mutation: each must be a constant-index select of this call.  */
	std::vector<std::pair<gcall *, tree>> users;
	{
	  imm_use_iterator it;
	  gimple *use;
	  bool bad = false;
	  FOR_EACH_IMM_USE_STMT (use, it, call_lhs)
	    {
	      gcall *sel = dyn_cast <gcall *> (use);
	      const rvtt_insn_data *seld = sel ? rvtt_get_insn_data (sel)
					       : nullptr;
	      unsigned j;
	      if (!seld
		  || seld->id != (indexed ? rvtt_insn_data::sfpselect4
					  : rvtt_insn_data::sfpselect2)
		  || !const_uarg (sel, 1, &j) || j >= nsel)
		{
		  bad = true;
		  break;
		}
	      tree fwd;
	      if (mod2 != 0)
		/* Idempotent: result j == CALL's operand j.  */
		fwd = gimple_call_arg (call, j);
	      else
		{
		  /* Cancelling: a mod-0 swap of (x, y) returns (y, x),
		     so CALL's select j carries CALL's operand (j ^ 1)
		     -- which is FIRST's select sel_of_arg[j ^ 1] -- and
		     that select of FIRST is FIRST's operand
		     (sel_of_arg[j ^ 1] ^ 1): the original pre-swap
		     value.  */
		  unsigned k = sel_of_arg[(j ^ 1) & (nsel - 1)] ^ 1;
		  fwd = gimple_call_arg (first, k & (nsel - 1));
		}
	      users.push_back ({ sel, fwd });
	    }
	  if (bad)
	    continue;
	}

	DUMP ("crosslane: %s refold at uid %u -> forwarding to uid %u "
	      "(mod %u, priced %u -> 0 slots)\n",
	      mod2 == 0 ? "swap-pair cancel" : "swap idempotence",
	      gimple_uid (call), gimple_uid (first), mod2,
	      m_caps->swap_slots);

	for (auto &u : users)
	  {
	    tree sel_lhs = gimple_call_lhs (u.first);
	    if (sel_lhs && u.second)
	      replace_uses_by (sel_lhs, u.second);
	    delete_stmt (u.first);
	  }
	delete_stmt (call);
	/* The cancelling pair leaves FIRST's selects (and FIRST itself)
	   dead -- the generic DCE has already run, so sweep them here.  */
	tree first_lhs = gimple_call_lhs (first);
	if (first_lhs && TREE_CODE (first_lhs) == SSA_NAME)
	  {
	    std::vector<gimple *> dead;
	    imm_use_iterator it;
	    gimple *use;
	    FOR_EACH_IMM_USE_STMT (use, it, first_lhs)
	      {
		/* A select whose result is gone (deleting its consumer
		   released the name, leaving an lhs-less call) or dead
		   is swept with it.  */
		const rvtt_insn_data *ud
		  = call_insnd (use);
		if (!ud
		    || (ud->id != rvtt_insn_data::sfpselect2
			&& ud->id != rvtt_insn_data::sfpselect4))
		  continue;
		tree l = gimple_get_lhs (use);
		if (!l || (TREE_CODE (l) == SSA_NAME && has_zero_uses (l)))
		  dead.push_back (use);
	      }
	    for (gimple *d : dead)
	      delete_stmt (d);
	    if (has_zero_uses (first_lhs))
	      delete_stmt (first);
	  }
	gsi = gsi_start_bb (bb);
      }
}

/* ------------------------------------------------------------------ */
/* R4/R5 shared machinery: the canonical transp8 frame.

   The audited dual-bank transpose graduates through one inline frame
   (sfpi_crosslane.h transp8):

     r = SFPTRANSP8 (v0, v1, v2, v3, c0, c1, c2, c3)
     select4 (r, 0..3)  [each optionally threaded via assign_lv]
     readlreg (4..7)    [each optionally threaded via assign_lv]

   The frame's eight outputs are the threaded lhs values; its inputs
   are the call operands.  */

struct transp8_frame
{
  gcall *call;
  std::vector<gimple *> stmts;	/* every constituent, call included */
  tree in[8];
  tree out[8];			/* value-carrying lhs per position */
  gimple *last;			/* last constituent in program order */
};

static bool
match_transp8_frame (gcall *call, transp8_frame *fr)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfptransp8)
    return false;
  if (gimple_call_num_args (call) != 8 || !gimple_call_lhs (call))
    return false;
  fr->call = call;
  fr->stmts.clear ();
  fr->stmts.push_back (call);
  for (unsigned i = 0; i != 8; ++i)
    {
      fr->in[i] = gimple_call_arg (call, i);
      fr->out[i] = NULL_TREE;
    }
  fr->last = call;

  basic_block bb = gimple_bb (call);
  tree tup = gimple_call_lhs (call);

  /* Bank A: the four selects on the tuple.  */
  {
    imm_use_iterator it;
    gimple *use;
    FOR_EACH_IMM_USE_STMT (use, it, tup)
      {
	gcall *sel = dyn_cast <gcall *> (use);
	const rvtt_insn_data *seld = sel ? rvtt_get_insn_data (sel) : nullptr;
	if (!seld || seld->id != rvtt_insn_data::sfpselect4
	    || gimple_bb (sel) != bb)
	  return false;
	unsigned idx;
	if (!const_uarg (sel, 1, &idx) || idx >= 4 || fr->out[idx])
	  return false;
	tree v = gimple_call_lhs (sel);
	fr->stmts.push_back (sel);
	v = thread_assign (v, bb, &fr->stmts);
	if (!v)
	  return false;
	fr->out[idx] = v;
      }
  }
  for (unsigned i = 0; i != 4; ++i)
    if (!fr->out[i])
      return false;

  /* Bank B: the readlreg (4..7) reads following the call.  Dead
     companion outputs are DCE'd before this pass runs, so between zero
     and four reads appear; collect until the first statement that is
     neither a frame member, nor skippable, nor a fresh companion read
     (anything else could redefine the companion bank).  Missing reads
     leave their out[] slot null (an unused output).  */
  gimple_stmt_iterator gsi = gsi_for_stmt (call);
  while (true)
    {
      gsi_next (&gsi);
      if (gsi_end_p (gsi))
	break;
      gimple *s = gsi_stmt (gsi);
      if (classify_stmt (s) == SC_SKIP)
	continue;
      gcall *c = dyn_cast <gcall *> (s);
      const rvtt_insn_data *d = c ? rvtt_get_insn_data (c) : nullptr;
      if (d && d->id == rvtt_insn_data::sfpreadlreg)
	{
	  unsigned reg;
	  if (!const_uarg (c, 0, &reg))
	    break;
	  if (reg >= 4 && reg <= 7 && !fr->out[reg])
	    {
	      tree v = gimple_call_lhs (c);
	      fr->stmts.push_back (c);
	      v = thread_assign (v, gimple_bb (call), &fr->stmts);
	      if (!v)
		return false;
	      fr->out[reg] = v;
	      continue;
	    }
	  break;
	}
      /* Selects/assigns of this frame pass through.  */
      if (c && std::find (fr->stmts.begin (), fr->stmts.end (), (gimple *) c)
	       != fr->stmts.end ())
	continue;
      break;
    }
  /* Frame's last constituent: latest in program order.  */
  for (gimple *s : fr->stmts)
    if (gimple_uid (s) > gimple_uid (fr->last))
      fr->last = s;
  return true;
}

/* Any readlreg(4..7) in the function OUTSIDE recognized frames makes
   companion-state rewrites unsound (deleting a transpose changes what
   those reads observe).  */

bool
crosslane_transform::companion_escape_p ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *c = dyn_cast <gcall *> (gsi_stmt (gsi));
	const rvtt_insn_data *d = c ? rvtt_get_insn_data (c) : nullptr;
	if (!d || d->id != rvtt_insn_data::sfpreadlreg)
	  continue;
	unsigned reg;
	if (!const_uarg (c, 0, &reg) || reg < 4 || reg > 7)
	  continue;
	if (!m_frame_stmts.contains (gsi_stmt (gsi)))
	  return true;
      }
  return false;
}

/* R4: cancel adjacent involution pairs.  F2's eight inputs resolve to
   F1's eight outputs positionally; SFPTRANSP o SFPTRANSP == identity on
   both banks (SFPTRANSP.md functional model; FB battery: transpose
   inverse).  F2's outputs are served by F1's INPUT values; F2 is
   deleted; F1 follows when its outputs die with F2.  All-lanes proof
   required (mixed-enable transposes are not involutions), plus a
   lane-preserving span between the frames.  */

void
crosslane_transform::cancel_transp8_pairs ()
{
 restart:
  /* Parse all frames first (also feeds the companion-escape gate).
     After a successful cancellation the cached frames' operand trees
     may reference released SSA names, so re-parse from scratch.  */
  std::vector<transp8_frame> frames;
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *c = dyn_cast <gcall *> (gsi_stmt (gsi));
	if (!c)
	  continue;
	transp8_frame fr;
	if (match_transp8_frame (c, &fr))
	  {
	    for (gimple *s : fr.stmts)
	      note_frame_stmt (s);
	    frames.push_back (fr);
	  }
      }
  if (frames.size () < 2)
    return;
  if (companion_escape_p ())
    {
      DUMP ("crosslane: transp8 involution refused "
	    "(crosslane-companion-escape: companion-bank read outside "
	    "recognized frames)\n");
      return;
    }

  for (unsigned i = 0; i + 1 < frames.size (); ++i)
    {
      transp8_frame &f1 = frames[i];
      transp8_frame &f2 = frames[i + 1];
      if (gimple_bb (f1.call) != gimple_bb (f2.call))
	continue;
      bool feeds = true;
      for (unsigned p = 0; p != 8 && feeds; ++p)
	feeds = f1.out[p]
		&& resolve_value (f2.in[p]) == resolve_value (f1.out[p]);
      if (!feeds)
	continue;
      if (!lane_state_before (f1.call))
	{
	  DUMP ("crosslane: transp8 involution at uid %u refused "
		"(crosslane-lane-state-unproven)\n", gimple_uid (f1.call));
	  continue;
	}
      /* Span between F1's last constituent and F2's call must preserve
	 the lane state and the companion bank (SAFE excludes further
	 transposes by construction: they parse as frames).  */
      bool ok = true;
      for (gimple_stmt_iterator s = gsi_for_stmt (f1.last); ok;)
	{
	  gsi_next (&s);
	  if (gsi_end_p (s))
	    {
	      ok = false;
	      break;
	    }
	  gimple *st = gsi_stmt (s);
	  if (st == f2.call)
	    break;
	  if (std::find (f2.stmts.begin (), f2.stmts.end (), st)
	      != f2.stmts.end ())
	    continue;
	  stmt_class cl = classify_stmt (st);
	  if (cl != SC_SKIP && cl != SC_SAFE)
	    ok = false;
	  /* A transp8 between the pair would break the companion
	     threading; it parses as a frame and its call is not SAFE
	     positionally reachable here because F2's inputs resolved to
	     F1's outputs -- keep the refusing default anyway.  */
	  gcall *cc = dyn_cast <gcall *> (st);
	  const rvtt_insn_data *dd = cc ? rvtt_get_insn_data (cc) : nullptr;
	  if (dd && (dd->id == rvtt_insn_data::sfptransp8
		     || dd->id == rvtt_insn_data::sfptransp))
	    ok = false;
	}
      if (!ok)
	{
	  DUMP ("crosslane: transp8 involution at uid %u refused "
		"(crosslane-lane-state-unproven: span)\n",
		gimple_uid (f1.call));
	  continue;
	}

      DUMP ("crosslane: transp8 involution cancel at uids %u/%u "
	    "(priced %u -> 0 slots)\n", gimple_uid (f1.call),
	    gimple_uid (f2.call), 2 * m_caps->transp_slots);

      /* Forward F2's outputs to F1's input values.  */
      for (unsigned p = 0; p != 8; ++p)
	if (f2.out[p] && TREE_CODE (f2.out[p]) == SSA_NAME)
	  replace_uses_by (f2.out[p], resolve_value (f1.in[p]));
      /* Delete F2 wholesale (reverse program order).  */
      std::sort (f2.stmts.begin (), f2.stmts.end (),
		 [] (gimple *a, gimple *b)
		 { return gimple_uid (a) > gimple_uid (b); });
      for (gimple *s : f2.stmts)
	delete_stmt (s);
      /* Delete F1 when its outputs are now dead.  */
      bool dead = true;
      for (unsigned p = 0; p != 8 && dead; ++p)
	dead = !f1.out[p] || TREE_CODE (f1.out[p]) != SSA_NAME
	       || has_zero_uses (f1.out[p]);
      if (dead)
	{
	  std::sort (f1.stmts.begin (), f1.stmts.end (),
		     [] (gimple *a, gimple *b)
		     { return gimple_uid (a) > gimple_uid (b); });
	  for (gimple *s : f1.stmts)
	    delete_stmt (s);
	}
      goto restart;		/* cached frame operands may be stale */
    }
}

/* ------------------------------------------------------------------ */
/* R5: zip-chain collapse.

   The canonical zip frame (sfpi_crosslane.h rowvec_zip: stage the bank
   as (a, b, a, b) with zeroed companions, one dual-bank transpose,
   then a row>=2-predicated unconditional pair swap):

     z0..z3 = LOADI(0) x4
     r = SFPTRANSP8 (a, b, a, b, z0..z3)   [+ selects/readlreg frame]
     PUSHC; m = XVIF; t = READLREG(15); s = SHFT_I(t, -4);
     c = XICMPS(s, 2, GE); XCONDB(c, m);
     SWAP(v0, v1, 0) [+ selects/assigns]; SWAP(v2, v3, 0) [...]
     POPC
     a' = ASSIGN_LV(_, v0-final); b' = ASSIGN_LV(_, v2-final)

   As a permutation of the 8 subvector rows of (a, b) the zip is the
   out-riffle sigma = [0 4 1 5 2 6 3 7], whose order is 3 (2^3 == 1 mod
   7): zip^2 == unzip (already the canonical unzip lowering; no rewrite)
   and zip^3 == identity (FB battery: zip/unzip inverses).  Chains of n
   >= 3 zips on the same pair collapse to n mod 3.  All-lanes proof
   required: the frame's predicated swap is relative to the enclosing
   enable state.  */

struct zip_frame
{
  transp8_frame t8;
  std::vector<gimple *> stmts;	/* every constituent incl. t8's */
  tree in_a, in_b;
  tree out_a, out_b;
  gimple *first, *last;
};

static bool
match_zip_frame (gcall *transp_call, zip_frame *zf)
{
  if (!match_transp8_frame (transp_call, &zf->t8))
    return false;
  transp8_frame &t8 = zf->t8;
  /* Staging pattern (a, b, a, b) with fresh zero companions.  */
  tree a = resolve_value (t8.in[0]);
  tree b = resolve_value (t8.in[1]);
  if (resolve_value (t8.in[2]) != a || resolve_value (t8.in[3]) != b)
    return false;
  /* Companion staging must be the bit-pattern zero: either a literal
     zero SFPLOADI or a read of LReg[9], the architectural constant-0
     register (simulator reset state, transp-involution audit table).  */
  std::vector<gimple *> zeros;
  for (unsigned i = 4; i != 8; ++i)
    {
      tree z = t8.in[i];
      if (TREE_CODE (z) != SSA_NAME)
	return false;
      gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (z));
      const rvtt_insn_data *d = def ? rvtt_get_insn_data (def) : nullptr;
      unsigned imm;
      bool zero_ok
	= d
	  && ((d->id == rvtt_insn_data::sfploadi
	       && const_uarg (def, 1, &imm) && imm == 0)
	      || (d->id == rvtt_insn_data::sfpreadlreg
		  && const_uarg (def, 0, &imm) && imm == 9));
      if (!zero_ok || gimple_bb (def) != gimple_bb (transp_call))
	return false;
      zeros.push_back (def);
    }

  zf->stmts = t8.stmts;
  for (gimple *z : zeros)
    zf->stmts.push_back (z);
  zf->in_a = a;
  zf->in_b = b;

  /* The row-predicated pair-swap region after the frame, in its flat
     post-CC-lowering form (the structured v_if has already been lowered
     by the rvtt expansion pipeline before this pass runs):

       t = READLREG (15)                [vConstTileId == 2*lane]
       s = SFPSHFT_I (t, -4)            [subvector row = tileid >> 4]
       SFPXIADD_I (s, 2, mod 9)         [CC := row >= 2, no result]
       SFPSWAP (v0, v1, 0) + select2(0) [+ optional select2(1)] threads
       SFPSWAP (v2, v3, 0) + likewise
       SFPENCC all-lanes                [region exit re-enable]  */
  basic_block bb = gimple_bb (transp_call);
  std::vector<gimple *> region;
  hash_set<gimple *> members;
  for (gimple *s : zf->stmts)
    members.add (s);
  gimple_stmt_iterator gsi = gsi_for_stmt (t8.last);
  auto next_call = [&] (rvtt_insn_data::insn_id want) -> gcall *
  {
    while (true)
      {
	gsi_next (&gsi);
	if (gsi_end_p (gsi))
	  return nullptr;
	gimple *s = gsi_stmt (gsi);
	if (classify_stmt (s) == SC_SKIP || members.contains (s))
	  continue;
	gcall *c = dyn_cast <gcall *> (s);
	if (!c)
	  return nullptr;
	const rvtt_insn_data *d = rvtt_get_insn_data (c);
	if (!d || d->id != want)
	  return nullptr;
	return c;
      }
  };

  gcall *readl = next_call (rvtt_insn_data::sfpreadlreg);
  unsigned reg;
  if (!readl || !const_uarg (readl, 0, &reg) || reg != 15)
    return false;
  gcall *shft = next_call (rvtt_insn_data::sfpshft_i);
  unsigned shimm;
  if (!shft
      || resolve_value (gimple_call_arg (shft, 1)) != gimple_call_lhs (readl)
      || !const_uarg (shft, 2, &shimm) || shimm != 0xfffffffcu)
    return false;		/* tileid >> 4 == lane_row */
  gcall *cmp = next_call (rvtt_insn_data::sfpxiadd_i);
  unsigned imm, mod;
  if (!cmp
      || resolve_value (gimple_call_arg (cmp, 1)) != gimple_call_lhs (shft)
      || !const_uarg (cmp, 2, &imm) || imm != 2
      || !const_uarg (cmp, 5, &mod) || mod != 9)
    return false;		/* row >= 2 (canonical GE-compare CC set) */
  region.push_back (readl);
  region.push_back (shft);
  region.push_back (cmp);

  /* Two mod-0 pair swaps on (out0, out1) and (out2, out3).  The
     surviving-value select (index 0) must exist; the partner select is
     optional (dead outputs are DCE'd before this pass).  */
  tree cur[4] = { t8.out[0], t8.out[1], t8.out[2], t8.out[3] };
  for (unsigned pair = 0; pair != 2; ++pair)
    {
      gcall *sw = next_call (rvtt_insn_data::sfpswap);
      unsigned m0;
      if (!sw || !const_uarg (sw, 2, &m0) || m0 != 0)
	return false;
      unsigned lo = pair * 2;
      if (resolve_value (gimple_call_arg (sw, 0)) != resolve_value (cur[lo])
	  || resolve_value (gimple_call_arg (sw, 1))
	     != resolve_value (cur[lo + 1]))
	return false;
      region.push_back (sw);
      members.add (sw);
      /* Its selects and assign threads.  */
      tree tup = gimple_call_lhs (sw);
      if (!tup)
	return false;
      imm_use_iterator it;
      gimple *use;
      tree newv[2] = { NULL_TREE, NULL_TREE };
      std::vector<gimple *> sel_stmts;
      FOR_EACH_IMM_USE_STMT (use, it, tup)
	{
	  gcall *sel = dyn_cast <gcall *> (use);
	  const rvtt_insn_data *seld = sel ? rvtt_get_insn_data (sel)
					   : nullptr;
	  if (!seld || seld->id != rvtt_insn_data::sfpselect2
	      || gimple_bb (sel) != bb)
	    return false;
	  unsigned idx;
	  if (!const_uarg (sel, 1, &idx) || idx > 1 || newv[idx])
	    return false;
	  tree v = gimple_call_lhs (sel);
	  sel_stmts.push_back (sel);
	  v = thread_assign (v, bb, &sel_stmts);
	  if (!v)
	    return false;
	  newv[idx] = v;
	}
      if (!newv[0])
	return false;
      for (gimple *s : sel_stmts)
	{
	  region.push_back (s);
	  members.add (s);
	}
      cur[lo] = newv[0];
      if (newv[1])
	cur[lo + 1] = newv[1];
    }

  /* Region exit: the word-exact all-lanes re-enable the structured-CC
     lowering places at region exits.  */
  gcall *encc = next_call (rvtt_insn_data::sfpencc);
  if (!encc || !encc_all_lanes_call_p (encc, rvtt_get_insn_data (encc)))
    return false;
  region.push_back (encc);

  /* The frame's outputs: the post-region versions of positions 0 and 2
     (a' = rows (a0, b0, a1, b1); b' = rows (a2, b2, a3, b3)), possibly
     re-threaded once more by the surface's final assignments.  */
  tree out_a = cur[0], out_b = cur[2];

  for (gimple *s : region)
    zf->stmts.push_back (s);
  out_a = thread_assign (out_a, bb, &zf->stmts);
  out_b = thread_assign (out_b, bb, &zf->stmts);
  zf->out_a = out_a;
  zf->out_b = out_b;

  zf->first = zf->stmts.front ();
  zf->last = zf->stmts.front ();
  for (gimple *s : zf->stmts)
    {
      if (gimple_uid (s) < gimple_uid (zf->first))
	zf->first = s;
      if (gimple_uid (s) > gimple_uid (zf->last))
	zf->last = s;
    }
  return true;
}

void
crosslane_transform::collapse_zip_chains ()
{
  /* Parse zip frames in program order per BB.  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    {
      std::vector<zip_frame> zips;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *c = dyn_cast <gcall *> (gsi_stmt (gsi));
	  const rvtt_insn_data *d = c ? rvtt_get_insn_data (c) : nullptr;
	  if (!d || d->id != rvtt_insn_data::sfptransp8)
	    continue;
	  zip_frame zf;
	  if (match_zip_frame (c, &zf))
	    {
	      for (gimple *s : zf.stmts)
		note_frame_stmt (s);
	      zips.push_back (zf);
	    }
	}
      if (zips.size () < 3)
	continue;
      if (companion_escape_p ())
	{
	  DUMP ("crosslane: zip-chain collapse refused "
		"(crosslane-companion-escape)\n");
	  continue;
	}

      /* Chain zips: next frame's inputs resolve to this frame's
	 outputs.  */
      unsigned start = 0;
      while (start < zips.size ())
	{
	  unsigned end = start;
	  while (end + 1 < zips.size ()
		 && resolve_value (zips[end + 1].in_a)
		    == resolve_value (zips[end].out_a)
		 && resolve_value (zips[end + 1].in_b)
		    == resolve_value (zips[end].out_b))
	    end++;
	  unsigned n = end - start + 1;
	  unsigned m = n % 3;
	  if (m == n)
	    {
	      start = end + 1;
	      continue;
	    }
	  /* Lane-state + span proof over the whole run.  */
	  if (!lane_state_before (zips[start].first))
	    {
	      DUMP ("crosslane: zip chain at uid %u refused "
		    "(crosslane-lane-state-unproven)\n",
		    gimple_uid (zips[start].first));
	      start = end + 1;
	      continue;
	    }
	  bool ok = true;
	  hash_set<gimple *> members;
	  for (unsigned i = start; i <= end; ++i)
	    for (gimple *s : zips[i].stmts)
	      members.add (s);
	  for (gimple_stmt_iterator s = gsi_for_stmt (zips[start].first);
	       ok && gsi_stmt (s) != zips[end].last; gsi_next (&s))
	    {
	      gimple *st = gsi_stmt (s);
	      if (members.contains (st))
		continue;
	      stmt_class cl = classify_stmt (st);
	      if (cl != SC_SKIP && cl != SC_SAFE)
		ok = false;
	    }
	  if (!ok)
	    {
	      DUMP ("crosslane: zip chain at uid %u refused "
		    "(crosslane-lane-state-unproven: span)\n",
		    gimple_uid (zips[start].first));
	      start = end + 1;
	      continue;
	    }

	  /* Collapse the last n - m zips (a suffix collapse keeps the
	     first m frames producing the required residue: zip^n ==
	     zip^m needs the DELETED frames to be a multiple of 3 --
	     delete the trailing 3*floor((n-m)/3) frames by forwarding
	     the final outputs to the (start+m-1)-th frame's outputs
	     (or the chain inputs when m == 0).  */
	  unsigned keep = m;
	  unsigned del_from = start + keep;

	  /* Use-exclusivity over the deleted suffix (lane FP audit,
	     FP-1): every value a to-be-deleted frame defines must be
	     consumed inside the chain.  An external tap keeps its
	     value-carrying producers alive through delete_stmt's use
	     guard, but the frame's lhs-less CC statements (the row>=2
	     SFPXIADD CC set, the region-exit SFPENCC) delete
	     unconditionally -- the surviving mod-0 SFPSWAP would then
	     execute under the enclosing all-lanes enable and swap every
	     row.  The final frame's own outputs are exempt: their uses
	     are forwarded before deletion.  */
	  bool exclusive = true;
	  for (unsigned i = del_from; i <= end && exclusive; ++i)
	    for (gimple *s : zips[i].stmts)
	      {
		tree lhs = gimple_get_lhs (s);
		if (!lhs || TREE_CODE (lhs) != SSA_NAME)
		  continue;
		if (lhs == zips[end].out_a || lhs == zips[end].out_b)
		  continue;
		imm_use_iterator it;
		gimple *use;
		bool escaped = false;
		FOR_EACH_IMM_USE_STMT (use, it, lhs)
		  if (!members.contains (use))
		    {
		      escaped = true;
		      break;
		    }
		if (escaped)
		  {
		    DUMP ("crosslane: zip chain at uid %u refused "
			  "(crosslane-frame-value-escape: deleted-frame "
			  "value has a consumer outside the chain)\n",
			  gimple_uid (zips[start].first));
		    exclusive = false;
		    break;
		  }
	      }
	  if (!exclusive)
	    {
	      start = end + 1;
	      continue;
	    }

	  tree fa = keep ? zips[start + keep - 1].out_a : zips[start].in_a;
	  tree fb = keep ? zips[start + keep - 1].out_b : zips[start].in_b;
	  unsigned frame_cost
	    = m_caps->transp_slots + 2 * m_caps->swap_slots + 10;
	  DUMP ("crosslane: zip chain collapse %u -> %u frames at uid %u "
		"(zip^3 == identity; priced %u -> %u slots)\n",
		n, m, gimple_uid (zips[start].first), n * frame_cost,
		m * frame_cost);
	  replace_uses_by (zips[end].out_a, resolve_value (fa));
	  replace_uses_by (zips[end].out_b, resolve_value (fb));
	  for (unsigned i = end + 1; i-- > del_from;)
	    {
	      std::sort (zips[i].stmts.begin (), zips[i].stmts.end (),
			 [] (gimple *a, gimple *b)
			 { return gimple_uid (a) > gimple_uid (b); });
	      for (gimple *s : zips[i].stmts)
		delete_stmt (s);
	    }
	  start = end + 1;
	}
    }
}

/* ------------------------------------------------------------------ */

bool
crosslane_transform::run ()
{
  compute_lane_states ();
  collapse_rotate_chains ();
  relower_slides ();
  refold_swaps ();
  cancel_transp8_pairs ();
  collapse_zip_chains ();
  return m_changed;
}

const pass_data pass_data_rvtt_crosslane =
{
  GIMPLE_PASS, /* type */
  "rvtt_crosslane", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  TODO_update_ssa, /* todo_flags_finish */
};

class pass_rvtt_crosslane : public gimple_opt_pass
{
public:
  pass_rvtt_crosslane (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_crosslane, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_crosslane > 0;
  }

  unsigned int execute (function *fn) final override
  {
    const crosslane_caps *caps = get_crosslane_caps ();
    if (!caps)
      {
	DUMP ("crosslane: function refused "
	      "(crosslane-unsupported-target: no pinned-simulator proof "
	      "battery for this Tensix variant)\n");
	return 0;
      }
    renumber_gimple_stmt_uids (fn);
    crosslane_transform xf (fn, caps);
    if (xf.run ())
      return TODO_update_ssa;
    return 0;
  }
}; /* class pass_rvtt_crosslane */

} /* anon namespace */

gimple_opt_pass *
make_pass_rvtt_crosslane (gcc::context *ctxt)
{
  return new pass_rvtt_crosslane (ctxt);
}
