/* Tensix replay formation: counted-row canonicalization
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

/* Counted-row formation (canonicalize_counted_rows and its crf_*
   machinery): re-forms textually rotated counted-loop rows into
   the canonical capturable shape under the lockstep, occupancy,
   delay-shadow and final-lockstep audits.  Split from
   rtl-rvtt-replay.cc; the algorithm essay lives there.  */

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

/* ==== Counted-row parameterized formation (canonicalization phase) ====

   docs/COUNTED_ROW_FORMATION.md.  The word-exact sequence discovery above
   cannot form a record over template-expanded "rows" that repeat modulo
   (a) per-row immediate materializations and (b) the register allocator's
   rotation of equivalent assignments.  This phase, gated by
   -mtt-tensix-optimize-counted-row-formation (default off), REWRITES such
   parameterized clone families into word-exact form so the existing
   discovery, budgeting, capture and launch machinery -- unchanged -- forms
   one parameterized row program:

   1. Invariant-violation exclusion.  A member position whose clones
      disagree only on compile-time-constant operands is EXCLUDED from the
      residual and moved to its clone's head, where it will be issued
      between launches.  The excluded class is derived from the invariance
      proof itself (the clones' reaching constant operands differ) plus the
      audited effect set: a single-slot materialization whose only SFPU
      dataflow is the write (and read-modify-write) of its single
      destination register and which carries no CC, Dst, RWC, or
      configuration effect.  No instruction identity participates.

   2. Clone canonicalization for register rotation.  Clones are matched
      against the first clone under an evolving register value map (the
      launch-conversion matcher's exact test, run as a transform): every
      hard-register value whose mapping differs is REWRITTEN to the
      recorded register -- the whole value, definition and every use, with
      a linear in-block value analysis proving the rewrite sound.  A
      live-in value pinned by a fixed multi-definition instruction is
      bridged with one all-lanes register move issued before the launch
      and priced as a slot.

   3. Slot-budget honesty.  Candidates are ranked by modeled slot saving
      (shorter residuals with more clones win ties); a family whose
      residual exceeds the largest available replay-buffer span refuses by
      name.  The budget never grows by stealing user-recorded slots.

   Refusals, each by name in the dump, leaving code byte-identical:
   counted-row-excluded-member-unmovable, counted-row-residual-not-uniform,
   counted-row-map-live-out, counted-row-slot-budget,
   counted-row-rename-interference, counted-row-rename-constraint,
   counted-row-lane-state, counted-row-bridge-clobber,
   counted-row-vacated-delay-shadow (a move may not re-open a
   delay shadow the nop inserter discharged against the original order;
   the SFPMAD.md stall-detection erratum list makes that wrong code
   on hardware).  */

/* LREG index domain helpers: xtt_effect_set masks are over L0..L15; the
   allocatable SFPU hard registers are L0..L7.  */
static inline uint32_t
crf_reg_bit (unsigned regno)
{
  gcc_checking_assert (SFPU_REG_P (regno));
  return 1u << (regno - SFPU_REG_FIRST);
}

/* A member admissible for exclusion from a parameterized record: a
   single-slot immediate materialization -- every non-register operand a
   compile-time constant, its only SFPU dataflow the write (and
   read-modify-write) of its single destination register, and its audited
   effect set free of CC, Dst, RWC, and configuration effects.  Derived
   from the effect audit and the cross-clone invariance proof; never from
   instruction identity.  */

static bool
crf_excludable_insn_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX
      || get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || get_attr_length (insn) != 4
      || !fixed_replay_rtx_p (pattern))
    return false;

  /* cc_read (the audited model of a lane-gated write) is admissible: the
     movement window is proven free of CC writes, so the lane state at the
     new position is the state at the old one.  A CC write is not.  */
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque || e.cc_write
      || e.config_dests_written || e.config_dests_read
      || e.addr_mod_slot_write
      || e.rwc.kind != xtt_rwc_effect_t::NONE
      || e.dst_mem_read || e.dst_mem_write)
    return false;

  uint32_t w = e.lreg_write & 0xFF;
  if (popcount_hwi (w) != 1)
    return false;
  if ((e.lreg_write & ~0xFFu) || (e.lreg_read & ~(uint32_t) w))
    return false;
  return true;
}

/* Classified SFPU register mentions of an rtx, by pattern position.  */

static void
crf_scan_rtx (rtx x, bool in_def, uint32_t *defs, uint32_t *uses,
	      bool *unhandled)
{
  switch (GET_CODE (x))
    {
    case REG:
      if (SFPU_REG_P (REGNO (x)))
	{
	  if (REG_NREGS (x) != 1)
	    *unhandled = true;
	  else
	    *(in_def ? defs : uses) |= crf_reg_bit (REGNO (x));
	}
      return;

    case SET:
      crf_scan_rtx (SET_SRC (x), false, defs, uses, unhandled);
      crf_scan_rtx (SET_DEST (x), true, defs, uses, unhandled);
      return;

    case CLOBBER:
      crf_scan_rtx (XEXP (x, 0), true, defs, uses, unhandled);
      return;

    case USE:
      crf_scan_rtx (XEXP (x, 0), false, defs, uses, unhandled);
      return;

    case MEM:
      /* Address registers are uses even under a store destination.  */
      crf_scan_rtx (XEXP (x, 0), false, defs, uses, unhandled);
      return;

    case SUBREG:
    case STRICT_LOW_PART:
    case ZERO_EXTRACT:
      /* Partial or indirect register access: not a whole-value def/use.  */
      {
	uint32_t d = 0, u = 0;
	crf_scan_rtx (XEXP (x, 0), false, &d, &u, unhandled);
	if (u | d)
	  *unhandled = true;
      }
      return;

    default:
      {
	const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
	for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
	  if (fmt[i] == 'e')
	    crf_scan_rtx (XEXP (x, i), in_def, defs, uses, unhandled);
	  else if (fmt[i] == 'E')
	    for (int j = XVECLEN (x, i); j--;)
	      crf_scan_rtx (XVECEXP (x, i, j), in_def, defs, uses, unhandled);
      }
      return;
    }
}

/* One whole-register value in the linear in-block dataflow model.  */

struct crf_value
{
  int def_pos = -1;		 /* -1: live into the block */
  rtx_insn *def_insn = nullptr;
  unsigned reg = 0;		 /* original hard register */
  std::vector<unsigned> use_positions;
  int last_pos = -1;
  bool live_out = false;	 /* consumed on some path after the block */
  bool fixed = false;		 /* multi-definition or hidden-effect def */
  bool poisoned = false;	 /* range crosses an opaque event or shadow */
  int renamed_to = -1;		 /* planned final hard register (-1 = keep) */
};

/* Per-position facts for the whole block.  */

struct crf_position
{
  rtx_insn *insn;
  uint32_t defs = 0, uses = 0;	 /* SFPU reg masks (pattern + audited extra) */
  bool eligible = false;	 /* may be a family member */
  bool excludable = false;	 /* admissible for invariant-violation
				    exclusion */
  bool barrier = false;		 /* breaks family runs */
  bool empty = false;		 /* zero-length marker: transparent */
  bool cc_write = false;
  bool opaque = false;
  uint32_t marker_mask = 0;	 /* zero-length LREG interface marker */
  unsigned phash = 0;		 /* parameterized structural hash */
  int value_of_def[8];		 /* value index defined per reg, or -1 */
  int value_of_use[8];		 /* value index consumed per reg, or -1 */
  crf_position () { memset (value_of_def, -1, sizeof (value_of_def));
		    memset (value_of_use, -1, sizeof (value_of_use)); }
};

struct crf_block
{
  basic_block bb;
  std::vector<crf_position> pos;
  std::vector<crf_value> values;
};

/* Parameterized structural hash: instruction code and full structure with
   SFPU register numbers abstracted, and with constant operands abstracted
   only for exclusion-admissible instructions (their constants never enter
   the record; every other constant is part of the recorded word).  */

static unsigned
crf_param_hash (rtx_insn *insn, bool excludable)
{
  auto hasher = [excludable] (auto &self, unsigned hash, rtx rtl) -> unsigned
  {
    hash = crc32_unsigned (hash, GET_CODE (rtl) + (GET_MODE (rtl) << 16));
    switch (GET_CODE (rtl))
      {
      case UNSPEC:
      case UNSPEC_VOLATILE:
	hash = crc32_unsigned (hash, XINT (rtl, 1));
	/* FALLTHROUGH */
      case PARALLEL:
	{
	  auto &vec = XVEC (rtl, 0);
	  for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
	    hash = self (self, hash, RTVEC_ELT (vec, ix));
	}
	break;

      case SET:
	hash = self (self, hash, SET_SRC (rtl));
	hash = self (self, hash, SET_DEST (rtl));
	break;

      case REG:
	if (!SFPU_REG_P (REGNO (rtl)))
	  hash = crc32_unsigned (hash, REGNO (rtl));
	break;

      case CONST_INT:
	if (!excludable)
	  hash = crc32_unsigned (hash, unsigned (INTVAL (rtl)));
	break;

      case CLOBBER:
      case USE:
	hash = self (self, hash, XEXP (rtl, 0));
	break;

      default:
	break;
      }
    return hash;
  };
  return hasher (hasher, recog_memoized (insn), PATTERN (insn));
}

/* Build the linear value model and per-position facts for BB.  Returns
   false when the block cannot be modeled (variable user capture).  */

static bool
crf_scan_block (basic_block bb, crf_block &blk)
{
  blk.bb = bb;
  blk.pos.clear ();
  blk.values.clear ();

  int cur[8];
  for (int i = 0; i != 8; ++i)
    cur[i] = -1;

  unsigned shadow = 0;

  auto value_at = [&] (unsigned regix, unsigned pos_ix) -> int
  {
    if (cur[regix] < 0)
      {
	/* Live into the block.  */
	blk.values.emplace_back ();
	crf_value &v = blk.values.back ();
	v.reg = SFPU_REG_FIRST + regix;
	v.def_pos = -1;
	cur[regix] = int (blk.values.size ()) - 1;
      }
    crf_value &v = blk.values[cur[regix]];
    v.use_positions.push_back (pos_ix);
    v.last_pos = int (pos_ix);
    return cur[regix];
  };

  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      blk.pos.emplace_back ();
      crf_position &p = blk.pos.back ();
      unsigned pos_ix = blk.pos.size () - 1;
      p.insn = insn;

      bool opaque_event = false;
      bool in_shadow = shadow > 0;

      if (CALL_P (insn) || GET_CODE (insn) == JUMP_INSN)
	opaque_event = CALL_P (insn);
      else if (asm_noperands (PATTERN (insn)) >= 0
	       || (GET_CODE (insn) == INSN && recog_memoized (insn) < 0))
	opaque_event = true;

      rtx pattern = PATTERN (insn);
      bool unhandled = false;
      if (!opaque_event)
	crf_scan_rtx (pattern, false, &p.defs, &p.uses, &unhandled);
      if (unhandled)
	opaque_event = true;

      bool tensix = GET_CODE (insn) == INSN && !opaque_event
	&& recog_memoized (insn) >= 0
	&& GET_CODE (pattern) != USE && GET_CODE (pattern) != CLOBBER
	&& get_attr_type (insn) == TYPE_TENSIX;

      uint32_t fixed_extra = 0;
      if (tensix)
	{
	  replay_span span;
	  auto rtype = get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER
	    ? is_replay_insn (span, insn) : REPLAY_none;
	  if (rtype != REPLAY_none)
	    {
	      if (rtype == REPLAY_variable_capture)
		return false;
	      if (rtype == REPLAY_fixed_capture)
		shadow = span.end;
	      p.barrier = true;
	    }
	  else
	    {
	      xtt_effect_set e = rvtt_insn_effects (insn);
	      if (e.opaque)
		opaque_event = true;
	      else
		{
		  p.cc_write = e.cc_write;
		  /* Audited architectural effects beyond the pattern's
		     registers: hidden dataflow, modeled as fixed def+use.  */
		  fixed_extra = ((e.lreg_write | e.lreg_read) & 0xFF)
		    & ~(p.defs | p.uses);
		  p.defs |= fixed_extra;
		  p.uses |= fixed_extra;
		}
	      p.empty = !get_attr_length (insn);
	      if (p.empty)
		rvtt_lreg_marker (insn, &p.marker_mask);
	      if (in_shadow && !p.empty)
		shadow--;
	    }
	}
      p.opaque = opaque_event;

      if (opaque_event)
	{
	  /* Unknown reads and writes: poison every live value, extend
	     their ranges, and act as a run barrier.  */
	  for (int i = 0; i != 8; ++i)
	    if (cur[i] >= 0)
	      {
		blk.values[cur[i]].poisoned = true;
		blk.values[cur[i]].use_positions.push_back (pos_ix);
		blk.values[cur[i]].last_pos = int (pos_ix);
	      }
	  p.barrier = true;
	  continue;
	}

      /* Uses consume the current values.  */
      for (int i = 0; i != 8; ++i)
	if (p.uses & (1u << i))
	  p.value_of_use[i] = value_at (i, pos_ix);

      /* Definitions begin new values.  */
      unsigned ndefs = popcount_hwi (p.defs);
      for (int i = 0; i != 8; ++i)
	if (p.defs & (1u << i))
	  {
	    blk.values.emplace_back ();
	    crf_value &v = blk.values.back ();
	    v.reg = SFPU_REG_FIRST + i;
	    v.def_pos = int (pos_ix);
	    v.def_insn = insn;
	    v.last_pos = int (pos_ix);
	    v.fixed = ndefs > 1 || (fixed_extra & (1u << i));
	    v.poisoned = in_shadow || (fixed_extra & (1u << i));
	    cur[i] = int (blk.values.size ()) - 1;
	    p.value_of_def[i] = cur[i];
	  }

      if (in_shadow)
	{
	  /* Values consumed by a user capture payload feed a recorded
	     program with launch sites this analysis cannot see.  */
	  for (int i = 0; i != 8; ++i)
	    if (p.value_of_use[i] >= 0)
	      blk.values[p.value_of_use[i]].poisoned = true;
	  p.barrier = true;
	  continue;
	}

      if (p.barrier || p.empty)
	continue;

      p.excludable = crf_excludable_insn_p (insn);
      if (p.excludable)
	/* Transparent to discovery: re-attached to the containing clone
	   at verification, moved to its head or tail by dataflow.  */
	continue;
      p.eligible = conv_run_insn_p (insn);
      if (p.eligible)
	p.phash = crf_param_hash (insn, false);
      else
	p.barrier = true;
    }

  /* Values still live at the block's end that some path consumes.  */
  for (int i = 0; i != 8; ++i)
    if (cur[i] >= 0)
      blk.values[cur[i]].live_out
	= conv_reg_consumed_after_p (SFPU_REG_FIRST + i, BB_END (bb), bb);

  return !blk.pos.empty ();
}

/* One clone of a parameterized family: a span of positions and the
   evolving register value map relating it to the family's first clone.  */

struct crf_clone
{
  unsigned begin, end;		 /* half-open position span */
  std::map<unsigned, unsigned> p2r, r2p; /* seed reg <-> clone reg */
  std::map<unsigned, bool> defined_p, defined_r;
};

struct crf_seq
{
  unsigned parent = 0;
  unsigned hash = 0;
  unsigned length = 0;		 /* members (excludable/empty not counted) */
  std::vector<crf_clone> clones; /* clones[0] is the seed */
};

/* Structural lockstep match of one seed/clone member pair under the
   evolving map.  Extends the launch-conversion matcher in exactly one
   way: a live-in register pair may differ, recorded in the map as a
   canonicalization requirement rather than failing.  Everything else --
   codes, modes, structure, constants, run-local value correspondence --
   must agree.  */

static bool
crf_match_rtx (rtx a, rtx b, bool in_def, crf_clone &map,
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
	  return pa == rb;
	if (in_def)
	  {
	    pending_defs.emplace_back (pa, rb);
	    return true;
	  }
	if (map.defined_p.count (pa) || map.defined_r.count (rb))
	  /* Run-local value: must be the corresponding definition.  */
	  return map.defined_p.count (pa) && map.defined_r.count (rb)
	    && map.p2r.count (pa) && map.p2r[pa] == rb
	    && map.r2p.count (rb) && map.r2p[rb] == pa;
	/* Live-in use: record the (possibly differing) correspondence.  */
	auto pi = map.p2r.find (pa), ri = map.r2p.find (rb);
	if (pi != map.p2r.end () || ri != map.r2p.end ())
	  return pi != map.p2r.end () && pi->second == rb
	    && ri != map.r2p.end () && ri->second == pa;
	map.p2r[pa] = rb;
	map.r2p[rb] = pa;
	return true;
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return crf_match_rtx (SET_SRC (a), SET_SRC (b), false, map,
			    pending_defs)
	&& crf_match_rtx (SET_DEST (a), SET_DEST (b), true, map,
			  pending_defs);

    case CLOBBER:
      return crf_match_rtx (XEXP (a, 0), XEXP (b, 0), true, map,
			    pending_defs);

    case USE:
    case MEM:
      return crf_match_rtx (XEXP (a, 0), XEXP (b, 0), false, map,
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
	  if (!crf_match_rtx (XVECEXP (a, 0, ix), XVECEXP (b, 0, ix),
			      in_def, map, pending_defs))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Lockstep-match seed member P against clone member R under MAP's
   evolving register correspondence (see the block comment above
   crf_match_rtx): same insn code, patterns structurally equal via
   crf_match_rtx.  Only on a whole-insn match are the pair's register
   definitions committed into MAP -- definitions take effect after all
   of the instruction's uses.  */

static bool
crf_match_insn (rtx_insn *p, rtx_insn *r, crf_clone &map)
{
  if (recog_memoized (p) != recog_memoized (r))
    return false;
  std::vector<std::pair<unsigned, unsigned>> pending_defs;
  if (!crf_match_rtx (PATTERN (p), PATTERN (r), false, map, pending_defs))
    return false;
  for (auto const &def : pending_defs)
    {
      map.p2r[def.first] = def.second;
      map.r2p[def.second] = def.first;
      map.defined_p[def.first] = true;
      map.defined_r[def.second] = true;
    }
  return true;
}

/* Transparent positions never become members: zero-length markers and
   exclusion-admissible materializations (the latter are re-attached to
   the clone that contains them at verification).  */

static inline bool
crf_transparent_p (crf_position const &p)
{
  return p.empty || p.excludable;
}

/* Grow parameterized sequences over the block, mirroring build_sequences'
   grow-by-one architecture with the lockstep matcher in place of word
   equality.  */

static void
crf_extend (std::map<unsigned, std::vector<unsigned>> &map,
	    std::vector<crf_seq> &list, crf_block &blk,
	    unsigned parent, unsigned length, unsigned begin, unsigned end)
{
  crf_position &p = blk.pos[end - 1];
  unsigned hash = parent ? crc32_unsigned (list[parent].hash, p.phash)
    : p.phash;

  auto slot = map.emplace (hash, std::vector<unsigned> ());
  for (auto ix : slot.first->second)
    {
      if (list[ix].parent != parent)
	continue;
      /* A joining clone matches all members from scratch against the
         sequence's seed.  */
      crf_clone cand;
      cand.begin = begin;
      cand.end = end;
      bool ok = true;
      crf_seq &seq0 = list[ix];
      unsigned spos = seq0.clones.front ().begin;
      unsigned cpos = begin;
      for (unsigned member = 0; member != length; ++member)
	{
	  while (crf_transparent_p (blk.pos[spos]))
	    ++spos;
	  while (crf_transparent_p (blk.pos[cpos]))
	    ++cpos;
	  if (!crf_match_insn (blk.pos[spos].insn, blk.pos[cpos].insn,
			       cand))
	    {
	      ok = false;
	      break;
	    }
	  ++spos;
	  ++cpos;
	}
      if (!ok)
	continue;
      if (begin <= seq0.clones.back ().begin)
	continue;
      seq0.clones.push_back (std::move (cand));
      return;
    }

  slot.first->second.push_back (unsigned (list.size ()));
  list.emplace_back ();
  crf_seq &seq = list.back ();
  seq.parent = parent;
  seq.hash = hash;
  seq.length = length;
  crf_clone seed;
  seed.begin = begin;
  seed.end = end;
  /* Seed self-match establishes the identity map and def sets.  */
  {
    unsigned spos = begin;
    for (unsigned member = 0; member != length; ++member)
      {
	while (crf_transparent_p (blk.pos[spos]))
	  ++spos;
	crf_match_insn (blk.pos[spos].insn, blk.pos[spos].insn, seed);
	++spos;
      }
  }
  seq.clones.push_back (std::move (seed));
}

/* Discover parameterized clone families over BLK into LIST: seed a
   length-1 sequence at every eligible position, then grow every
   multi-clone sequence by one member per round through crf_extend, up
   to MAX_RESIDUAL members (the record must fit the slot budget).
   Transparent positions (zero-length markers, excludable
   materializations) are stepped over; a barrier or ineligible position
   ends growth of that clone.  LIST[0] is the null sequence.  */

static void
crf_build_sequences (std::vector<crf_seq> &list, crf_block &blk,
		     unsigned max_residual)
{
  list.clear ();
  list.push_back (crf_seq ()); /* null */
  std::map<unsigned, std::vector<unsigned>> map;

  unsigned n = blk.pos.size ();
  for (unsigned ix = 0; ix != n; ++ix)
    {
      crf_position &p = blk.pos[ix];
      if (p.barrier || crf_transparent_p (p) || !p.eligible)
	continue;
      crf_extend (map, list, blk, 0, 1, ix, ix + 1);
    }

  unsigned from = 1, length = 1;
  unsigned max_length = max_residual;
  while (length++ < max_length)
    {
      map.clear ();
      unsigned seq_end = list.size ();
      for (unsigned seq_ix = from; seq_ix != seq_end; ++seq_ix)
	{
	  if (list[seq_ix].clones.size () == 1)
	    continue;
	  for (unsigned clone_ix = 0,
		 clone_end = list[seq_ix].clones.size ();
	       clone_ix != clone_end; ++clone_ix)
	    {
	      unsigned span_begin = list[seq_ix].clones[clone_ix].begin;
	      unsigned span_end = list[seq_ix].clones[clone_ix].end;
	    skip_transparent:
	      if (span_end >= blk.pos.size ())
		continue;
	      if (crf_transparent_p (blk.pos[span_end]))
		{
		  span_end++;
		  goto skip_transparent;
		}
	      crf_position &nxt = blk.pos[span_end];
	      if (nxt.barrier || !nxt.eligible)
		continue;
	      crf_extend (map, list, blk, seq_ix, length, span_begin,
			  span_end + 1);
	    }
	}
      from = seq_end;
    }
}

/* A selected family, fully verified: the rename plan, bridges, and
   member movements ready to apply.  */

struct crf_plan
{
  unsigned residual = 0;		   /* recorded slot words (= members) */
  int saving = 0;			   /* modeled issued-slot saving */
  std::vector<crf_clone> clones;	   /* surviving, disjoint */
  /* per clone: member positions (block indices), lockstep with the seed's */
  std::vector<std::vector<unsigned>> members;
  /* value index -> final hard reg */
  std::map<unsigned, unsigned> renames;
  /* value index -> clone whose lockstep walk required the rename
     (bystander cascade swaps carry -1) */
  std::map<unsigned, int> rename_source;
  /* per clone: bridge moves (dest_reg <- src_reg) inserted at clone head */
  std::vector<std::vector<std::pair<unsigned, unsigned>>> bridges;
  /* per clone: excludable positions inside the span moving to the head
     (its consumer is a member) or the tail (consumers all later) */
  std::vector<std::vector<unsigned>> moves_head;
  std::vector<std::vector<unsigned>> moves_tail;
};

/* FINAL LOCKSTEP AUDIT comparator: structural equality of a
   seed/clone member pair under the plan's FINAL register assignment
   (every value's register read through plan.renames).  Mirrors
   crf_match_rtx's structure walk, but where the walk RECORDED live-in
   correspondences to be canonicalized later, this comparator REQUIRES
   the canonicalization to have happened: the record delivers the
   seed's words byte-exactly at every clone, so any remaining register
   divergence is wrong code.  */

static bool
crf_final_lockstep_rtx (rtx a, rtx b, bool in_def,
			const crf_position &pa, const crf_position &pb,
			const crf_plan &plan, const crf_block &blk,
			const std::vector<std::pair<unsigned, unsigned>>
			  &bridges)
{
  if (!a || !b)
    return a == b;
  if (GET_CODE (a) != GET_CODE (b) || GET_MODE (a) != GET_MODE (b))
    return false;
  switch (GET_CODE (a))
    {
    case REG:
      {
	if (REG_NREGS (a) != 1 || REG_NREGS (b) != 1)
	  return false;
	unsigned ra = REGNO (a), rb = REGNO (b);
	if (!SFPU_REG_P (ra) || !SFPU_REG_P (rb))
	  return ra == rb;
	auto final_of = [&] (const crf_position &p, unsigned r) -> unsigned
	{
	  int bit = int (r) - SFPU_REG_FIRST;
	  int vix = in_def ? p.value_of_def[bit] : p.value_of_use[bit];
	  if (vix < 0)
	    return r;
	  auto it = plan.renames.find (unsigned (vix));
	  return it != plan.renames.end () ? it->second
					   : blk.values[vix].reg;
	};
	unsigned fa = final_of (pa, ra), fb = final_of (pb, rb);
	if (fa == fb)
	  return true;
	/* A live-in correspondence resolved by a clone-head BRIDGE
	   copy (dest <- src) legitimately leaves the clone's value in
	   its own register: the bridge moves it into the seed's before
	   the clone runs.  Def-side divergence is never bridged.  */
	if (!in_def)
	  for (auto const &br : bridges)
	    if (br.first == fa && br.second == fb)
	      return true;
	return false;
      }

    case CONST_INT:
      return INTVAL (a) == INTVAL (b);

    case SCRATCH:
      return true;

    case SET:
      return crf_final_lockstep_rtx (SET_SRC (a), SET_SRC (b), false,
				     pa, pb, plan, blk, bridges)
	&& crf_final_lockstep_rtx (SET_DEST (a), SET_DEST (b), true,
				   pa, pb, plan, blk, bridges);

    case CLOBBER:
      return crf_final_lockstep_rtx (XEXP (a, 0), XEXP (b, 0), true,
				     pa, pb, plan, blk, bridges);

    case USE:
    case MEM:
      return crf_final_lockstep_rtx (XEXP (a, 0), XEXP (b, 0), false,
				     pa, pb, plan, blk, bridges);

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
	  if (!crf_final_lockstep_rtx (XVECEXP (a, 0, ix),
				       XVECEXP (b, 0, ix), in_def,
				       pa, pb, plan, blk, bridges))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Collect into OUT the block positions of clone C's LENGTH members: the
   eligible, non-transparent, non-barrier positions of BLK inside C's
   span, in stream order.  A caller finding fewer than LENGTH entries
   knows the clone's span no longer carries a full member set.  */

static void
crf_clone_members (crf_block const &blk, crf_clone const &c, unsigned length,
		   std::vector<unsigned> &out)
{
  out.clear ();
  for (unsigned pos = c.begin; pos != c.end && out.size () != length; ++pos)
    if (!crf_transparent_p (blk.pos[pos]) && blk.pos[pos].eligible
	&& !blk.pos[pos].barrier)
      out.push_back (pos);
}

/* The final register of a value under the plan.  */

static inline unsigned
crf_final_reg (crf_block const &blk, crf_plan const &plan, int vix)
{
  auto it = plan.renames.find (vix);
  return it != plan.renames.end () ? it->second : blk.values[vix].reg;
}

/* Final-assignment def/use register masks of the instruction at POS.  */

static void
crf_final_masks (crf_block const &blk, crf_plan const &plan, unsigned pos,
		 uint32_t *defs, uint32_t *uses)
{
  crf_position const &p = blk.pos[pos];
  *defs = 0;
  *uses = 0;
  for (int i = 0; i != 8; ++i)
    {
      if (p.value_of_def[i] >= 0)
	*defs |= crf_reg_bit (crf_final_reg (blk, plan, p.value_of_def[i]));
      if (p.value_of_use[i] >= 0)
	*uses |= crf_reg_bit (crf_final_reg (blk, plan, p.value_of_use[i]));
    }
  *uses |= p.marker_mask & 0xFF;
}

/* Can the excluded materialization at POS move to the clone HEAD (before
   ANCHOR) or TAIL (after LAST), under the FINAL register assignment?
   Ordinary dependence check against every crossed instruction; a crossed
   CC write would change the member's lane gating.  Positions in SKIP move
   with it (order preserved) and are transparent.  */

static bool
crf_move_ok (crf_block const &blk, crf_plan const &plan, unsigned pos,
	     unsigned from, unsigned to,
	     std::vector<unsigned> const &skip)
{
  uint32_t mdefs, muses;
  crf_final_masks (blk, plan, pos, &mdefs, &muses);
  for (unsigned ix = from; ix != to; ++ix)
    {
      if (ix == pos)
	continue;
      if (std::find (skip.begin (), skip.end (), ix) != skip.end ())
	continue;
      crf_position const &q = blk.pos[ix];
      if (q.empty && !q.marker_mask)
	continue;
      if (q.opaque || q.cc_write)
	return false;
      uint32_t qdefs, quses;
      crf_final_masks (blk, plan, ix, &qdefs, &quses);
      if ((qdefs & (mdefs | muses)) || (quses & mdefs))
	return false;
    }
  return true;
}

static bool crf_occupancy_ok (crf_block &blk, crf_plan &plan,
			      int *conflict_a = nullptr,
			      int *conflict_b = nullptr);

/* THE plan-order interpreter: the final
   instruction order of a plan over BLK, as entries into blk.pos --
   e >= 0 is block position e; e < 0 is one bridge move of clone
   (-1 - e), one entry per bridge in plan order.  Per-clone head moves,
   then bridges, seat immediately before the clone's anchor (its first
   member); tail moves seat immediately after its last member; unmoved
   positions keep block order.

   BOTH consumers walk this one stream: crf_shadow_contract_ok
   SIMULATES it (delay-contract re-verification of the final order)
   and crf_apply REALIZES it (reseating the moved members and emitting
   the bridges).  The hand-maintained mirror the verifier used to
   carry ("mirrors crf_apply exactly") is deleted, not patched; any
   residual divergence between simulation and realization is still
   caught fail-closed by the final-lockstep re-verification belt
   (counted-row-final-lockstep-divergence).  */

static std::vector<int>
crf_plan_order (crf_block &blk, crf_plan &plan)
{
  unsigned n = blk.pos.size ();
  std::vector<char> is_moved (n, 0);
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      for (unsigned pos : plan.moves_head[c])
	is_moved[pos] = 1;
      for (unsigned pos : plan.moves_tail[c])
	is_moved[pos] = 1;
    }

  std::vector<int> order;
  order.reserve (n + 8);
  std::map<unsigned, unsigned> anchor_clone, tail_clone;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      anchor_clone[plan.members[c].front ()] = c;
      tail_clone[plan.members[c].back ()] = c;
    }
  for (unsigned pos = 0; pos != n; ++pos)
    {
      auto ac = anchor_clone.find (pos);
      if (ac != anchor_clone.end ())
	{
	  for (unsigned m : plan.moves_head[ac->second])
	    order.push_back (int (m));
	  for (unsigned b = 0; b != plan.bridges[ac->second].size (); ++b)
	    order.push_back (-1 - int (ac->second));	/* bridge of clone */
	}
      if (!is_moved[pos])
	order.push_back (int (pos));
      auto tc = tail_clone.find (pos);
      if (tc != tail_clone.end ())
	for (unsigned m : plan.moves_tail[tc->second])
	  order.push_back (int (m));
    }
  return order;
}

/* Delay-contract verification of the plan's FINAL order (a
   hardware-adjudicated wrong-code guard).

   The nop inserter (rtl-rvtt-schedule.cc transform) discharges every
   XTT_DELAY contract against the ORIGINAL instruction order: a
   dependent erratum consumer (xtt_dynamic_bug -- the SFPMAD.md
   "automatic stalling does not detect" list, e.g. SFPSWAP min/max's
   1st-cycle reads) directly behind a DYNAMIC-delay producer gets an
   SFPNOP, and a pair separated by any slot-occupying word needs none.
   Moving an excluded member can VACATE exactly such a gap word: the
   discharged contract silently re-opens, no later pass re-probes it,
   and the recorded window replays the unpadded pair forever.  On the
   tanh fresh sem body the canonicalization moved the next row's
   SFPLOADI out of the SFPMUL->SFPSWAP gap; the hardware stall logic
   cannot see SFPSWAP's 1st-cycle read (documented hardware bug), the
   min clamp consumed the STALE accumulator and polynomial values > 1
   escaped (device corr FAIL) while the pinned sim -- which executes
   instructions atomically -- kept passing.

   So: walk the plan's final order (moved members re-seated at their
   clone heads/tails, bridges at the anchors) and re-verify every
   producer/consumer adjacency with the inserter's own test -- the
   producer's XTT_DELAY class, the register dependence under the FINAL
   assignment, and the per-target erratum mask.  Any undischarged pair
   refuses the family by name, byte-identically.  A barrier or opaque
   position conservatively ends the walk segment (the shapes this
   machinery forms are all-Tensix runs).  Renames cannot create new
   dependences (values are preserved and occupancy is verified), so a
   violation found here is always plan-caused.  */

static bool
crf_shadow_contract_ok (crf_block &blk, crf_plan &plan)
{
  /* The final order, from the one plan-order interpreter crf_apply
     realizes (crf_plan_order above).  */
  std::vector<int> order = crf_plan_order (blk, plan);

  unsigned bug_mask = TARGET_XTT_TENSIX_BH ? XTT_DYNAMIC_BUG_BH
    : TARGET_XTT_TENSIX_QSR ? XTT_DYNAMIC_BUG_QSR : 0;

  int prod = -1;		/* order entry of the open delay producer */
  unsigned bridge_ix = 0;	/* running index into the clone's bridges */
  int bridge_clone = -1;
  for (unsigned oi = 0; oi != order.size (); ++oi)
    {
      int e = order[oi];
      uint32_t cuses;
      bool slot_word, is_nop, is_bridge = e < 0;
      rtx_insn *cinsn = nullptr;
      if (is_bridge)
	{
	  /* A bridge is an all-lanes register move: it occupies a slot,
	     reads its source, and (not yet emitted) carries no audited
	     erratum attribute -- treat a dependent one conservatively.  */
	  unsigned c = unsigned (-1 - e);
	  if (bridge_clone != int (c))
	    {
	      bridge_clone = int (c);
	      bridge_ix = 0;
	    }
	  cuses = crf_reg_bit (plan.bridges[c][bridge_ix++].second);
	  slot_word = true;
	  is_nop = false;
	}
      else
	{
	  crf_position const &q = blk.pos[e];
	  if (q.barrier || q.opaque
	      || GET_CODE (q.insn) != INSN
	      || recog_memoized (q.insn) < 0
	      || get_attr_type (q.insn) != TYPE_TENSIX)
	    {
	      prod = -1;	/* conservative segment end */
	      continue;
	    }
	  cinsn = q.insn;
	  uint32_t cdefs;
	  crf_final_masks (blk, plan, unsigned (e), &cdefs, &cuses);
	  slot_word = get_attr_length (cinsn) != 0;
	  is_nop = recog_memoized (cinsn) == CODE_FOR_rvtt_sfpnop;
	}

      if (prod >= 0)
	{
	  rtx_insn *pinsn = blk.pos[order[prod]].insn;
	  bool hazard;
	  if (get_attr_xtt_delay (pinsn) == XTT_DELAY_STATIC)
	    hazard = !is_nop;
	  else
	    {
	      uint32_t pdefs, puses;
	      crf_final_masks (blk, plan, unsigned (order[prod]),
			       &pdefs, &puses);
	      hazard = (pdefs & cuses) != 0;
	      if (hazard && bug_mask && !is_bridge)
		hazard = (bug_mask & get_attr_xtt_dynamic_bug (cinsn)) != 0;
	    }
	  if (hazard)
	    {
	      rvtt_refuse (RVTT_REF_COUNTED_ROW_VACATED_DELAY_SHADOW, dump_file,
			   "Refusing counted-row family [%u,%u):"
			   " counted-row-vacated-delay-shadow: the final"
			   " order puts %s insn %d in insn %d's undischarged"
			   " delay shadow (erratum consumer; SFPMAD.md"
			   " stall-detection bug list)\n",
			   plan.clones.front ().begin,
			   plan.clones.front ().end,
			   is_bridge ? "bridge for" : "dependent",
			   is_bridge ? INSN_UID (blk.pos[order[prod]].insn)
				     : INSN_UID (cinsn),
			   INSN_UID (pinsn));
	      return false;
	    }
	}

      if (!slot_word)
	continue;		/* zero-length marker: the shadow stays open */
      prod = (!is_bridge && get_attr_xtt_delay (cinsn) != XTT_DELAY_NONE)
	? int (oi) : -1;
    }
  return true;
}

/* True read-modify-write tie of INSN's definition of REG: a source
   operand carries a matching constraint naming the destination operand,
   so both share one encoded register field and a rename must carry the
   register's previous value along.  A source that merely happens to name
   the same register in an independently encoded field is not a tie.  */

static bool
crf_tied_rmw_p (rtx_insn *insn, unsigned reg)
{
  extract_insn_cached (insn);
  int dest_op = -1;
  for (int i = 0; i < recog_data.n_operands; ++i)
    if (recog_data.operand_type[i] != OP_IN
	&& REG_P (recog_data.operand[i])
	&& REGNO (recog_data.operand[i]) == reg)
      {
	if (recog_data.operand_type[i] == OP_INOUT)
	  return true;
	dest_op = i;
      }
  if (dest_op < 0)
    return false;
  for (int i = 0; i < recog_data.n_operands; ++i)
    {
      if (i == dest_op || !REG_P (recog_data.operand[i])
	  || REGNO (recog_data.operand[i]) != reg)
	continue;
      for (const char *c = recog_data.constraints[i]; *c;)
	if (ISDIGIT (*c))
	  {
	    char *end;
	    if (strtol (c, &end, 10) == dest_op)
	      return true;
	    c = end;
	  }
	else
	  ++c;
    }
  return false;
}

/* Verify one candidate family and build its plan.  Returns true with PLAN
   filled on success; every refusal dumps its taxonomy name.  */

static bool
crf_verify_family (crf_block &blk, crf_seq &seq, unsigned budget,
		   bool sticky, crf_plan &plan, unsigned ref)
{
  unsigned length = seq.length;

  /* Overlap triage: clones ascending by begin; keep a maximal disjoint set.  */
  plan.clones.clear ();
  {
    unsigned bound = 0;
    for (auto const &c : seq.clones)
      {
	if (plan.clones.size () && c.begin < bound)
	  continue;
	plan.clones.push_back (c);
	bound = c.end;
      }
  }
  if (plan.clones.size () < 2 || ref >= plan.clones.size ())
    return false;

  plan.residual = length;
  if (length < MIN_SEQUENCE)
    return false;
  if (length > budget)
    {
      rvtt_refuse (RVTT_REF_COUNTED_ROW_SLOT_BUDGET, dump_file,
		   "Refusing counted-row family [%u,%u):"
		   " counted-row-slot-budget: residual %u exceeds the largest"
		   " available replay span %u\n",
		   plan.clones.front ().begin, plan.clones.front ().end,
		   length, budget);
      return false;
    }

  plan.members.assign (plan.clones.size (), {});
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      crf_clone_members (blk, plan.clones[c], length, plan.members[c]);
      if (plan.members[c].size () != length)
	return false;
    }

  /* Residual soundness under possibly-enabled shadow coupling, and the
     v1 multi-result restriction (companion-group boundary semantics stay
     with the word-exact machinery).  */
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    for (unsigned m = 0; m != length; ++m)
      {
	crf_position const &mp = blk.pos[plan.members[c][m]];
	rtx_insn *insn = mp.insn;
	xtt_effect_set e = rvtt_insn_effects (insn);
	xtt_multiresult_group group;
	if (rvtt_multiresult_group (insn, e, &group))
	  {
	    rvtt_refuse (RVTT_REF_COUNTED_ROW_RESIDUAL_NOT_UNIFORM, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " counted-row-residual-not-uniform: member %u is a"
			 " multi-result instruction\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
	/* Rename planning (seed_def_reg below and the clone definition
	   roles) requires a single canonical definition register per
	   member.  A member defining several registers (a two-register
	   SFPSWAP, or audited hidden dataflow modeled as fixed def+use)
	   has no single seed register: refuse by name rather than
	   asserting.  */
	if (popcount_hwi (mp.defs) > 1)
	  {
	    rvtt_refuse (RVTT_REF_COUNTED_ROW_MULTIDEF_MEMBER, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " counted-row-multidef-member: member %u defines"
			 " multiple registers\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
	if (sticky && (e.lreg_write & 0xF))
	  {
	    rvtt_refuse (RVTT_REF_SHADOW_STATE_UNPROVED, dump_file,
			 "Refusing counted-row family [%u,%u):"
			 " shadow-state-unproved: member %u writes the value"
			 " bank under possibly-enabled index tracking\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, m);
	    return false;
	  }
      }

  /* Rename planning with seed chain-closure.  SEED_REMAP retargets the
     seed's own definition at member M when a cross-clone value carries a
     def role and a live-in role that demand different registers.  A
     refusal specific to one clone drops that clone; only seed-side
     refusals and non-converging chain closure refuse the whole family.  */
  plan.bridges.assign (plan.clones.size (), {});
  plan.moves_head.assign (plan.clones.size (), {});
  plan.moves_tail.assign (plan.clones.size (), {});
  std::map<unsigned, unsigned> seed_remap; /* member index -> target reg */

  auto seed_def_reg = [&] (unsigned m) -> int
  {
    auto it = seed_remap.find (m);
    if (it != seed_remap.end ())
      return int (it->second);
    crf_position const &p = blk.pos[plan.members[ref][m]];
    if (!p.defs)
      return -1;
    gcc_assert (popcount_hwi (p.defs) == 1);
    return SFPU_REG_FIRST + exact_log2 (p.defs);
  };

  unsigned iter_limit = 8 + unsigned (plan.clones.size ());
  int drop_clone = -1;
  for (unsigned iter = 0; ; ++iter)
    {
      if (drop_clone >= 0)
	{
	  gcc_assert (unsigned (drop_clone) != ref);
	  plan.clones.erase (plan.clones.begin () + drop_clone);
	  plan.members.erase (plan.members.begin () + drop_clone);
	  plan.bridges.erase (plan.bridges.begin () + drop_clone);
	  plan.moves_head.erase (plan.moves_head.begin () + drop_clone);
	  plan.moves_tail.erase (plan.moves_tail.begin () + drop_clone);
	  if (unsigned (drop_clone) < ref)
	    --ref;
	  drop_clone = -1;
	  if (plan.clones.size () < 2)
	    return false;
	}
      if (iter == iter_limit)
	{
	  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
		       "Refusing counted-row family [%u,%u):"
		       " counted-row-map-live-out: chain closure did not"
		       " converge\n", plan.clones.front ().begin,
		       plan.clones.front ().end);
	  return false;
	}

      plan.renames.clear ();
      plan.rename_source.clear ();
      for (auto &b : plan.bridges)
	b.clear ();
      bool remapped = false;
      bool refused = false;
      /* Which member's definition role planned a value's rename, for
         chain closure from the live-in side.  */
      std::map<unsigned, unsigned> plan_def_member;

      uint32_t seed_writes = 0;
      for (unsigned m = 0; m != length; ++m)
	{
	  int r = seed_def_reg (m);
	  if (r >= 0)
	    seed_writes |= crf_reg_bit (r);
	}

      /* Plan one value's rename with read-modify-write chain closure:
         when a renamed value's definition also consumes the register's
         previous value (an RMW half), that value must move to the same
         register or the single encoded register field would tear.
         Returns 1 on success, 0 on an in-plan conflict (caller decides),
         -1 on an unrenameable value.  */
      int planning_clone = -1;
      auto plan_rename = [&] (int vix0, unsigned target) -> int
      {
	std::vector<int> work (1, vix0);
	while (!work.empty ())
	  {
	    int vix = work.back ();
	    work.pop_back ();
	    crf_value const &v = blk.values[vix];
	    if (v.reg == target && !plan.renames.count (vix))
	      continue;
	    auto it = plan.renames.find (vix);
	    if (it != plan.renames.end ())
	      {
		if (it->second == target)
		  continue;
		return 0;
	      }
	    if (v.fixed || v.poisoned || v.live_out || v.def_pos < 0)
	      return -1;
	    plan.renames[vix] = target;
	    plan.rename_source[vix] = planning_clone;
	    /* RMW chain: the definition consumes the register's previous
	       value through a TIED operand (one encoded field).  */
	    crf_position const &dp = blk.pos[v.def_pos];
	    int bit = int (v.reg) - SFPU_REG_FIRST;
	    if (dp.value_of_use[bit] >= 0
		&& crf_tied_rmw_p (v.def_insn, v.reg))
	      work.push_back (dp.value_of_use[bit]);
	  }
	return 1;
      };

      for (unsigned c = 0;
	   c != plan.clones.size () && !refused && drop_clone < 0
	   && !remapped; ++c)
	{
	  if (c == ref)
	    continue;
	  planning_clone = int (c);
	  crf_clone map;
	  for (unsigned m = 0;
	       m != length && !refused && drop_clone < 0 && !remapped; ++m)
	    {
	      unsigned spos = plan.members[ref][m];
	      unsigned cpos = plan.members[c][m];
	      std::map<unsigned, unsigned> before_r2p = map.r2p;
	      if (!crf_match_insn (blk.pos[spos].insn, blk.pos[cpos].insn,
				   map))
		{
		  drop_clone = int (c);
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RESIDUAL_NOT_UNIFORM,
			       dump_file,
			       "Dropping counted-row clone"
			       " [%u,%u): counted-row-residual-not-uniform:"
			       " diverges at member %u\n",
			       plan.clones[c].begin, plan.clones[c].end, m);
		  break;
		}

	      crf_position const &cp = blk.pos[cpos];

	      /* New live-in correspondences discovered at this member.  */
	      for (auto const &pr : map.r2p)
		{
		  if (map.defined_r.count (pr.first))
		    continue;
		  if (before_r2p.count (pr.first))
		    continue;
		  unsigned t = pr.first, s = pr.second;
		  if (t == s)
		    continue;
		  int ci = int (t) - SFPU_REG_FIRST;
		  int vu = cp.value_of_use[ci];
		  if (vu < 0)
		    continue;
		  crf_value const &v = blk.values[vu];
		  if (!v.fixed && !v.poisoned && !v.live_out
		      && v.def_pos >= 0)
		    {
		      int rr = plan_rename (vu, s);
		      if (rr == 1)
			continue;
		      if (rr == 0)
			{
			  /* Def role vs live-in role conflict: close the
			     chain by retargeting the seed definition the
			     def role mirrors, then replan.  */
			  auto dm = plan_def_member.find (vu);
			  if (dm != plan_def_member.end ())
			    {
			      seed_remap[dm->second] = s;
			      remapped = true;
			      break;
			    }
			}
		      drop_clone = int (c);
		      rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
				   "Dropping counted-row clone"
				   " [%u,%u): counted-row-map-live-out:"
				   " live-in value in r%u cannot move to"
				   " r%u\n", plan.clones[c].begin,
				   plan.clones[c].end, t, s);
		      break;
		    }
		  /* Unrenameable live-in: bridge with one all-lanes move,
		     unless the recorded program would clobber the source
		     still needed later.  */
		  bool used_later = false;
		  for (unsigned up : v.use_positions)
		    if (up >= plan.clones[c].end)
		      used_later = true;
		  if (v.live_out)
		    used_later = true;
		  if (used_later && (seed_writes & crf_reg_bit (t)))
		    {
		      drop_clone = int (c);
		      rvtt_refuse (RVTT_REF_COUNTED_ROW_BRIDGE_CLOBBER,
				   dump_file,
				   "Dropping counted-row clone"
				   " [%u,%u): counted-row-bridge-clobber:"
				   " r%u is written by the recorded program"
				   " but consumed after the clone\n",
				   plan.clones[c].begin,
				   plan.clones[c].end, t);
		      break;
		    }
		  plan.bridges[c].emplace_back (s, t);
		}
	      if (refused || drop_clone >= 0 || remapped)
		break;

	      /* Definition roles.  */
	      if (cp.defs)
		{
		  gcc_assert (popcount_hwi (cp.defs) == 1);
		  int ci = exact_log2 (cp.defs);
		  int vd = cp.value_of_def[ci];
		  gcc_assert (vd >= 0);
		  crf_value const &v = blk.values[vd];
		  int want = seed_def_reg (m);
		  gcc_assert (want >= 0);
		  auto it = plan.renames.find (vd);
		  if (it != plan.renames.end ()
		      && int (it->second) != want)
		    {
		      /* The value already carries a live-in role demanding
		         a different register: close the seed chain at this
		         member and replan.  */
		      seed_remap[m] = it->second;
		      remapped = true;
		      break;
		    }
		  if (int (v.reg) != want || it != plan.renames.end ())
		    {
		      if (v.fixed || v.poisoned || v.live_out)
			{
			  drop_clone = int (c);
			  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT,
				       dump_file,
				       "Dropping counted-row"
				       " clone [%u,%u):"
				       " counted-row-map-live-out: pinned or"
				       " live-out definition of r%u at"
				       " member %u\n", plan.clones[c].begin,
				       plan.clones[c].end, v.reg, m);
			  break;
			}
		      int rr = plan_rename (vd, want);
		      if (rr != 1)
			{
			  drop_clone = int (c);
			  rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT,
				       dump_file,
				       "Dropping counted-row"
				       " clone [%u,%u):"
				       " counted-row-map-live-out:"
				       " definition of r%u at member %u"
				       " cannot move to r%u\n",
				       plan.clones[c].begin,
				       plan.clones[c].end, v.reg, m, want);
			  break;
			}
		    }
		  if (!plan_def_member.count (vd))
		    plan_def_member[vd] = m;
		}
	    }
	}

      /* Seed remap entries are renames of the seed's own def values.  */
      planning_clone = -1;
      if (!remapped && drop_clone < 0 && !refused)
	for (auto const &sr : seed_remap)
	  {
	    crf_position const &p = blk.pos[plan.members[ref][sr.first]];
	    int vd = p.value_of_def[exact_log2 (p.defs)];
	    gcc_assert (vd >= 0);
	    if (plan_rename (vd, sr.second) != 1)
	      {
		rvtt_refuse (RVTT_REF_COUNTED_ROW_MAP_LIVE_OUT, dump_file,
			     "Refusing counted-row family"
			     " [%u,%u): counted-row-map-live-out: seed chain"
			     " closure needs an unrenameable value\n",
			     plan.clones.front ().begin,
			     plan.clones.front ().end);
		return false;
	      }
	  }

      if (!remapped && drop_clone < 0 && !refused)
	{
	  /* Excludable materializations: every one whose value feeds a
	     clone member relocates to that clone's head (canonical-register
	     recips serialize between launches, the hand's own delivery
	     discipline); one inside a span but feeding nothing in the
	     family moves out past the tail.  Movement legality is
	     VALUE-based: uses must follow the new position, no CC write or
	     opaque instruction may be crossed (lane-state constancy), and
	     the whole-block occupancy simulation of the final assignment
	     is the authoritative gate.  */
	  for (auto &mh : plan.moves_head)
	    mh.clear ();
	  for (auto &mt : plan.moves_tail)
	    mt.clear ();

	  /* Clone of the first member-use of an excludable's value,
	     following chains through other excludables (the RMW pair).  */
	  std::map<unsigned, int> consumer_clone; /* position -> clone or -1 */
	  std::vector<unsigned> excl_all;
	  for (unsigned pos = 0; pos != blk.pos.size (); ++pos)
	    if (blk.pos[pos].excludable)
	      excl_all.push_back (pos);
	  std::map<unsigned, unsigned> member_clone; /* member pos -> clone */
	  for (unsigned c = 0; c != plan.clones.size (); ++c)
	    for (unsigned m = 0; m != length; ++m)
	      member_clone[plan.members[c][m]] = c;
	  for (auto pit = excl_all.rbegin (); pit != excl_all.rend ();
	       ++pit)
	    {
	      unsigned pos = *pit;
	      crf_position const &p = blk.pos[pos];
	      int cc = -1;
	      for (int i = 0; i != 8 && cc < 0; ++i)
		{
		  if (p.value_of_def[i] < 0)
		    continue;
		  for (unsigned up
			 : blk.values[p.value_of_def[i]].use_positions)
		    {
		      auto mi = member_clone.find (up);
		      if (mi != member_clone.end ())
			{
			  cc = int (mi->second);
			  break;
			}
		      auto ei = consumer_clone.find (up);
		      if (ei != consumer_clone.end () && ei->second >= 0)
			{
			  cc = ei->second;
			  break;
			}
		    }
		}
	      consumer_clone[pos] = cc;
	    }

	  /* Lane-state/opacity constancy over a movement range.  */
	  auto move_window_ok = [&] (unsigned lo, unsigned hi) -> bool
	  {
	    for (unsigned ix = lo; ix < hi; ++ix)
	      if (blk.pos[ix].cc_write || blk.pos[ix].opaque)
		return false;
	    return true;
	  };

	  bool moves_ok = true;
	  for (unsigned pos : excl_all)
	    {
	      int cc = consumer_clone[pos];
	      if (cc >= 0)
		{
		  unsigned anchor = plan.members[cc].front ();
		  if (pos < anchor)
		    {
		      /* Already directly ahead of the clone (only
		         transparent positions between): leave it.  */
		      bool clean = true;
		      for (unsigned ix = pos + 1; ix != anchor; ++ix)
			if (!crf_transparent_p (blk.pos[ix]))
			  clean = false;
		      if (clean)
			continue;
		    }
		  /* Uses must all follow the new position; a use by a
		     fellow excludable relocating to the same head keeps
		     its original order there.  */
		  crf_position const &p = blk.pos[pos];
		  bool uses_ok = true;
		  for (int i = 0; i != 8; ++i)
		    if (p.value_of_def[i] >= 0)
		      for (unsigned up
			     : blk.values[p.value_of_def[i]].use_positions)
			if (up < anchor
			    && !(blk.pos[up].excludable
				 && consumer_clone.count (up)
				 && consumer_clone[up] == cc))
			  uses_ok = false;
		  unsigned lo = MIN (pos, anchor), hi = MAX (pos, anchor);
		  if (!uses_ok || !move_window_ok (lo, hi))
		    {
		      if (unsigned (cc) == ref)
			{
			  rvtt_refuse
			    (RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			     dump_file,
			     "Refusing counted-row"
			     " family [%u,%u):"
			     " counted-row-excluded-member-"
			     "unmovable: insn %d cannot reach its"
			     " consumer clone\n",
			     plan.clones.front ().begin,
			     plan.clones.front ().end,
			     INSN_UID (blk.pos[pos].insn));
			  return false;
			}
		      rvtt_refuse
			(RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			 dump_file,
			 "Dropping counted-row clone"
			 " [%u,%u):"
			 " counted-row-excluded-member-unmovable:"
			 " insn %d cannot reach its consumer"
			 " clone\n", plan.clones[cc].begin,
			 plan.clones[cc].end,
			 INSN_UID (blk.pos[pos].insn));
		      drop_clone = cc;
		      moves_ok = false;
		      break;
		    }
		  plan.moves_head[cc].push_back (pos);
		  continue;
		}
	      /* No consumer in the family: if inside a clone span, move
	         out past the tail.  */
	      for (unsigned c = 0; c != plan.clones.size (); ++c)
		if (pos > plan.members[c].front ()
		    && pos < plan.members[c].back ())
		  {
		    unsigned last = plan.members[c].back ();
		    crf_position const &p = blk.pos[pos];
		    bool uses_ok = true;
		    for (int i = 0; i != 8; ++i)
		      if (p.value_of_def[i] >= 0)
			for (unsigned up
			       : blk.values[p.value_of_def[i]]
				   .use_positions)
			  if (up <= last)
			    uses_ok = false;
		    if (!uses_ok || !move_window_ok (pos, last + 1))
		      {
			if (c == ref)
			  return false;
			rvtt_refuse
			  (RVTT_REF_COUNTED_ROW_EXCLUDED_MEMBER_UNMOVABLE,
			   dump_file,
			   "Dropping counted-row clone"
			   " [%u,%u):"
			   " counted-row-excluded-member-"
			   "unmovable: insn %d cannot move past"
			   " the tail\n", plan.clones[c].begin,
			   plan.clones[c].end,
			   INSN_UID (blk.pos[pos].insn));
			drop_clone = int (c);
			moves_ok = false;
		      }
		    else
		      plan.moves_tail[c].push_back (pos);
		    break;
		  }
	      if (!moves_ok)
		break;
	    }
	  (void) moves_ok;
	}

      /* Occupancy of the final assignment, with the bystander cascade:
         a conflict against an untouched renameable value swaps it into
         the evacuated register; a non-cascadable conflict drops the
         clone whose lockstep walk required the conflicting rename.  */
      if (!remapped && drop_clone < 0 && !refused)
	{
	  bool occupancy = false;
	  for (unsigned swap = 0; swap != 64; ++swap)
	    {
	      int a = -2, b = -2;
	      if (crf_occupancy_ok (blk, plan, &a, &b))
		{
		  occupancy = true;
		  break;
		}
	      int renamed = -1, bystander = -1;
	      if (a >= 0 && plan.renames.count (a)
		  && b >= 0 && !plan.renames.count (b))
		{
		  renamed = a;
		  bystander = b;
		}
	      else if (b >= 0 && plan.renames.count (b)
		       && a >= 0 && !plan.renames.count (a))
		{
		  renamed = b;
		  bystander = a;
		}
	      if (renamed >= 0)
		{
		  crf_value const &bv = blk.values[bystander];
		  if (!bv.fixed && !bv.poisoned && !bv.live_out
		      && bv.def_pos >= 0)
		    {
		      plan.renames[bystander] = blk.values[renamed].reg;
		      plan.rename_source[bystander] = -1;
		      if (dump_file)
			fprintf (dump_file, "counted-row bystander swap:"
				 " value of r%u (insn %d) -> r%u\n",
				 bv.reg, INSN_UID (bv.def_insn),
				 blk.values[renamed].reg);
		      continue;
		    }
		}
	      /* Not cascadable: drop the responsible clone.  */
	      int src = -1;
	      for (int v : { a, b })
		if (v >= 0 && plan.rename_source.count (v)
		    && plan.rename_source[v] >= 0)
		  src = plan.rename_source[v];
	      if (src >= 0 && unsigned (src) != ref)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Dropping counted-row clone"
			       " [%u,%u): counted-row-rename-interference:"
			       " unresolvable occupancy conflict\n",
			       plan.clones[src].begin,
			       plan.clones[src].end);
		  drop_clone = src;
		}
	      break;
	    }
	  if (drop_clone < 0 && !occupancy)
	    return false;
	}

      if (drop_clone >= 0 || remapped)
	continue;
      if (refused)
	return false;
      break;
    }

  /* FINAL LOCKSTEP AUDIT: the occupancy cascade's bystander
     swaps rewrite value registers AFTER the lockstep walk verified the
     member words' operand correspondence, and nothing re-checked the
     words against the FINAL assignment -- the launch replays the
     seed's words byte-exactly at every clone, so any remaining
     divergence is wrong code (observed live: the tanh corr TU under
     crossrow-2datum x loop-prgm-reclaim replayed a read of the
     register a bystander swap had just evacuated).  Re-verify every
     member pair under the final assignment, refusing by name.  */
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (c == ref)
	continue;
      for (unsigned m = 0; m != length; ++m)
	{
	  unsigned spos = plan.members[ref][m];
	  unsigned cpos = plan.members[c][m];
	  if (!crf_final_lockstep_rtx (PATTERN (blk.pos[spos].insn),
				       PATTERN (blk.pos[cpos].insn),
				       false, blk.pos[spos],
				       blk.pos[cpos], plan, blk,
				       plan.bridges[c]))
	    {
	      if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_FINAL_LOCKSTEP_DIVERGENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-final-lockstep-"
			       "divergence: member %u of clone %u diverges"
			       " from the seed under the final register"
			       " assignment\n",
			       plan.clones.front ().begin,
			       plan.clones.front ().end, m, c);
		  fprintf (dump_file, "  seed (ref %u):  ", ref);
		  print_rtl_single (dump_file, blk.pos[spos].insn);
		  fprintf (dump_file, "  clone %u: ", c);
		  print_rtl_single (dump_file, blk.pos[cpos].insn);
		  for (auto const &rn : plan.renames)
		    fprintf (dump_file, "  rename: val %u (r%u, insn %d)"
			     " -> r%u\n", rn.first,
			     blk.values[rn.first].reg,
			     blk.values[rn.first].def_insn
			     ? INSN_UID (blk.values[rn.first].def_insn)
			     : -1, rn.second);
		}
	      return false;
	    }
	}
    }

  /* Modeled saving: every non-seed clone's residual collapses to one
     launch; bridges are bought slots; the capture word is one slot.  */
  {
    int bridges_total = 0;
    for (auto const &b : plan.bridges)
      bridges_total += b.size ();
    plan.saving = int (plan.clones.size () - 1) * int (length - 1)
      - bridges_total - 1;
  }
  if (plan.saving < 1)
    {
      if (dump_file)
	fprintf (dump_file, "Not canonicalizing counted-row family"
		 " [%u,%u): modeled saving %d\n",
		 plan.clones.front ().begin, plan.clones.front ().end,
		 plan.saving);
      return false;
    }

  /* A plan that changes nothing is the word-exact machinery's territory.  */
  {
    bool any_change = !plan.renames.empty ();
    for (unsigned c = 0; c != plan.clones.size () && !any_change; ++c)
      any_change = !plan.moves_head[c].empty ()
	|| !plan.moves_tail[c].empty () || !plan.bridges[c].empty ();
    if (!any_change)
      return false;
  }

  /* The moves and bridges above may not re-open a delay shadow the nop
     inserter already discharged: re-verify the
     whole delay contract over the plan's final order, refusing by name.  */
  if (!crf_shadow_contract_ok (blk, plan))
    return false;

  return true;
}

/* Whole-block occupancy verification of the plan's final register
   assignment, over the stream in its FINAL order (excluded members moved,
   bridges inserted), plus the lane-state window proof: no CC write may
   fall inside the span affected by any rewritten value (state-constancy
   makes the rewrites lane-exact for any entry lane state).  */

static bool
crf_occupancy_ok (crf_block &blk, crf_plan &plan,
		  int *conflict_a, int *conflict_b)
{
  unsigned n = blk.pos.size ();

  std::vector<int> time (n, -1);
  std::vector<char> is_moved (n, 0);
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      for (unsigned pos : plan.moves_head[c])
	is_moved[pos] = 1;
      for (unsigned pos : plan.moves_tail[c])
	is_moved[pos] = 1;
    }

  std::vector<int> bridge_time (plan.clones.size (), -1);
  std::map<unsigned, unsigned> anchor_clone, tail_clone;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      anchor_clone[plan.members[c].front ()] = c;
      tail_clone[plan.members[c].back ()] = c;
    }

  int t = 0;
  for (unsigned pos = 0; pos != n; ++pos)
    {
      auto ac = anchor_clone.find (pos);
      if (ac != anchor_clone.end ())
	{
	  unsigned c = ac->second;
	  for (unsigned mpos : plan.moves_head[c])
	    time[mpos] = t++;
	  bridge_time[c] = t++;
	}
      if (!is_moved[pos])
	time[pos] = t++;
      auto tc = tail_clone.find (pos);
      if (tc != tail_clone.end ())
	for (unsigned mpos : plan.moves_tail[tc->second])
	  time[mpos] = t++;
    }

  struct interval { long start, end; int val; };
  std::vector<std::vector<interval>> per_reg (8);

  /* Bridged values: their uses inside the bridging clone move to the
     bridge read.  */
  std::map<unsigned, std::vector<unsigned>> value_bridge_clones;
  for (unsigned c = 0; c < plan.clones.size (); ++c)
    for (auto const &br : plan.bridges[c])
      {
	int vix = -1;
	for (unsigned m = 0; m != plan.members[c].size () && vix < 0; ++m)
	  {
	    crf_position const &p = blk.pos[plan.members[c][m]];
	    int ci = int (br.second) - SFPU_REG_FIRST;
	    if (p.value_of_use[ci] >= 0
		&& blk.values[p.value_of_use[ci]].reg == br.second)
	      vix = p.value_of_use[ci];
	  }
	if (vix >= 0)
	  value_bridge_clones[vix].push_back (c);
      }

  long horizon = 2L * t + 4;
  for (unsigned vix = 0; vix != blk.values.size (); ++vix)
    {
      crf_value const &v = blk.values[vix];
      long start = v.def_pos < 0 ? -1 : 2L * time[v.def_pos] + 1;
      long end = start;
      auto vb = value_bridge_clones.find (vix);
      for (unsigned up : v.use_positions)
	{
	  bool moved_use = false;
	  if (vb != value_bridge_clones.end ())
	    for (unsigned c : vb->second)
	      if (up >= plan.clones[c].begin && up < plan.clones[c].end)
		moved_use = true;
	  if (moved_use)
	    continue;
	  long ut = 2L * time[up];
	  if (ut > end)
	    end = ut;
	}
      if (vb != value_bridge_clones.end ())
	for (unsigned c : vb->second)
	  {
	    long bt = 2L * bridge_time[c];
	    if (bt > end)
	      end = bt;
	  }
      if (v.live_out)
	end = horizon;
      unsigned freg = crf_final_reg (blk, plan, vix);
      per_reg[freg - SFPU_REG_FIRST].push_back ({ start, end,
						  int (vix) });
    }

  /* Bridge destination values: from the bridge write to the end of the
     clone's span (conservative).  */
  for (unsigned c = 0; c < plan.clones.size (); ++c)
    for (auto const &br : plan.bridges[c])
      {
	long start = 2L * bridge_time[c] + 1;
	long end = 2L * time[plan.members[c].back ()];
	per_reg[br.first - SFPU_REG_FIRST].push_back ({ start, end, -1 });
      }

  for (unsigned r = 0; r != 8; ++r)
    {
      auto &iv = per_reg[r];
      std::sort (iv.begin (), iv.end (),
		 [] (interval const &a, interval const &b)
		 { return a.start < b.start; });
      /* Abutment (def at 2t+1 after uses at 2t) is already encoded in the
         timestamps; any remaining overlap is a conflict.  */
      long reach = LONG_MIN;
      int reach_val = -1;
      for (unsigned i = 0; i < iv.size (); ++i)
	{
	  if (i && iv[i].start <= reach)
	    {
	      if (conflict_a)
		{
		  *conflict_a = reach_val;
		  *conflict_b = iv[i].val;
		}
	      if (dump_file)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-rename-interference:"
			       " two values occupy r%u:\n",
			       plan.clones.front ().begin,
			       plan.clones.front ().end, SFPU_REG_FIRST + r);
		  for (auto const &e : iv)
		    fprintf (dump_file, "    val %d [%ld,%ld] def insn %d"
			     " orig r%u\n", e.val, e.start, e.end,
			     e.val >= 0 && blk.values[e.val].def_insn
			     ? INSN_UID (blk.values[e.val].def_insn) : -1,
			     e.val >= 0 ? blk.values[e.val].reg : 0);
		}
	      return false;
	    }
	  if (iv[i].end >= reach)
	    {
	      reach = iv[i].end;
	      reach_val = iv[i].val;
	    }
	}
    }

  /* Lane-state window: the rewrites are lane-exact only while the lane
     state is constant over every affected value's span.  */
  long wmin = LONG_MAX, wmax = LONG_MIN;
  auto widen = [&] (long s, long e) { wmin = MIN (wmin, s);
				      wmax = MAX (wmax, e); };
  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      if (v.def_pos >= 0)
	widen (2L * time[v.def_pos], 2L * time[v.def_pos]);
      for (unsigned up : v.use_positions)
	widen (2L * time[up], 2L * time[up]);
    }
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (!plan.bridges[c].empty ())
	widen (2L * bridge_time[c], 2L * time[plan.members[c].back ()]);
      for (unsigned pos : plan.moves_head[c])
	widen (2L * time[pos],
	       2L * time[plan.members[c].back ()]);
      for (unsigned pos : plan.moves_tail[c])
	widen (2L * time[plan.members[c].front ()], 2L * time[pos]);
    }
  if (wmin <= wmax)
    for (unsigned pos = 0; pos != n; ++pos)
      if (blk.pos[pos].cc_write && time[pos] >= 0
	  && 2L * time[pos] >= wmin && 2L * time[pos] <= wmax)
	{
	  rvtt_refuse (RVTT_REF_COUNTED_ROW_LANE_STATE, dump_file,
		       "Refusing counted-row family [%u,%u):"
		       " counted-row-lane-state: CC write (insn %d) inside"
		       " the rewritten window\n", plan.clones.front ().begin,
		       plan.clones.front ().end,
		       INSN_UID (blk.pos[pos].insn));
	  return false;
	}

  return true;
}

/* Apply a verified plan: queue every register replacement in one change
   group (recog and constraints re-verify each rewritten instruction),
   then fix the dead-note registers, move the excluded members, and issue
   the bridge moves.  */

static bool
crf_apply (crf_block &blk, crf_plan &plan)
{
  /* Replace registers per value, ROLE-AWARE: a definition rename touches
     only definition positions (SET_DEST outside a MEM), a use rename only
     use positions.  One instruction can carry two same-numbered registers
     belonging to different values with different targets (the abutting
     accumulator chain).  Renames can chain (L5->L1 while L1->L3), so all
     locations are collected against the ORIGINAL patterns first.  */
  std::vector<std::pair<rtx_insn *, std::pair<rtx *, unsigned>>> changes;
  auto queue_reg = [&changes] (rtx_insn *insn, unsigned from, unsigned to,
			       bool def_side)
  {
    auto walk = [&] (auto &self, rtx *loc, bool in_def) -> void
    {
      rtx x = *loc;
      if (!x)
	return;
      switch (GET_CODE (x))
	{
	case REG:
	  if (REGNO (x) == from && in_def == def_side)
	    changes.push_back ({ insn, { loc, to } });
	  return;
	case SET:
	  self (self, &SET_SRC (x), false);
	  self (self, &SET_DEST (x), true);
	  return;
	case CLOBBER:
	  self (self, &XEXP (x, 0), true);
	  return;
	case USE:
	case MEM:
	  self (self, &XEXP (x, 0), false);
	  return;
	default:
	  {
	    const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
	    for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
	      if (fmt[i] == 'e')
		self (self, &XEXP (x, i), in_def);
	      else if (fmt[i] == 'E')
		for (int j = XVECLEN (x, i); j--;)
		  self (self, &XVECEXP (x, i, j), in_def);
	  }
	  return;
	}
    };
    walk (walk, &PATTERN (insn), false);
  };

  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      if (v.def_pos >= 0)
	queue_reg (blk.pos[v.def_pos].insn, v.reg, rn.second, true);
      unsigned prev = (unsigned) -1;
      for (unsigned up : v.use_positions)
	{
	  if (up == prev)
	    continue;
	  prev = up;
	  queue_reg (blk.pos[up].insn, v.reg, rn.second, false);
	}
    }

  for (auto const &ch : changes)
    validate_change (ch.first, ch.second.first,
		     gen_rtx_REG (XTT32SImode, ch.second.second), 1);

  if (!apply_change_group ())
    {
      rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_CONSTRAINT, dump_file,
		   "Refusing counted-row family [%u,%u):"
		   " counted-row-rename-constraint: a rewritten instruction"
		   " failed re-recognition\n", plan.clones.front ().begin,
		   plan.clones.front ().end);
      return false;
    }

  /* Dead/unused notes riding the rewritten instructions.  */
  for (auto const &rn : plan.renames)
    {
      crf_value const &v = blk.values[rn.first];
      rtx to_reg = gen_rtx_REG (XTT32SImode, rn.second);
      auto fix_notes = [&] (rtx_insn *insn)
      {
	for (rtx note = REG_NOTES (insn); note; note = XEXP (note, 1))
	  if ((REG_NOTE_KIND (note) == REG_DEAD
	       || REG_NOTE_KIND (note) == REG_UNUSED)
	      && REG_P (XEXP (note, 0))
	      && REGNO (XEXP (note, 0)) == v.reg)
	    XEXP (note, 0) = to_reg;
      };
      if (v.def_pos >= 0)
	fix_notes (blk.pos[v.def_pos].insn);
      for (unsigned up : v.use_positions)
	fix_notes (blk.pos[up].insn);
    }

  /* Move the excluded members and issue the bridges by REALIZING the
     final order of the one plan-order interpreter (crf_plan_order) --
     the same stream the shadow-contract verifier simulated.  Walk the
     order keeping LAST = the previously realized instruction: an
     unmoved position realizes in place (unmoved insns keep block
     order, which the interpreter preserves), a moved member reseats
     directly after LAST, a bridge entry emits its move there.  Moved
     entries always follow at least one realized fixed position or
     moved neighbor by construction (heads/bridges seat at their
     clone's anchor), so LAST is the anchor's final predecessor when
     they are placed -- the same seat the per-clone placement used.  */
  {
    std::vector<int> order = crf_plan_order (blk, plan);
    std::vector<char> is_moved (blk.pos.size (), 0);
    for (unsigned c = 0; c != plan.clones.size (); ++c)
      {
	for (unsigned mpos : plan.moves_head[c])
	  is_moved[mpos] = 1;
	for (unsigned mpos : plan.moves_tail[c])
	  is_moved[mpos] = 1;
      }
    rtx_insn *last = nullptr;
    for (int e : order)
      if (e >= 0 && !is_moved[e])
	{
	  /* Seat for leading moved/bridge entries: the final
	     predecessor of the first fixed position.  */
	  last = blk.pos[e].insn;
	  break;
	}
    gcc_assert (last);
    last = PREV_INSN (last);

    unsigned bridge_ix = 0;
    int bridge_clone = -1;
    for (int e : order)
      {
	if (e >= 0)
	  {
	    rtx_insn *insn = blk.pos[e].insn;
	    if (is_moved[e])
	      reorder_insns (insn, insn, last);
	    last = insn;
	  }
	else
	  {
	    unsigned c = unsigned (-1 - e);
	    if (bridge_clone != int (c))
	      {
		bridge_clone = int (c);
		bridge_ix = 0;
	      }
	    auto const &br = plan.bridges[c][bridge_ix++];
	    rtx mv = gen_rtx_SET (gen_rtx_REG (XTT32SImode, br.first),
				  gen_rtx_REG (XTT32SImode, br.second));
	    rtx_insn *mvi = emit_insn_after (mv, last);
	    gcc_assert (recog_memoized (mvi) >= 0);
	    last = mvi;
	  }
      }
  }

  if (dump_file)
    {
      int bridges_total = 0, moved_total = 0;
      for (unsigned c = 0; c != plan.clones.size (); ++c)
	{
	  bridges_total += int (plan.bridges[c].size ());
	  moved_total += int (plan.moves_head[c].size ()
			      + plan.moves_tail[c].size ());
	}
      fprintf (dump_file, "Canonicalized counted-row family: %u clones,"
	       " %u members, %d renames, %d bridges, %d moved,"
	       " modeled saving %d slots\n",
	       unsigned (plan.clones.size ()),
	       unsigned (plan.members[0].size ()),
	       int (plan.renames.size ()), bridges_total, moved_total,
	       plan.saving);
      for (unsigned c = 0; c != plan.clones.size (); ++c)
	fprintf (dump_file,
		 "  clone %u at [%u,%u): %u+%u moved, %u bridged\n",
		 c, plan.clones[c].begin, plan.clones[c].end,
		 unsigned (plan.moves_head[c].size ()),
		 unsigned (plan.moves_tail[c].size ()),
		 unsigned (plan.bridges[c].size ()));
    }
  return true;
}

/* Form the record and launches for an applied plan, mirroring
   replace_sequence: the first clone hosts the capture (executing while
   recording where the target allows), every other clone collapses to a
   launch.  The consumed slots are marked persistent so the word-exact
   machinery below never reallocates them.  */

static void
crf_form (crf_block &blk, crf_plan &plan, unsigned slot_start)
{
  unsigned length = plan.residual;
  bool not_quasar_fix = !(riscv_tt_fix_qsr_replay > 0);

  rtx capture = gen_rvtt_ttreplay_int
    (const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
     rvtt_gen_rtx_noval (XTT32SImode),
     GEN_INT (slot_start), GEN_INT (not_quasar_fix), GEN_INT (1));
  emit_insn_before (capture, blk.pos[plan.members[0].front ()].insn);

  bool keep = not_quasar_fix;
  for (unsigned c = 0; c != plan.clones.size (); ++c)
    {
      if (c == 0 && keep)
	continue;
      rtx replay = gen_rvtt_ttreplay_int
	(const0_rtx, const0_rtx, const0_rtx, GEN_INT (length),
	 rvtt_gen_rtx_noval (XTT32SImode), GEN_INT (slot_start),
	 const0_rtx, const0_rtx);
      emit_insn_after (replay, blk.pos[plan.members[c].back ()].insn);
      if (c != 0)
	for (unsigned m = 0; m != length; ++m)
	  SET_INSN_DELETED (blk.pos[plan.members[c][m]].insn);
    }

  if (dump_file)
    fprintf (dump_file, "Formed counted-row record [%u,+%u): %u launch"
	     " sites\n", slot_start, length,
	     unsigned (plan.clones.size ()) - keep);
}

/* Driver: canonicalize parameterized counted-row families so the ordinary
   word-exact discovery below records one parameterized row program per
   family.  Budget honesty: candidates are ranked by modeled slot saving
   with shorter residuals winning ties, and the local budget model shrinks
   by each applied family's residual.  */

void
canonicalize_counted_rows (function *cfn,
			   std::vector<replay_span> const &replay_spans,
			   std::vector<bool> &persistent_slots,
			   bitmap dirty_bbs, bool sticky)
{
  auto spans = available_replay_spans (replay_spans, persistent_slots);
  if (spans.empty ())
    return;
  unsigned budget = spans.front ().end;

  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      if (bitmap_bit_p (dirty_bbs, bb->index))
	continue;

      /* Iterate: apply the best verifiable family, rescan, repeat.
         Instructions of an applied family are frozen: a later family may
         not rewrite values they define or use, or it would break the
         earlier family's canonical form.  */
      std::set<rtx_insn *> frozen;
      for (unsigned round = 0; round != 8 && budget >= MIN_SEQUENCE;
	   ++round)
	{
	  crf_block blk;
	  if (!crf_scan_block (bb, blk))
	    break;

	  std::vector<crf_seq> list;
	  crf_build_sequences (list, blk, budget);

	  /* Rank candidates: modeled saving descending, residual
	     ascending (budget honesty), position ascending.  */
	  std::vector<unsigned> order;
	  for (unsigned ix = 1; ix < list.size (); ++ix)
	    {
	      crf_seq &seq = list[ix];
	      if (seq.clones.size () < 2 || seq.length < MIN_SEQUENCE)
		continue;
	      int bound = int (seq.clones.size () - 1)
		* int (seq.length - 1) - 1;
	      if (bound < 1)
		continue;
	      /* Identity families (no divergence, nothing excludable in
	         any span) are the word-exact machinery's own territory.  */
	      bool any_divergence = false;
	      for (auto const &c : seq.clones)
		{
		  for (auto const &pr : c.p2r)
		    if (pr.first != pr.second)
		      {
			any_divergence = true;
			break;
		      }
		  if (any_divergence)
		    break;
		  for (unsigned pos = c.begin;
		       pos != c.end && !any_divergence; ++pos)
		    if (blk.pos[pos].excludable)
		      any_divergence = true;
		  if (any_divergence)
		    break;
		}
	      if (!any_divergence)
		continue;
	      order.push_back (ix);
	    }
	  auto rank = [&list] (unsigned ix, int *bound, unsigned *residual,
			       unsigned *begin)
	  {
	    crf_seq &s = list[ix];
	    *bound = int (s.clones.size () - 1) * int (s.length - 1);
	    *residual = s.length;
	    *begin = s.clones.front ().begin;
	  };
	  std::sort (order.begin (), order.end (),
		     [&rank] (unsigned a, unsigned b)
	  {
	    int ba, bb_;
	    unsigned ra, rb, pa, pb;
	    rank (a, &ba, &ra, &pa);
	    rank (b, &bb_, &rb, &pb);
	    if (ba != bb_)
	      return ba > bb_;
	    if (ra != rb)
	      return ra < rb;
	    return pa < pb;
	  });
	  /* Deterministic-rank truncation: 64 is an uncited engineering
	     search budget (DG2 ref-cap class, FH audit FHO-8), byte-stable
	     because the ranking above is total.  A candidate dropped here is
	     silently not formed -- widening or a cost.md derivation is the
	     counted-row owner's follow-up.  */
	  if (order.size () > 64)
	    order.resize (64);
	  if (dump_file)
	    for (unsigned oi = 0; oi != order.size () && oi != 12; ++oi)
	      {
		crf_seq &s = list[order[oi]];
		fprintf (dump_file, "counted-row candidate %u: [%u,%u)"
			 " len %u clones %u\n", oi,
			 s.clones.front ().begin, s.clones.front ().end,
			 s.length, unsigned (s.clones.size ()));
	      }

	  /* Verify every ranked candidate and apply the best VERIFIED
	     plan: a high-bound family that lost most of its clones must
	     not shadow a smaller family that survived whole.  */
	  bool applied = false;
	  crf_plan best;
	  bool have_best = false;
	  for (unsigned ix : order)
	    for (unsigned ref = 0;
		 ref != 8 && ref < list[ix].clones.size (); ++ref)
	    {
	      crf_plan plan;
	      if (!crf_verify_family (blk, list[ix], budget, sticky, plan,
				      ref))
		continue;
	      bool touches_frozen = false;
	      for (auto const &rn : plan.renames)
		{
		  crf_value const &v = blk.values[rn.first];
		  if (v.def_pos >= 0
		      && frozen.count (blk.pos[v.def_pos].insn))
		    touches_frozen = true;
		  for (unsigned up : v.use_positions)
		    if (frozen.count (blk.pos[up].insn))
		      touches_frozen = true;
		}
	      if (touches_frozen)
		{
		  rvtt_refuse (RVTT_REF_COUNTED_ROW_RENAME_INTERFERENCE,
			       dump_file,
			       "Refusing counted-row family"
			       " [%u,%u): counted-row-rename-interference:"
			       " rewrite touches an already-canonicalized"
			       " family\n", plan.clones.front ().begin,
			       plan.clones.front ().end);
		  continue;
		}
	      if (dump_file)
		fprintf (dump_file, "counted-row verified [%u,%u) ref %u:"
			 " %u clones, saving %d\n",
			 plan.clones.front ().begin,
			 plan.clones.front ().end, ref,
			 unsigned (plan.clones.size ()), plan.saving);
	      if (!have_best || plan.saving > best.saving
		  || (plan.saving == best.saving
		      && plan.residual < best.residual))
		{
		  best = std::move (plan);
		  have_best = true;
		}
	    }
	  if (have_best && crf_apply (blk, best))
	    {
	      /* Best-fit slot span (smallest that holds the record).  */
	      auto avail = available_replay_spans (replay_spans,
						   persistent_slots);
	      unsigned start = 0;
	      bool found = false;
	      for (auto pos = avail.rbegin (); pos != avail.rend (); ++pos)
		if (pos->end >= best.residual)
		  {
		    start = pos->begin;
		    found = true;
		    break;
		  }
	      gcc_assert (found); /* budget model guaranteed a fit */
	      std::fill (persistent_slots.begin () + start,
			 persistent_slots.begin () + start + best.residual,
			 true);
	      crf_form (blk, best, start);
	      for (unsigned c = 0; c != best.clones.size (); ++c)
		for (unsigned m = 0; m != best.members[c].size (); ++m)
		  frozen.insert (blk.pos[best.members[c][m]].insn);
	      auto navail = available_replay_spans (replay_spans,
						    persistent_slots);
	      budget = navail.empty () ? 0 : navail.front ().end;
	      applied = true;
	    }
	  if (!applied)
	    break;
	}
    }
}
