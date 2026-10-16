/* Placement arbitration for loop-invariant constants.
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

#ifndef GCC_RVTT_PLACEMENT_H
#define GCC_RVTT_PLACEMENT_H

/* Placement arbitration for loop-invariant constants.

   Where a loop-invariant constant lives is decided by several passes (the
   early invariant hoist's park-ordering heuristics and the const-residency
   placement tiers: PRGM park, pressure-park LREG tier), and the decisions
   interact through the 8-LREG file: a constant parked in an LREG can leave
   a downstream identity-reload fold (a repeated typed Dst load of an
   identical constant operand tuple reusing the earlier load's value)
   without the register it needs.

   This module prices those alternatives; it does not prove them.  Every
   legality proof stays in its owning pass, and the arbiter only orders
   admitted alternatives by price.  Prices are in centislots via
   rvtt-delivery-cost, on the RISC-push plane (placement words are pushed,
   never replay-delivered at their programming points); pressure comes from
   rvtt-pressure and trip facts from rvtt-trips.

   Under -mtt-tensix-optimize-priced-placement (off by default) the priced
   result decides.  An unpriceable ingredient refuses
   (place-alternative-unpriceable) and keeps the policy decision, as does
   a candidate set over the budget below (place-budget-exhausted).  The
   pressure-park tier's marginal LREG park can be outbid by the priced
   downstream fold demand (place-fold-reserve-outbid).  */

/* A single decision point arbitrates at most this many candidates;
   beyond it the arbiter refuses (place-budget-exhausted) and the existing
   policy stands.  */
#define RVTT_PLACE_MAX_CANDIDATES 64

/* Trip weight of one placement's amortization: BODY executions per ENTRY
   execution (scaled into 48 bits by the shared discipline).  PROVEN is
   false when LOOP is a real loop whose trip count rvtt-trips cannot prove
   -- the alternative is then unpriceable.  A null or root LOOP prices
   exactly once (1/1, proven).  */

struct rvtt_place_weight
{
  int64_t entry;
  int64_t body;
  bool proven;
};

extern rvtt_place_weight rvtt_place_loop_weight (class loop *loop);

/* Amortized net benefit, in centislots, of a placement that removes
   WORDS_BODY_SAVED issue words from every body execution and pays
   WORDS_ENTRY_PAID issue words once per entry (RISC-push plane).  */

extern int64_t rvtt_place_net_benefit (unsigned words_body_saved,
				       unsigned words_entry_paid,
				       const rvtt_place_weight &w);

/* Priced identity-reload fold demand of FN: the gimple pre-image scan of
   the identity-reload fold (two typed Dst loads with an identical constant
   operand tuple; the RTL fold keeps every proof and still decides the fold
   itself).  SHAPES counts the priceable duplicate groups (capped at
   RVTT_PLACE_MAX_CANDIDATES); BEST_BENEFIT is the highest single-fold
   benefit among them (one delivered SFPLOAD word per execution of the
   reload's position); UNPRICEABLE counts shapes refused pricing
   (trip-unproven positions), which contribute no demand, so an
   unpriceable fold never outbids a park.  */

struct rvtt_place_fold_demand
{
  unsigned shapes;
  unsigned unpriceable;
  int64_t best_benefit;
};

extern rvtt_place_fold_demand rvtt_place_fold_demand_of (function *fn,
							 FILE *dump);

/* Return true when the marginal LREG park with amortized net benefit
   PARK_BENEFIT (priced by the caller from its own candidate facts) must
   yield the last free LREG to FN's priced fold demand.  A park with
   PARK_PRICEABLE false is never outbid.  TIER names the calling tier in
   the dump line.  Callers consult this only when their residual budget is
   down to the final register.  */

extern bool rvtt_place_fold_reserve_outbids (function *fn,
					     int64_t park_benefit,
					     bool park_priceable,
					     const char *tier, FILE *dump);

#endif /* GCC_RVTT_PLACEMENT_H */
