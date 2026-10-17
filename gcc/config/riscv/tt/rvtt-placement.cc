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

/* Shared pricing helpers for placement decisions, the fold-demand scan
   and the LREG reserve check.  Legality proofs stay in the calling
   passes; word and slot costs come from rvtt-delivery-cost and trip
   counts from rvtt-trips.  */

/* Return the trip weight of a placement inside LOOP: body and entry
   execution weights scaled as in rvtt-delivery-cost, with proven=false
   when LOOP's trip count is unknown.  Outside any real loop the weight
   is exactly 1/1.  */

rvtt_place_weight
rvtt_place_loop_weight (class loop *loop)
{
  rvtt_place_weight w = { 1, 1, true };
  if (!loop || !loop_outer (loop))
    /* Not inside a real loop: the placement executes once per entry.  */
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

/* Return the amortized net benefit, in centislots, of a placement that
   saves WORDS_BODY_SAVED issue words per body execution and pays
   WORDS_ENTRY_PAID words once per entry, weighted by W.  Both sides are
   priced as RISC-pushed words (placement words are never delivered by
   replay).  A negative result means the placement does not pay off.  */

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

/* ---------------------- fold-demand scan ------------------------ */

/* Key of a constant-operand Dst load: the plain sfpload builtin (the
   _lv form carries a merge operand that the RTL fold refuses) with all
   scalar operands constant, keyed by basic block because the RTL fold
   records per block.  */

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

/* Scan FN for identity-reload fold demand: each repeated all-constant
   sfpload tuple within a block counts as one shape saving one SFPLOAD
   word per execution of the reload.  Return the shape count, the best
   single-shape benefit and the number of unpriceable shapes.  More than
   RVTT_PLACE_MAX_CANDIDATES distinct tuples refuses the whole scan (zero
   demand).  This only estimates demand; whether a shape folds is decided
   by the RTL fold in rtl-rvtt-dst-ownership.cc.  DUMP is the dump file.  */

rvtt_place_fold_demand
rvtt_place_fold_demand_of (function *fn, FILE *dump)
{
  rvtt_place_fold_demand d = { 0, 0, 0 };

  /* Linear scan; kernel bodies are small.  */
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
		/* Refuse the whole scan past the cap, so a park is never
		   outbid by demand that was not counted.  */
		rvtt_refuse (RVTT_REF_PLACE_BUDGET_EXHAUSTED, dump,
			     "placement-arbiter: fold-demand scan refused "
			     "(place-budget-exhausted): more than %d "
			     "distinct Dst-load tuples\n",
			     RVTT_PLACE_MAX_CANDIDATES);
		return rvtt_place_fold_demand { 0, 0, 0 };
	      }
	    seen.push_back (key);
	    seen_stmt.push_back (call);
	    continue;
	  }

	/* A repeated identical tuple: one shape, priced at one SFPLOAD
	   word per execution of this position and nothing at entry.  */
	rvtt_place_weight w
	  = rvtt_place_loop_weight (bb->loop_father);
	if (!w.proven)
	  {
	    d.unpriceable++;
	    rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE, dump,
			 "placement-arbiter: fold-demand shape at bb %d "
			 "refused pricing (place-alternative-unpriceable: "
			 "trip weight unproven); it contributes no "
			 "demand\n", bb->index);
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

/* Return true when FN's best fold-demand benefit strictly exceeds
   PARK_BENEFIT, the net benefit of parking a value in the last free
   LREG; the park must then leave that LREG to the identity-reload fold.
   If PARK_PRICEABLE is false the park is never outbid.  TIER names the
   calling placement tier in the dump.  */

bool
rvtt_place_fold_reserve_outbids (function *fn, int64_t park_benefit,
				 bool park_priceable, const char *tier,
				 FILE *dump)
{
  rvtt_place_fold_demand d = rvtt_place_fold_demand_of (fn, dump);
  if (!d.shapes)
    return false;
  if (!park_priceable)
    {
      /* An unpriceable park is never outbid; keep the existing
	 placement.  */
      rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE, dump,
		   "placement-arbiter: %s marginal park refused pricing "
		   "(place-alternative-unpriceable); fold reserve does not "
		   "outbid it\n", tier);
      return false;
    }
  bool outbid = d.best_benefit > park_benefit;
  if (dump)
    fprintf (dump,
	     "placement-arbiter: %s marginal LREG park bid %" PRId64
	     " vs fold-demand bid %" PRId64 " (%u shape(s)): %s\n",
	     tier, park_benefit, d.best_benefit, d.shapes,
	     !outbid ? "park keeps the register"
	     : "fold reserve outbids (tier-conflict"
	     " place-fold-reserve-outbid)");
  return outbid;
}
