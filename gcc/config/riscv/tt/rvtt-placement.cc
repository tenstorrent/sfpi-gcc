/* One placement arbiter for invariant constants.
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

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "ssa.h"
#include "cfgloop.h"
#include "rvtt.h"
#include "rvtt-refuse.h"
#include "rvtt-trips.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-placement.h"

/* See rvtt-placement.h for the contract.  The module is deliberately
   small: pricing primitives + the fold-demand census + the reserve
   verdict + the uniform shadow line.  Everything that PROVES stays in
   the owning passes; everything that COUNTS words or slots routes
   through tt/rvtt-delivery-cost; every trip fact routes through
   tt/rvtt-trips.  */

/* Proven trip weight of a placement inside LOOP: body/entry execution
   weights (delivery-cost scaled) with proven=false when LOOP's trip
   count cannot be established.  Outside any real loop the weight is
   the exact 1/1.  */

rvtt_place_weight
rvtt_place_loop_weight (class loop *loop)
{
  rvtt_place_weight w = { 1, 1, true };
  if (!loop || !loop_outer (loop))
    /* Not inside a real loop: the placement executes exactly once per
       entry; 1/1 is exact, not an estimate.  */
    return w;

  unsigned HOST_WIDE_INT trips;
  if (!rvtt_loop_trips_gimple (loop, &trips) || trips == 0)
    {
      w.proven = false;
      return w;
    }
  w.body = (int64_t) trips;
  w.entry = 1;
  rvtt_delivery_cost::scale_trip_weight (&w.body, &w.entry);
  return w;
}

/* Amortized net benefit, in centislots, of a placement that saves
   WORDS_BODY_SAVED issue words per body execution and pays
   WORDS_ENTRY_PAID issue words once per entry, weighted by the proven
   trip weight W.  Both sides price on the RISC-push plane (placement
   words are pushed, never replay-delivered).  Negative means the
   placement loses.  */

int64_t
rvtt_place_net_benefit (unsigned words_body_saved, unsigned words_entry_paid,
			const rvtt_place_weight &w)
{
  gcc_checking_assert (w.proven);
  int64_t body = rvtt_dcost_words_to_centislots
    ((int64_t) words_body_saved, rvtt_delivery_cost::PLANE_RISC_PUSH);
  int64_t entry = rvtt_dcost_words_to_centislots
    ((int64_t) words_entry_paid, rvtt_delivery_cost::PLANE_RISC_PUSH);
  return body * w.body - entry * w.entry;
}

/* ---------------------- fold-demand census ------------------------ */

/* The dst-ownership identity-reload fold's gimple pre-image: a typed
   Dst load (rvtt sfpload builtin; the plain form -- an _lv form
   carries a live-value merge operand the fold's noval requirement
   refuses) whose every scalar operand is a literal constant, repeated
   with an identical operand tuple in the SAME basic block (the RTL
   pass's record scope is per-block).  The census is a structural
   DEMAND count only: whether any individual shape folds is decided,
   with all its proofs (RWC/layout epochs, CC lattice, noval, its own
   pressure guard), by rtl-rvtt-dst-ownership.cc exactly as today.  */

struct fold_shape_key
{
  basic_block bb;
  unsigned HOST_WIDE_INT args[8];
  unsigned nargs;

  bool operator== (const fold_shape_key &o) const
  {
    if (bb != o.bb || nargs != o.nargs)
      return false;
    for (unsigned i = 0; i < nargs; i++)
      if (args[i] != o.args[i])
	return false;
    return true;
  }
};

/* Census FN for priced identity-reload fold demand: scan every basic
   block for typed Dst loads (sfpload calls) whose operand tuple is
   all-constant, and price each REPEATED identical tuple in the same
   block as one fold shape saving one delivered SFPLOAD word per
   execution of the reload's position.  Returns the shape count, the
   best single-shape benefit, and the unpriceable count; a census with
   more than RVTT_PLACE_MAX_CANDIDATES distinct tuples refuses whole
   (zero demand).  DECIDE selects stage-B registry refusals over
   stage-A shadow dump lines.  Structural demand only -- whether a
   shape actually folds stays decided by rtl-rvtt-dst-ownership.cc.  */

rvtt_place_fold_demand
rvtt_place_fold_demand_of (function *fn, FILE *dump, bool decide)
{
  rvtt_place_fold_demand d = { 0, 0, 0 };

  /* Deterministic linear scan; kernel-scale function bodies.  */
  std::vector<fold_shape_key> seen;
  std::vector<gimple *> seen_stmt;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	if (!call)
	  continue;
	const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	if (!insnd || insnd->id != rvtt_insn_data::sfpload)
	  continue;

	fold_shape_key key;
	key.bb = bb;
	key.nargs = 0;
	bool constant = true;
	/* Skip operand 0 (the architectural instruction-buffer
	   address; not part of the delivered word).  */
	for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
	  {
	    tree arg = gimple_call_arg (call, ix);
	    if (TREE_CODE (arg) != INTEGER_CST
		|| key.nargs >= ARRAY_SIZE (key.args))
	      {
		constant = false;
		break;
	      }
	    key.args[key.nargs++] = TREE_INT_CST_LOW (arg);
	  }
	if (!constant)
	  continue;

	bool duplicate = false;
	for (unsigned i = 0; i < seen.size (); ++i)
	  if (seen[i] == key)
	    {
	      duplicate = true;
	      break;
	    }
	if (!duplicate)
	  {
	    if (seen.size () >= RVTT_PLACE_MAX_CANDIDATES)
	      {
		/* Deterministic arbitration budget: refuse the whole
		   census; no demand is derived past the cap (the
		   fail-closed direction -- no park is ever outbid by
		   an uncounted fold).  */
		if (decide)
		  rvtt_refuse (RVTT_REF_PLACE_BUDGET_EXHAUSTED, dump,
			       "placement-arbiter: fold-demand census refused "
			       "(place-budget-exhausted): more than %d "
			       "distinct Dst-load tuples\n",
			       RVTT_PLACE_MAX_CANDIDATES);
		else if (dump)
		  fprintf (dump,
			   "placement-arbiter: fold-demand census over "
			   "budget (more than %d distinct Dst-load tuples); "
			   "no demand derived\n", RVTT_PLACE_MAX_CANDIDATES);
		return rvtt_place_fold_demand { 0, 0, 0 };
	      }
	    seen.push_back (key);
	    seen_stmt.push_back (call);
	    continue;
	  }

	/* A repeated identical tuple: one fold-demand shape at this
	   statement's position.  Price one delivered SFPLOAD word per
	   execution of the reload position, nothing at entry.  */
	rvtt_place_weight w
	  = rvtt_place_loop_weight (bb->loop_father);
	if (!w.proven)
	  {
	    d.unpriceable++;
	    if (decide)
	      rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE, dump,
			   "placement-arbiter: fold-demand shape at bb %d "
			   "refused pricing (place-alternative-unpriceable: "
			   "trip weight unproven); it contributes no "
			   "demand\n", bb->index);
	    else if (dump)
	      fprintf (dump,
		       "placement-arbiter: fold-demand shape at bb %d "
		       "unpriceable (trip weight unproven); it contributes "
		       "no demand\n", bb->index);
	    continue;
	  }
	int64_t benefit = rvtt_place_net_benefit (1, 0, w);
	d.shapes++;
	if (benefit > d.best_benefit)
	  d.best_benefit = benefit;
	if (dump)
	  fprintf (dump,
		   "placement-arbiter: fold-demand shape at bb %d "
		   "(duplicate Dst-load tuple): benefit %" PRId64
		   " centislots (weight %" PRId64 "/%" PRId64 ")\n",
		   bb->index, benefit, w.body, w.entry);
      }
  return d;
}

/* The pressure-park reserve verdict: true when FN's best priced
   fold-demand bid strictly exceeds PARK_BENEFIT, the marginal LREG
   park's amortized net benefit -- the park must then yield the last
   free LREG to the downstream identity-reload fold.  When
   PARK_PRICEABLE is false the park is never outbid (fail closed; a
   named refusal fires when DECIDE).  TIER names the calling placement
   tier in the dump line; DECIDE selects stage-B wording and registry
   refusals over the stage-A shadow wording.  */

bool
rvtt_place_fold_reserve_outbids (function *fn, int64_t park_benefit,
				 bool park_priceable, const char *tier,
				 FILE *dump, bool decide)
{
  rvtt_place_fold_demand d = rvtt_place_fold_demand_of (fn, dump, decide);
  if (!d.shapes)
    return false;
  if (!park_priceable)
    {
      /* Fail closed: an unpriceable park is never outbid -- the legacy
	 placement stands.  */
      if (decide)
	rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE, dump,
		     "placement-arbiter: %s marginal park refused pricing "
		     "(place-alternative-unpriceable); fold reserve does not "
		     "outbid it\n", tier);
      else if (dump)
	fprintf (dump,
		 "placement-arbiter: %s marginal park unpriceable; fold "
		 "reserve does not outbid it\n", tier);
      return false;
    }
  bool outbid = d.best_benefit > park_benefit;
  if (dump)
    fprintf (dump,
	     "placement-arbiter: %s marginal LREG park bid %" PRId64
	     " vs fold-demand bid %" PRId64 " (%u shape(s)): %s\n",
	     tier, park_benefit, d.best_benefit, d.shapes,
	     !outbid ? "park keeps the register"
	     : decide ? "fold reserve outbids (tier-conflict"
	     " place-fold-reserve-outbid)"
	     : "fold demand outbids (shadow; the established park"
	     " stands)");
  return outbid;
}

/* Emit the uniform shadow-census line to DUMP: decision point POINT
   computed LEGACY and PRICED verdicts, tagged AGREE/DISAGREE by
   string equality, with optional trailing DETAIL.  The corpus census
   greps exactly this line format; no verdict is applied here.  */

void
rvtt_place_dump_verdict (FILE *dump, const char *point, const char *legacy,
			 const char *priced, const char *detail)
{
  if (!dump)
    return;
  fprintf (dump, "placement-arbiter: %s legacy=%s priced=%s %s%s%s\n",
	   point, legacy, priced,
	   strcmp (legacy, priced) == 0 ? "AGREE" : "DISAGREE",
	   detail && *detail ? " " : "", detail ? detail : "");
}
